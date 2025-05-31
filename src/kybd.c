#include "kybd.h"
#include "kb_data.h"
#include <SDL.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

// --- Scaling macros for window size ---
#define SCALEX(x) ((int)((float)(x) * win_w / 1280.0f))
#define SCALEY(y) ((int)((float)(y) * win_h / 720.0f))

// Bit reversal lookup for all 256 bytes
static const uint8_t bit_reverse_table[256] = {
  0x00,0x80,0x40,0xC0,0x20,0xA0,0x60,0xE0,0x10,0x90,0x50,0xD0,0x30,0xB0,0x70,0xF0,
  0x08,0x88,0x48,0xC8,0x28,0xA8,0x68,0xE8,0x18,0x98,0x58,0xD8,0x38,0xB8,0x78,0xF8,
  0x04,0x84,0x44,0xC4,0x24,0xA4,0x64,0xE4,0x14,0x94,0x54,0xD4,0x34,0xB4,0x74,0xF4,
  0x0C,0x8C,0x4C,0xCC,0x2C,0xAC,0x6C,0xEC,0x1C,0x9C,0x5C,0xDC,0x3C,0xBC,0x7C,0xFC,
  0x02,0x82,0x42,0xC2,0x22,0xA2,0x62,0xE2,0x12,0x92,0x52,0xD2,0x32,0xB2,0x72,0xF2,
  0x0A,0x8A,0x4A,0xCA,0x2A,0xAA,0x6A,0xEA,0x1A,0x9A,0x5A,0xDA,0x3A,0xBA,0x7A,0xFA,
  0x06,0x86,0x46,0xC6,0x26,0xA6,0x66,0xE6,0x16,0x96,0x56,0xD6,0x36,0xB6,0x76,0xF6,
  0x0E,0x8E,0x4E,0xCE,0x2E,0xAE,0x6E,0xEE,0x1E,0x9E,0x5E,0xDE,0x3E,0xBE,0x7E,0xFE,
  0x01,0x81,0x41,0xC1,0x21,0xA1,0x61,0xE1,0x11,0x91,0x51,0xD1,0x31,0xB1,0x71,0xF1,
  0x09,0x89,0x49,0xC9,0x29,0xA9,0x69,0xE9,0x19,0x99,0x59,0xD9,0x39,0xB9,0x79,0xF9,
  0x05,0x85,0x45,0xC5,0x25,0xA5,0x65,0xE5,0x15,0x95,0x55,0xD5,0x35,0xB5,0x75,0xF5,
  0x0D,0x8D,0x4D,0xCD,0x2D,0xAD,0x6D,0xED,0x1D,0x9D,0x5D,0xDD,0x3D,0xBD,0x7D,0xFD,
  0x03,0x83,0x43,0xC3,0x23,0xA3,0x63,0xE3,0x13,0x93,0x53,0xD3,0x33,0xB3,0x73,0xF3,
  0x0B,0x8B,0x4B,0xCB,0x2B,0xAB,0x6B,0xEB,0x1B,0x9B,0x5B,0xDB,0x3B,0xBB,0x7B,0xFB,
  0x07,0x87,0x47,0xC7,0x27,0xA7,0x67,0xE7,0x17,0x97,0x57,0xD7,0x37,0xB7,0x77,0xF7,
  0x0F,0x8F,0x4F,0xCF,0x2F,0xAF,0x6F,0xEF,0x1F,0x9F,0x5F,0xDF,0x3F,0xBF,0x7F,0xFF
};

static const char* kb_layouts[3][5] = {
    // UPPER
    { "1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM", "<- SPACE SHIFT DONE" },
    // lower
    { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm", "<- space shift done" },
    // symbols
    { "!@#$%^&*()", "[]{}\\|;:'\",.", "<>/?`~_+-=", "", "<- space shift done" }
};

#define KB_NUM_ROWS 5
#define KB_LASTROW_KEYS 4

static int kb_row = 0, kb_col = 0, kb_layout = 0;
static char kb_buffer[33] = {0};
static int kb_cpos = 0; // Cursor in buffer
static int kb_result = KYBD_RUNNING;

// Key repeat
static int repeat_dir = 0;
static uint32_t repeat_start = 0, repeat_last = 0;
#define REPEAT_DELAY 250
#define REPEAT_RATE  50

void kybd_update_repeat(void) {
    uint32_t now = SDL_GetTicks();
    if (repeat_dir != 0) {
        if (now - repeat_start > REPEAT_DELAY && now - repeat_last > REPEAT_RATE) {
            repeat_last = now;
            switch (repeat_dir) {
                case 1: kb_row = (kb_row - 1 + KB_NUM_ROWS) % KB_NUM_ROWS; break;
                case 2: kb_row = (kb_row + 1) % KB_NUM_ROWS; break;
                case 3: {
                    int len = (kb_row == KB_NUM_ROWS - 1) ? KB_LASTROW_KEYS : strlen(kb_layouts[kb_layout][kb_row]);
                    kb_col = (kb_col - 1 + len) % len;
                } break;
                case 4: {
                    int len = (kb_row == KB_NUM_ROWS - 1) ? KB_LASTROW_KEYS : strlen(kb_layouts[kb_layout][kb_row]);
                    kb_col = (kb_col + 1) % len;
                } break;
            }
        }
    }
}

void kybd_init(char* init_text, int buflen) {
    memset(kb_buffer, 0, sizeof(kb_buffer));
    if (init_text && buflen > 0) {
        strncpy(kb_buffer, init_text, sizeof(kb_buffer) - 1);
        kb_buffer[sizeof(kb_buffer)-1] = 0;
    }
    kb_row = 0; kb_col = 0; kb_layout = 0;
    kb_cpos = strlen(kb_buffer);
    kb_result = KYBD_RUNNING;
    repeat_dir = 0;
}

static void insert_char_at_cursor(char ch) {
    int len = strlen(kb_buffer);
    if (len < 32 && kb_cpos <= len) {
        memmove(&kb_buffer[kb_cpos + 1], &kb_buffer[kb_cpos], len - kb_cpos + 1);
        kb_buffer[kb_cpos] = ch;
        kb_cpos++;
    }
}

static void delete_char_before_cursor() {
    if (kb_cpos > 0) {
        int len = strlen(kb_buffer);
        memmove(&kb_buffer[kb_cpos - 1], &kb_buffer[kb_cpos], len - kb_cpos + 1);
        kb_cpos--;
    }
}

static void move_cursor_left() {
    if (kb_cpos > 0) kb_cpos--;
}

static void move_cursor_right() {
    int len = strlen(kb_buffer);
    if (kb_cpos < len) kb_cpos++;
}

void draw_ascii_char(SDL_Renderer* r, char c, int x, int y, SDL_Color fg) {
    if (c < 0x20 || c > 0x7F) c = '?';
    const unsigned char* glyph = font8x8_basic[(unsigned char)c - 0x20];
    SDL_SetRenderDrawColor(r, fg.r, fg.g, fg.b, fg.a);
    for (int row = 0; row < 8; ++row) {
        uint8_t bits = bit_reverse_table[glyph[row]];
        for (int col = 0; col < 8; ++col) {
            if ((bits >> (7 - col)) & 1) {
                SDL_RenderDrawPoint(r, x + col, y + row);
            }
        }
    }
}

void draw_ascii_text(SDL_Renderer* r, const char* s, int x, int y, SDL_Color fg) {
    while (*s) {
        draw_ascii_char(r, *s, x, y, fg);
        x += 8;
        ++s;
    }
}

int kybd_handle_event(const SDL_Event* event, char* out, int outlen) {
    if (kb_result != KYBD_RUNNING) return 1;

    if (event->type == SDL_CONTROLLERBUTTONDOWN) {
        int but = event->cbutton.button;
        int len = (kb_row == KB_NUM_ROWS - 1) ? KB_LASTROW_KEYS : strlen(kb_layouts[kb_layout][kb_row]);
        switch (but) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                kb_row = (kb_row - 1 + KB_NUM_ROWS) % KB_NUM_ROWS;
                repeat_dir = 1; repeat_start = repeat_last = SDL_GetTicks();
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                kb_row = (kb_row + 1) % KB_NUM_ROWS;
                repeat_dir = 2; repeat_start = repeat_last = SDL_GetTicks();
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                kb_col = (kb_col - 1 + len) % len;
                repeat_dir = 3; repeat_start = repeat_last = SDL_GetTicks();
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                kb_col = (kb_col + 1) % len;
                repeat_dir = 4; repeat_start = repeat_last = SDL_GetTicks();
                break;
            case SDL_CONTROLLER_BUTTON_A:
                if (kb_row == KB_NUM_ROWS - 1) {
                    if (kb_col == 0) { // "<-"
                        delete_char_before_cursor();
                    } else if (kb_col == 1) { // SPACE
                        insert_char_at_cursor(' ');
                    } else if (kb_col == 2) { // SHIFT
                        kb_layout = (kb_layout + 1) % 3;
                        int max = (kb_row == KB_NUM_ROWS - 1) ? KB_LASTROW_KEYS : strlen(kb_layouts[kb_layout][kb_row]);
                        if (kb_col >= max) kb_col = max - 1;
                    } else if (kb_col == 3) { // DONE
                        if (out && outlen > 0)
                            strncpy(out, kb_buffer, outlen-1);
                        kb_result = KYBD_DONE;
                        repeat_dir = 0;
                        return KYBD_DONE;
                    }
                } else {
                    char ch = kb_layouts[kb_layout][kb_row][kb_col];
                    insert_char_at_cursor(ch);
                }
                break;
            case SDL_CONTROLLER_BUTTON_B:
                kb_result = KYBD_CANCELED;
                repeat_dir = 0;
                return KYBD_CANCELED;
            case SDL_CONTROLLER_BUTTON_X:
                delete_char_before_cursor();
                break;
            case SDL_CONTROLLER_BUTTON_Y:
                insert_char_at_cursor(' ');
                break;
            default:
                break;
        }
    }
    else if (event->type == SDL_CONTROLLERBUTTONUP) {
        int but = event->cbutton.button;
        if (but == SDL_CONTROLLER_BUTTON_DPAD_UP || but == SDL_CONTROLLER_BUTTON_DPAD_DOWN ||
            but == SDL_CONTROLLER_BUTTON_DPAD_LEFT || but == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            repeat_dir = 0;
        }
    }
    else if (event->type == SDL_CONTROLLERAXISMOTION) {
        static int left_trigger_pressed = 0, right_trigger_pressed = 0;
        if (event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            if (event->caxis.value > 16000 && !left_trigger_pressed) {
                left_trigger_pressed = 1;
                move_cursor_left();
            } else if (event->caxis.value < 8000 && left_trigger_pressed) {
                left_trigger_pressed = 0;
            }
        }
        else if (event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
            if (event->caxis.value > 16000 && !right_trigger_pressed) {
                right_trigger_pressed = 1;
                move_cursor_right();
            } else if (event->caxis.value < 8000 && right_trigger_pressed) {
                right_trigger_pressed = 0;
            }
        }
    }
    return 0;
}

void kybd_draw(SDL_Renderer* renderer, int win_w, int win_h, const char* textbuff) {
    // --- PATCHED FOR SCALING ---
    int key_w = SCALEX(64), key_h = SCALEY(32), spacing = SCALEX(10);
    int max_cols = 0;
    for (int row = 0; row < KB_NUM_ROWS; ++row) {
        int len = (row == KB_NUM_ROWS-1) ? KB_LASTROW_KEYS : strlen(kb_layouts[kb_layout][row]);
        if (len > max_cols) max_cols = len;
    }
    int grid_w = max_cols * key_w + (max_cols - 1) * spacing;
    int grid_h = KB_NUM_ROWS * key_h + (KB_NUM_ROWS - 1) * spacing;

    int overlay_x = win_w / 2 - grid_w / 2 - SCALEX(40);
    int overlay_y = win_h / 2 - grid_h / 2 - SCALEY(70);
    int overlay_w = grid_w + SCALEX(80);
    int overlay_h = grid_h + SCALEY(160);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 120);
    SDL_Rect shadow = { overlay_x + SCALEX(16), overlay_y + SCALEY(16), overlay_w, overlay_h };
    SDL_RenderFillRect(renderer, &shadow);

    SDL_SetRenderDrawColor(renderer, 0, 40, 0, 220);
    SDL_Rect bg = { overlay_x, overlay_y, overlay_w, overlay_h };
    SDL_RenderFillRect(renderer, &bg);

    SDL_Color fg = {255,255,255,255};
    char dispbuf[40];
    int blen = strlen(kb_buffer);
    int cpos = (kb_cpos > blen) ? blen : kb_cpos;
    int i=0, j=0;
    for (; i < cpos && i < 32; ++i) dispbuf[j++] = kb_buffer[i];
    dispbuf[j++] = '|';
    for (; i < blen && j < 39; ++i) dispbuf[j++] = kb_buffer[i];
    dispbuf[j] = 0;

    draw_ascii_text(renderer, dispbuf, bg.x + SCALEX(40), bg.y + SCALEY(30), fg);

    int grid_start_x = bg.x + (bg.w - grid_w) / 2;
    int grid_start_y = bg.y + SCALEY(90);

    for (int row = 0; row < KB_NUM_ROWS; row++) {
        int len = (row == KB_NUM_ROWS-1) ? KB_LASTROW_KEYS : strlen(kb_layouts[kb_layout][row]);
        int y = grid_start_y + row * (key_h + spacing);
        int row_start_x = grid_start_x + (grid_w - (len * key_w + (len - 1) * spacing)) / 2;
        for (int col = 0; col < len; col++) {
            int x = row_start_x + col * (key_w + spacing);
            SDL_Rect kr = { x, y, key_w, key_h };
            if (row == kb_row && col == kb_col) {
                SDL_SetRenderDrawColor(renderer, 0, 220, 0, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 36, 36, 36, 255);
            }
            SDL_RenderFillRect(renderer, &kr);
            SDL_SetRenderDrawColor(renderer, 80, 255, 100, 255);
            SDL_RenderDrawRect(renderer, &kr);

            char key[16] = {0};
            if (row == KB_NUM_ROWS - 1) {
                if (col == 0) strcpy(key, "<-");
                else if (col == 1) strcpy(key, "SPACE");
                else if (col == 2) {
                    if (kb_layout == 0) strcpy(key, "abc");
                    else if (kb_layout == 1) strcpy(key, "#!?");
                    else strcpy(key, "ABC");
                }
                else if (col == 3) strcpy(key, "DONE");
            } else {
                key[0] = kb_layouts[kb_layout][row][col];
                key[1] = 0;
            }
            int tx = kr.x + (kr.w - 8 * strlen(key)) / 2;
            int ty = kr.y + (kr.h - 8) / 2;
            draw_ascii_text(renderer, key, tx, ty, fg);
        }
    }
}

int kybd_get_result(void) { return kb_result; }
const char* kybd_get_buffer(void) { return kb_buffer; }
