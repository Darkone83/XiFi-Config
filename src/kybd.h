#ifndef KYBD_H
#define KYBD_H

#include <SDL.h>

#define KYBD_DONE     1
#define KYBD_CANCELED 2
#define KYBD_PENDING  0
#define KYBD_RUNNING  0

void kybd_init(char* textbuf, int buflen);
int kybd_handle_event(const SDL_Event* event, char* textbuf, int buflen);
void kybd_draw(SDL_Renderer* renderer, int win_w, int win_h, const char* textbuf);

int kybd_get_result(void);
const char* kybd_get_buffer(void);

#endif
