#include <hal/video.h>
#include <hal/debug.h>
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <nxdk/net.h>
#include "xifi_detect.h"
#include "send_cmd.h"
#include "kybd.h"

#define MUSIC_VOLUME      0.3f
#define SCREEN_WIDTH_DEF  1280
#define SCREEN_HEIGHT_DEF 720
#define MENU_ITEM_COUNT   7
#define MENU_REPEAT_DELAY 200
#define MENU_REPEAT_RATE  60

static int screen_width = SCREEN_WIDTH_DEF, screen_height = SCREEN_HEIGHT_DEF;
static FILE* audio_file = NULL;

// --- Audio callback: applies volume scaling and loops audio ---
void AudioCallback(void* userdata, Uint8* stream, int len) {
    if (!audio_file) {
        SDL_memset(stream, 0, len);
        return;
    }
    size_t r = fread(stream, 1, len, audio_file);
    if (r < (size_t)len) {
        fseek(audio_file, 44, SEEK_SET);
        fread(stream + r, 1, len - r, audio_file);
    }
    int16_t* samples = (int16_t*)stream;
    int count = len / sizeof(int16_t);
    for (int i = 0; i < count; i++) {
        float v = samples[i] * MUSIC_VOLUME;
        if (v < -32768.f) v = -32768.f;
        else if (v > 32767.f) v = 32767.f;
        samples[i] = (int16_t)v;
    }
}

// --- Draws an octagon border ---
static void DrawOct(SDL_Renderer *r, SDL_Rect rc, int m, SDL_Color c) {
    int l = rc.x - m, t = rc.y - m;
    int w = rc.w + 2*m, h = rc.h + 2*m;
    SDL_Point pts[9] = {
        {l+m,    t}, {l+w-m,  t},
        {l+w,    t+m}, {l+w,    t+h-m},
        {l+w-m,  t+h}, {l+m,    t+h},
        {l,      t+h-m}, {l,      t+m},
        {l+m,    t}
    };
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLines(r, pts, 9);
}

// --- Fills the inside of an octagon ---
static void FillOct(SDL_Renderer *r, SDL_Rect rc, int m, SDL_Color c) {
    int l = rc.x - m, t = rc.y - m;
    int w = rc.w + 2*m, h = rc.h + 2*m;
    SDL_Point pts[8] = {
        {l+m,    t}, {l+w-m,  t}, {l+w,    t+m}, {l+w,    t+h-m},
        {l+w-m,  t+h}, {l+m,    t+h}, {l,      t+h-m}, {l,      t+m}
    };
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int y = t; y <= t + h; y++) {
        int x[8], n = 0;
        for (int i = 0; i < 8; i++) {
            SDL_Point p1 = pts[i];
            SDL_Point p2 = pts[(i+1)%8];
            if ((y >= p1.y && y < p2.y) || (y >= p2.y && y < p1.y)) {
                int dy = p2.y - p1.y;
                if (dy != 0) {
                    int ix = p1.x + (int)((float)(y - p1.y) * (float)(p2.x - p1.x) / (float)dy);
                    x[n++] = ix;
                }
            }
        }
        if (n < 2) continue;
        if (x[0] > x[1]) { int tmp = x[0]; x[0] = x[1]; x[1] = tmp; }
        SDL_RenderDrawLine(r, x[0], y, x[1], y);
    }
}

// ---- SCALED COORDINATE HELPERS ----
#define SCALEX(x) ((int)((float)(x) * screen_width / (float)SCREEN_WIDTH_DEF))
#define SCALEY(y) ((int)((float)(y) * screen_height / (float)SCREEN_HEIGHT_DEF))

int main(void) {
    // Set texture filtering to linear for smooth scaling of images/logos
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    // --- VIDEO MODE PROBE ---
    struct { int w, h, mode; } modes[] = {
        {1280, 720, REFRESH_DEFAULT},
        {720, 480, REFRESH_DEFAULT},
        {640, 480, REFRESH_DEFAULT},
    };
    bool found = false;
    for (size_t i = 0; i < sizeof(modes)/sizeof(modes[0]); ++i) {
        if (XVideoSetMode(modes[i].w, modes[i].h, 32, modes[i].mode) == TRUE) {
            screen_width = modes[i].w;
            screen_height = modes[i].h;
            found = true;
            break;
        }
    }
    if (!found) return 0;

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0)
        return 0;
    if (TTF_Init() == -1) return 0;
    if ((IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG) & (IMG_INIT_JPG | IMG_INIT_PNG))
        != (IMG_INIT_JPG | IMG_INIT_PNG)) return 0;

    if (nxNetInit(NULL) != 0) {
        debugPrint("Network initialization failed!\n");
        return 1;
    }

    XiFi_StartDetectionThread(2000);

    // --- AUDIO SETUP ---
    audio_file = fopen("D:\\media\\bg\\bg.wav", "rb");
    if (!audio_file) return 0;
    static char buf[64*1024];
    setvbuf(audio_file, buf, _IOFBF, sizeof(buf));
    fseek(audio_file, 44, SEEK_SET);
    SDL_AudioSpec spec = {0};
    spec.freq     = 44100;
    spec.format   = AUDIO_S16LSB;
    spec.channels = 2;
    spec.samples  = 2048;
    spec.callback = AudioCallback;
    if (SDL_OpenAudio(&spec, NULL) < 0) return 0;
    SDL_PauseAudio(0);

    // --- CONTROLLER SETUP ---
    SDL_GameController* controller = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            break;
        }
    }

    // --- WINDOW & RENDERER ---
    SDL_Window* window = SDL_CreateWindow(
        "XiFi Configuration",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        screen_width, screen_height, SDL_WINDOW_SHOWN
    );
    if (!window) return 0;

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        SDL_DestroyWindow(window);
        return 0;
    }

    SDL_Surface* bgSurface = IMG_Load("D:\\media\\img\\background.jpg");
    SDL_Texture* bgTexture = bgSurface
        ? SDL_CreateTextureFromSurface(renderer, bgSurface)
        : NULL;
    if (bgSurface) SDL_FreeSurface(bgSurface);

    // --- FONT SIZES ---
    int font48_sz = screen_height/15;
    int font24_sz = screen_height/30;
    int font28_sz = screen_height/25;

    TTF_Font* font48 = TTF_OpenFont("D:\\media\\font\\font.ttf", font48_sz);
    TTF_Font* font24 = TTF_OpenFont("D:\\media\\font\\font.ttf", font24_sz);
    TTF_Font* font28 = TTF_OpenFont("D:\\media\\font\\font.ttf", font28_sz);

    SDL_Texture* titleTex = NULL;
    SDL_Rect titleR = {0};
    if (font48) {
        SDL_Surface* ts = TTF_RenderText_Blended(
            font48, "XiFi Configuration", (SDL_Color){255,255,255,255});
        titleTex = SDL_CreateTextureFromSurface(renderer, ts);
        titleR = (SDL_Rect){ (screen_width - ts->w)/2, SCALEY(50), ts->w, ts->h };
        SDL_FreeSurface(ts);
    }

    // --- BOTTOM "PRESS B TO EXIT" LABEL ---
    SDL_Texture *ep=NULL, *eb=NULL, *ep2=NULL;
    SDL_Rect epr={0}, ebr={0}, ep2r={0};
    if (font24) {
        SDL_Color white={200,200,200,255}, red={255,0,0,255};
        SDL_Surface* s1 = TTF_RenderText_Blended(font24, "Press ", white);
        ep = SDL_CreateTextureFromSurface(renderer, s1);
        epr = (SDL_Rect){0,0,s1->w,s1->h}; SDL_FreeSurface(s1);
        SDL_Surface* s2 = TTF_RenderText_Blended(font24, "B", red);
        eb = SDL_CreateTextureFromSurface(renderer, s2);
        ebr = (SDL_Rect){0,0,s2->w,s2->h}; SDL_FreeSurface(s2);
        SDL_Surface* s3 = TTF_RenderText_Blended(font24, " to exit", white);
        ep2 = SDL_CreateTextureFromSurface(renderer, s3);
        ep2r = (SDL_Rect){0,0,s3->w,s3->h}; SDL_FreeSurface(s3);
        int totalW = epr.w + ebr.w + ep2r.w;
        epr.x = screen_width - totalW - SCALEX(20); epr.y = screen_height - epr.h - SCALEY(20);
        ebr.x = epr.x + epr.w;      ebr.y = epr.y;
        ep2r.x= ebr.x + ebr.w;      ep2r.y= epr.y;
    }

    // --- XIFI/STATUS/IP ---
    SDL_Texture *xiT=NULL, *stT=NULL, *ipT=NULL;
    SDL_Rect xiR={SCALEX(20),0,0,0}, stR={0}, ipR={0};
    if (font24) {
        SDL_Surface* sx = TTF_RenderText_Blended(
            font24, "XiFi ", (SDL_Color){255,255,255,255});
        xiT = SDL_CreateTextureFromSurface(renderer, sx);
        xiR = (SDL_Rect){SCALEX(20), screen_height - sx->h - SCALEY(20), sx->w, sx->h};
        SDL_FreeSurface(sx);
    }

    // --- MENU ITEMS ---
    const char* items[MENU_ITEM_COUNT] = {
        "Start XiFi Portal", "Clear WiFi Password", "Turn off OLED",
        "Turn on OLED",      "Set Custom Status",   "Clear Custom Status",
        "About"
    };
    SDL_Rect mrect[MENU_ITEM_COUNT];
    int cx[2] = {screen_width/4, 3*screen_width/4};
    int ry[4] = {SCALEY(200),SCALEY(300),SCALEY(400),SCALEY(500)};
    int cw = SCALEX(400), ch = SCALEY(80);
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        SDL_Surface* ms = TTF_RenderText_Blended(
            font24, items[i], (SDL_Color){255,255,255,255});
        int w = ms->w, h = ms->h; SDL_FreeSurface(ms);
        int col = (i<6 ? i%2 : 0), row = (i<6 ? i/2 : 3);
        int px = (i<6 ? cx[col]-cw/2 : screen_width/2-cw/2);
        int py = ry[row] - ch/2;
        mrect[i] = (SDL_Rect){ px+(cw-w)/2, py+(ch-h)/2, w, h };
    }
    int selected = 0, aboutOpen = 0, kybdOpen = 0;
    char kb_text[33] = {0};

    SDL_Texture *dcT=NULL, *trT=NULL;
    if (font28) {
        SDL_Surface* d = IMG_Load("D:\\media\\img\\DC.png");
        dcT = d ? SDL_CreateTextureFromSurface(renderer, d) : NULL; if(d)SDL_FreeSurface(d);
        SDL_Surface* t = IMG_Load("D:\\media\\img\\TR.png");
        trT = t ? SDL_CreateTextureFromSurface(renderer, t) : NULL; if(t)SDL_FreeSurface(t);
    }

    // --- MENU FAST KEY REPEAT ---
    static int menu_repeat_dir = 0;
    static uint32_t menu_repeat_start = 0, menu_repeat_last = 0;

    SDL_Event event;
    while (1) {
        // ---- MAIN EVENT LOOP ----
        while (SDL_PollEvent(&event)) {
            if (kybdOpen) {
                int ret = kybd_handle_event(&event, kb_text, sizeof(kb_text));
                if (ret == KYBD_DONE || ret == KYBD_CANCELED) {
                    // Always reset keyboard state/buffer
                    kybdOpen = 0;
                    kb_text[0] = 0;
                }
                continue;
            }

            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                int b = event.cbutton.button;
                bool xifiPresent = XiFi_IsPresent();
                bool isDisabled = !xifiPresent && selected != 6; // 6 = About
                if (aboutOpen) {
                    if (b == SDL_CONTROLLER_BUTTON_B) aboutOpen = 0;
                } else {
                    switch (b) {
                        case SDL_CONTROLLER_BUTTON_B:
                            goto cleanup;
                        case SDL_CONTROLLER_BUTTON_A:
                            if (isDisabled)
                                break;
                            switch (selected) {
                                case 0: send_cmd(XiFi_GetIP(), "0101", NULL); break;
                                case 1: send_cmd(XiFi_GetIP(), "0102", NULL); break;
                                case 2: send_cmd(XiFi_GetIP(), "010E", NULL); break;
                                case 3: send_cmd(XiFi_GetIP(), "010F", NULL); break;
                                case 4:
                                    if (xifiPresent) {
                                        kybdOpen = 1;     // always re-enable overlay
                                        kb_text[0] = 0;   // always clear buffer on entry
                                    }
                                    break;
                                case 5: send_cmd(XiFi_GetIP(), "0111", NULL); break;
                                case 6: aboutOpen = 1; break;
                            }
                            break;
                        case SDL_CONTROLLER_BUTTON_X:
                            if (!xifiPresent) break;
                            send_cmd(XiFi_GetIP(), "0112", NULL); break;
                        case SDL_CONTROLLER_BUTTON_Y:
                            if (!xifiPresent) break;
                            send_cmd(XiFi_GetIP(), "0113", NULL); break;
                        case SDL_CONTROLLER_BUTTON_DPAD_UP:
                            selected = (selected + 5) % MENU_ITEM_COUNT;
                            if (selected == 6) selected = 4;
                            menu_repeat_dir = 1;
                            menu_repeat_start = menu_repeat_last = SDL_GetTicks();
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                            if (selected < 4) selected += 2;
                            else if (selected < 6) selected = 6;
                            else selected %= 2;
                            menu_repeat_dir = 2;
                            menu_repeat_start = menu_repeat_last = SDL_GetTicks();
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                            if (selected % 2) selected--;
                            menu_repeat_dir = 3;
                            menu_repeat_start = menu_repeat_last = SDL_GetTicks();
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                            if (selected % 2 == 0 && selected < 5) selected++;
                            menu_repeat_dir = 4;
                            menu_repeat_start = menu_repeat_last = SDL_GetTicks();
                            break;
                    }
                }
            } else if (event.type == SDL_CONTROLLERBUTTONUP) {
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP ||
                    event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN ||
                    event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ||
                    event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
                    menu_repeat_dir = 0;
                }
            }
        }

        // ---- MENU FAST KEY REPEAT ----
        if (!kybdOpen && menu_repeat_dir != 0) {
            uint32_t now = SDL_GetTicks();
            if (now - menu_repeat_start > MENU_REPEAT_DELAY &&
                now - menu_repeat_last > MENU_REPEAT_RATE) {
                menu_repeat_last = now;
                switch (menu_repeat_dir) {
                    case 1: // UP
                        selected = (selected + 5) % MENU_ITEM_COUNT;
                        if (selected == 6) selected = 4;
                        break;
                    case 2: // DOWN
                        if (selected < 4) selected += 2;
                        else if (selected < 6) selected = 6;
                        else selected %= 2;
                        break;
                    case 3: // LEFT
                        if (selected % 2) selected--;
                        break;
                    case 4: // RIGHT
                        if (selected % 2 == 0 && selected < 5) selected++;
                        break;
                }
            }
        }

        // ---- FAST KEYBOARD REPEAT FOR ONSCREEN KEYBOARD ----
        if (kybdOpen) kybd_update_repeat();

        // -- STATUS AND IP TEXTURES --
        if (stT) SDL_DestroyTexture(stT);
        if (ipT) SDL_DestroyTexture(ipT);
        const char* statusText = XiFi_IsPresent() ? "Detected" : "Not Detected";
        SDL_Color   statusCol  = XiFi_IsPresent()
                                 ? (SDL_Color){0,255,0,255}
                                 : (SDL_Color){255,0,0,255};
        SDL_Surface* ss = TTF_RenderText_Blended(font24, statusText, statusCol);
        stT = SDL_CreateTextureFromSurface(renderer, ss);
        stR = (SDL_Rect){ xiR.x + xiR.w, xiR.y, ss->w, ss->h };
        SDL_FreeSurface(ss);

        const char* show_ip = XiFi_IsPresent() ? XiFi_GetIP() : "";
        if (show_ip[0]) {
            SDL_Surface* ips = TTF_RenderText_Blended(font24, show_ip, (SDL_Color){255,255,255,255});
            ipT = SDL_CreateTextureFromSurface(renderer, ips);
            ipR = (SDL_Rect){ xiR.x + xiR.w + stR.w + SCALEX(10), xiR.y, ips->w, ips->h };
            SDL_FreeSurface(ips);
        } else {
            ipT = NULL;
        }

        // -- Render loop --
        SDL_RenderClear(renderer);
        if (bgTexture)    SDL_RenderCopy(renderer, bgTexture, NULL, NULL);
        if (titleTex)     SDL_RenderCopy(renderer, titleTex,   NULL, &titleR);

        // --- Menu and overlay layering ---
        bool xifiPresent = XiFi_IsPresent();
        // Draw the menu and highlights FIRST (always visible, even when overlay is open)
        if (!aboutOpen && !kybdOpen) {
            for (int i = 0; i < MENU_ITEM_COUNT; i++) {
                SDL_Rect rect = mrect[i];
                SDL_Rect shadow = rect;
                shadow.x += SCALEX(8); shadow.y += SCALEY(8);

                // Draw drop shadow
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 80);
                SDL_RenderFillRect(renderer, &shadow);

                // Draw filled octagon for highlight or gray
                if (i == selected) {
                    FillOct(renderer, rect, SCALEY(12), (SDL_Color){0,220,0,255});
                } else {
                    FillOct(renderer, rect, SCALEY(12), (SDL_Color){36,36,36,255});
                }

                // Draw octagonal border
                SDL_SetRenderDrawColor(renderer, 80, 255, 100, 255);
                DrawOct(renderer, rect, SCALEY(12), (SDL_Color){80, 255, 100, 255});

                // Disabled state: all except About (6) are disabled if not present
                bool isDisabled = !xifiPresent && i != 6;
                SDL_Color textColor = isDisabled
                    ? (SDL_Color){0,0,0,255}
                    : (SDL_Color){255,255,255,255};

                // Draw menu text centered
                SDL_Surface* ms = TTF_RenderText_Blended(
                    font24, items[i], textColor);
                SDL_Texture* tx = SDL_CreateTextureFromSurface(renderer, ms);
                SDL_Rect textRect = rect;
                textRect.x += (rect.w - ms->w) / 2;
                textRect.y += (rect.h - ms->h) / 2;
                textRect.w = ms->w;
                textRect.h = ms->h;
                SDL_FreeSurface(ms);
                SDL_RenderCopy(renderer, tx, NULL, &textRect);
                SDL_DestroyTexture(tx);
            }
        } else {
            // Draw the menu as background (NO highlight), About/keyboard overlay on top
            for (int i = 0; i < MENU_ITEM_COUNT; i++) {
                SDL_Rect rect = mrect[i];

                // Draw menu background (gray oct)
                FillOct(renderer, rect, SCALEY(12), (SDL_Color){36,36,36,255});
                // Octagonal border
                SDL_SetRenderDrawColor(renderer, 80, 255, 100, 255);
                DrawOct(renderer, rect, SCALEY(12), (SDL_Color){80, 255, 100, 255});

                // Disabled state: all except About (6) are disabled if not present
                bool isDisabled = !xifiPresent && i != 6;
                SDL_Color textColor = isDisabled
                    ? (SDL_Color){0,0,0,255}
                    : (SDL_Color){255,255,255,255};

                // Draw menu text centered
                SDL_Surface* ms = TTF_RenderText_Blended(
                    font24, items[i], textColor);
                SDL_Texture* tx = SDL_CreateTextureFromSurface(renderer, ms);
                SDL_Rect textRect = rect;
                textRect.x += (rect.w - ms->w) / 2;
                textRect.y += (rect.h - ms->h) / 2;
                textRect.w = ms->w;
                textRect.h = ms->h;
                SDL_FreeSurface(ms);
                SDL_RenderCopy(renderer, tx, NULL, &textRect);
                SDL_DestroyTexture(tx);
            }

            // Draw About overlay if open (drawn below keyboard if both open)
            if (aboutOpen) {
                SDL_Rect ov = {SCALEX(240), SCALEY(140), SCALEX(800), SCALEY(440)};
                // Drop shadow for overlay
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
                SDL_Rect shadow = {ov.x + SCALEX(14), ov.y + SCALEY(14), ov.w, ov.h};
                SDL_RenderFillRect(renderer, &shadow);

                // About main panel -- SOLID BLACK
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderFillRect(renderer, &ov);

                // Border
                SDL_SetRenderDrawColor(renderer, 80, 255, 100, 255);
                SDL_RenderDrawRect(renderer, &ov);

                // About text content
                const char* lines[] = {
                    "XiFi Config", "",
                    "Code by:", "Darkone83", "",
                    "Music By:", "Darkone83"
                };
                for (int i = 0; i < 7; i++) {
                    if (lines[i][0]) {
                        SDL_Surface* ls = TTF_RenderText_Blended(
                            font28, lines[i], (SDL_Color){255,255,255,255});
                        SDL_Texture* lt = SDL_CreateTextureFromSurface(renderer, ls);
                        SDL_Rect dr = {
                            (screen_width - ls->w) / 2,
                            SCALEY(160) + i * SCALEY(40),
                            ls->w, ls->h
                        };
                        SDL_FreeSurface(ls);
                        SDL_RenderCopy(renderer, lt, NULL, &dr);
                        SDL_DestroyTexture(lt);
                    }
                }

                // --- Logo images, centered with drop shadow and anti-aliased scaling ---
                int sz = SCALEY(64);
                int startX = (screen_width - (sz + SCALEX(20) + sz)) / 2;
                int logoY = SCALEY(140) + 7 * SCALEY(40) + SCALEY(20);

                // Drop shadows for images
                SDL_Rect r1_shadow = {startX + SCALEX(8), logoY + SCALEY(8), sz, sz};
                SDL_Rect r2_shadow = {startX + sz + SCALEX(20) + SCALEX(8), logoY + SCALEY(8), sz, sz};
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 100);
                SDL_RenderFillRect(renderer, &r1_shadow);
                SDL_RenderFillRect(renderer, &r2_shadow);

                // Actual images (now smooth-scaled!)
                SDL_Rect r1 = {startX, logoY, sz, sz};
                SDL_Rect r2 = {startX + sz + SCALEX(20), logoY, sz, sz};
                if (dcT) SDL_RenderCopy(renderer, dcT, NULL, &r1);
                if (trT) SDL_RenderCopy(renderer, trT, NULL, &r2);
            }

            // Draw On-Screen Keyboard overlay if open (always drawn on top)
            if (kybdOpen) {
                // --- MODAL OVERLAY PANEL WITH DROP SHADOW OUTSIDE ---
                int panel_w = SCALEX(960);
                int panel_h = SCALEY(420);
                int panel_x = (screen_width - panel_w) / 2;
                int panel_y = (screen_height - panel_h) / 2;
                SDL_Rect panel = { panel_x, panel_y, panel_w, panel_h };

                // Drop shadow (outside)
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
                SDL_Rect shadow = { panel.x + SCALEX(14), panel.y + SCALEY(14), panel.w, panel.h };
                SDL_RenderFillRect(renderer, &shadow);

                // Black modal panel
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderFillRect(renderer, &panel);

                // Neon green border
                SDL_SetRenderDrawColor(renderer, 80, 255, 100, 255);
                SDL_RenderDrawRect(renderer, &panel);

                // Draw the keyboard inside the modal (no extra green backgrounds)
                kybd_draw(renderer, screen_width, screen_height, kb_text);
            }
        }

        if (ep)  SDL_RenderCopy(renderer, ep,  NULL, &epr);
        if (eb)  SDL_RenderCopy(renderer, eb,  NULL, &ebr);
        if (ep2) SDL_RenderCopy(renderer, ep2, NULL, &ep2r);

        if (xiT) SDL_RenderCopy(renderer, xiT, NULL, &xiR);
        if (stT) SDL_RenderCopy(renderer, stT, NULL, &stR);
        if (ipT) SDL_RenderCopy(renderer, ipT, NULL, &ipR);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

cleanup:
    SDL_CloseAudio();
    if (audio_file) fclose(audio_file);

    // --- RESOURCE CLEANUP ---
    if (titleTex) SDL_DestroyTexture(titleTex);
    if (bgTexture) SDL_DestroyTexture(bgTexture);
    if (ep)  SDL_DestroyTexture(ep);
    if (eb)  SDL_DestroyTexture(eb);
    if (ep2) SDL_DestroyTexture(ep2);
    if (xiT) SDL_DestroyTexture(xiT);
    if (stT) SDL_DestroyTexture(stT);
    if (ipT) SDL_DestroyTexture(ipT);
    if (dcT) SDL_DestroyTexture(dcT);
    if (trT) SDL_DestroyTexture(trT);

    if (font48) TTF_CloseFont(font48);
    if (font24) TTF_CloseFont(font24);
    if (font28) TTF_CloseFont(font28);

    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);

    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return 0;
}
