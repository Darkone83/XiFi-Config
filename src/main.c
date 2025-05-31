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
#include <math.h>
#include <time.h>
#include <nxdk/net.h>
#include "xifi_detect.h"
#include "send_cmd.h"
#include "kybd.h"

// ---- GLOBALS FOR SCREEN SIZE ----
static int screen_width = 1280, screen_height = 720;

#define MUSIC_VOLUME 0.3f

static FILE* audio_file = NULL;

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

// Fill the inside of an octagon with the given color
static void FillOct(SDL_Renderer *r, SDL_Rect rc, int m, SDL_Color c)
{
    int l = rc.x - m, t = rc.y - m;
    int w = rc.w + 2*m, h = rc.h + 2*m;
    SDL_Point pts[8] = {
        {l+m,    t},        // Top-left
        {l+w-m,  t},        // Top-right
        {l+w,    t+m},      // Right-top
        {l+w,    t+h-m},    // Right-bottom
        {l+w-m,  t+h},      // Bottom-right
        {l+m,    t+h},      // Bottom-left
        {l,      t+h-m},    // Left-bottom
        {l,      t+m},      // Left-top
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
#define SCALEX(x) ((int)((float)(x) * screen_width / 1280.0f))
#define SCALEY(y) ((int)((float)(y) * screen_height / 720.0f))
#define SCALEI(i, base) ((int)((float)(i) * screen_height / (float)(base)))

int main(void) {
    // --- PROBE AND SET THE BEST VIDEO MODE ---
    struct { int w, h, mode; } modes[] = {
        {1280, 720, REFRESH_DEFAULT}, // 720p
        {720, 480, REFRESH_DEFAULT},  // 480p
        {640, 480, REFRESH_DEFAULT},  // 480i fallback
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

    // Audio setup
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

    SDL_GameController* controller = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            break;
        }
    }

    SDL_Window* window = SDL_CreateWindow(
        "XiFi Configuration",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        screen_width, screen_height, SDL_WINDOW_SHOWN
    );
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);

    SDL_Surface* bgSurface = IMG_Load("D:\\media\\img\\background.jpg");
    SDL_Texture* bgTexture = bgSurface
        ? SDL_CreateTextureFromSurface(renderer, bgSurface)
        : NULL;
    if (bgSurface) SDL_FreeSurface(bgSurface);

    // SCALED FONT SIZES (relative to height)
    int font48_sz = screen_height/15;   // ~48pt for 720p
    int font24_sz = screen_height/30;   // ~24pt for 720p
    int font28_sz = screen_height/25;   // ~28pt for 720p

    TTF_Font* font48 = TTF_OpenFont("D:\\media\\font\\font.ttf", font48_sz);
    TTF_Font* font24 = TTF_OpenFont("D:\\media\\font\\font.ttf", font24_sz);
    TTF_Font* font28 = TTF_OpenFont("D:\\media\\font\\font.ttf", font28_sz);

    SDL_Texture* titleTex = NULL;
    SDL_Rect titleR = {0};
    if (font48) {
        SDL_Surface* ts = TTF_RenderText_Solid(
            font48, "XiFi Configuration", (SDL_Color){255,255,255,255});
        titleTex = SDL_CreateTextureFromSurface(renderer, ts);
        titleR = (SDL_Rect){ (screen_width - ts->w)/2, SCALEY(50), ts->w, ts->h };
        SDL_FreeSurface(ts);
    }

    SDL_Texture *ep=NULL, *eb=NULL, *ep2=NULL;
    SDL_Rect epr={0}, ebr={0}, ep2r={0};
    if (font24) {
        SDL_Color white={200,200,200,255}, red={255,0,0,255};
        SDL_Surface* s1 = TTF_RenderText_Solid(font24, "Press ", white);
        ep = SDL_CreateTextureFromSurface(renderer, s1);
        epr = (SDL_Rect){0,0,s1->w,s1->h}; SDL_FreeSurface(s1);
        SDL_Surface* s2 = TTF_RenderText_Solid(font24, "B", red);
        eb = SDL_CreateTextureFromSurface(renderer, s2);
        ebr = (SDL_Rect){0,0,s2->w,s2->h}; SDL_FreeSurface(s2);
        SDL_Surface* s3 = TTF_RenderText_Solid(font24, " to exit", white);
        ep2 = SDL_CreateTextureFromSurface(renderer, s3);
        ep2r = (SDL_Rect){0,0,s3->w,s3->h}; SDL_FreeSurface(s3);
        int totalW = epr.w + ebr.w + ep2r.w;
        epr.x = screen_width - totalW - SCALEX(20); epr.y = screen_height - epr.h - SCALEY(20);
        ebr.x = epr.x + epr.w;      ebr.y = epr.y;
        ep2r.x= ebr.x + ebr.w;      ep2r.y= epr.y;
    }

    SDL_Texture *xiT=NULL, *stT=NULL, *ipT=NULL;
    SDL_Rect xiR={SCALEX(20),0,0,0}, stR={0}, ipR={0};
    if (font24) {
        SDL_Surface* sx = TTF_RenderText_Solid(
            font24, "XiFi ", (SDL_Color){255,255,255,255});
        xiT = SDL_CreateTextureFromSurface(renderer, sx);
        xiR = (SDL_Rect){SCALEX(20), screen_height - sx->h - SCALEY(20), sx->w, sx->h};
        SDL_FreeSurface(sx);
    }

    const char* items[7] = {
        "Start XiFi Portal", "Clear WiFi Password", "Turn off OLED",
        "Turn on OLED",      "Set Custom Status",   "Clear Custom Status",
        "About"
    };
    SDL_Rect mrect[7];
    int cx[2] = {screen_width/4, 3*screen_width/4};
    int ry[4] = {SCALEY(200),SCALEY(300),SCALEY(400),SCALEY(500)};
    int cw = SCALEX(400), ch = SCALEY(80);
    for (int i = 0; i < 7; i++) {
        SDL_Surface* ms = TTF_RenderText_Solid(
            font24, items[i], (SDL_Color){255,255,255,255});
        int w = ms->w, h = ms->h; SDL_FreeSurface(ms);
        int col = (i<6 ? i%2 : 0), row = (i<6 ? i/2 : 3);
        int px = (i<6 ? cx[col]-cw/2 : screen_width/2-cw/2);
        int py = ry[row] - ch/2;
        mrect[i] = (SDL_Rect){ px+(cw-w)/2, py+(ch-h)/2, w, h };
    }
    int selected = 0, aboutOpen = 0;
    int kybdOpen = 0;
    char kb_text[33] = {0};

    SDL_Texture *dcT=NULL, *trT=NULL;
    if (font28) {
        SDL_Surface* d = IMG_Load("D:\\media\\img\\DC.png");
        dcT = d ? SDL_CreateTextureFromSurface(renderer, d) : NULL; if(d)SDL_FreeSurface(d);
        SDL_Surface* t = IMG_Load("D:\\media\\img\\TR.png");
        trT = t ? SDL_CreateTextureFromSurface(renderer, t) : NULL; if(t)SDL_FreeSurface(t);
    }

    SDL_Event event;
    while (1) {
        // ---- MAIN EVENT LOOP ----
        while (SDL_PollEvent(&event)) {
            if (kybdOpen) {
                int ret = kybd_handle_event(&event, kb_text, sizeof(kb_text));
                if (ret == KYBD_DONE) {
                    if (XiFi_IsPresent()) {
                        char hexbuf[67] = {0};
                        ascii_to_hex(kb_text, hexbuf, sizeof(hexbuf));
                        send_cmd(XiFi_GetIP(), "0110", hexbuf);
                    }
                    kb_text[0] = 0;
                    kybdOpen = 0;
                } else if (ret == KYBD_CANCELED) {
                    kb_text[0] = 0;
                    kybdOpen = 0;
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
                                    if (xifiPresent) { kb_text[0]=0; kybdOpen=1; }
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
                            selected = (selected + 5) % 7;
                            if (selected == 6) selected = 4;
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                            if (selected < 4) selected += 2;
                            else if (selected < 6) selected = 6;
                            else selected %= 2;
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                            if (selected % 2) selected--;
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                            if (selected % 2 == 0 && selected < 5) selected++;
                            break;
                    }
                }
            }
        }

        // ---- FAST KEYBOARD REPEAT FOR ONSCREEN KEYBOARD ----
        if (kybdOpen) kybd_update_repeat();

        // -- Render status and IP textures --
        if (stT) SDL_DestroyTexture(stT);
        if (ipT) SDL_DestroyTexture(ipT);
        const char* statusText = XiFi_IsPresent() ? "Detected" : "Not Detected";
        SDL_Color   statusCol  = XiFi_IsPresent()
                                 ? (SDL_Color){0,255,0,255}
                                 : (SDL_Color){255,0,0,255};
        SDL_Surface* ss = TTF_RenderText_Solid(font24, statusText, statusCol);
        stT = SDL_CreateTextureFromSurface(renderer, ss);
        stR = (SDL_Rect){ xiR.x + xiR.w, xiR.y, ss->w, ss->h };
        SDL_FreeSurface(ss);

        const char* show_ip = XiFi_IsPresent() ? XiFi_GetIP() : "";
        if (show_ip[0]) {
            SDL_Surface* ips = TTF_RenderText_Solid(font24, show_ip, (SDL_Color){255,255,255,255});
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

        if (aboutOpen) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
            SDL_Rect ov={SCALEX(240),SCALEY(140),SCALEX(800),SCALEY(440)};
            SDL_RenderFillRect(renderer, &ov);

            const char* lines[] = {
                "XiFi Config", "", "Code by:", "Darkone83", "",
                "Music By:", "Darkone83"
            };
            for (int i = 0; i < 7; i++) {
                if (lines[i][0]) {
                    SDL_Surface* ls = TTF_RenderText_Solid(
                        font28, lines[i], (SDL_Color){255,255,255,255});
                    SDL_Texture* lt = SDL_CreateTextureFromSurface(renderer, ls);
                    SDL_Rect dr = {
                        (screen_width - ls->w)/2,
                        SCALEY(160) + i*SCALEY(40),
                        ls->w, ls->h
                    };
                    SDL_FreeSurface(ls);
                    SDL_RenderCopy(renderer, lt, NULL, &dr);
                    SDL_DestroyTexture(lt);
                }
            }
            int sz = SCALEY(64);
            int startX = (screen_width - (sz + SCALEX(20) + sz)) / 2;
            int logoY = SCALEY(140) + 7*SCALEY(40) + SCALEY(20);
            SDL_Rect r1 = {startX, logoY, sz, sz};
            SDL_Rect r2 = {startX + sz + SCALEX(20), logoY, sz, sz};
            if (dcT) SDL_RenderCopy(renderer, dcT, NULL, &r1);
            if (trT) SDL_RenderCopy(renderer, trT, NULL, &r2);

        } else if (kybdOpen) {
            kybd_draw(renderer, screen_width, screen_height, kb_text);

        } else {
            // --- Menu highlight effect: filled octagon, drop shadow, oct border ---
            bool xifiPresent = XiFi_IsPresent();
            for (int i = 0; i < 7; i++) {
                SDL_Rect rect = mrect[i];
                SDL_Rect shadow = rect;
                shadow.x += SCALEX(8); shadow.y += SCALEY(8); // Drop shadow offset

                // Draw drop shadow (soft black, semi-transparent)
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
                SDL_Surface* ms = TTF_RenderText_Solid(
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
            if (ep)  SDL_RenderCopy(renderer, ep,  NULL, &epr);
            if (eb)  SDL_RenderCopy(renderer, eb,  NULL, &ebr);
            if (ep2) SDL_RenderCopy(renderer, ep2, NULL, &ep2r);

            if (xiT) SDL_RenderCopy(renderer, xiT, NULL, &xiR);
            if (stT) SDL_RenderCopy(renderer, stT, NULL, &stR);
            if (ipT) SDL_RenderCopy(renderer, ipT, NULL, &ipR);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

cleanup:
    SDL_CloseAudio();
    if (audio_file) fclose(audio_file);
    return 0;
}
