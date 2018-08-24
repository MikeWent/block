#ifndef _MAIN_H
#define _MAIN_H

struct arguments;
void render_cairo(int is_checking, struct arguments *arguments);

typedef struct {
    int width, height, x, y;
    int scrno;
    xcb_screen_t *scr;
    xcb_connection_t *connection;
    xcb_drawable_t win;
    xcb_visualtype_t *visual_type;

    struct arguments *arguments;

    char hostname;
} Window;
Window window;

#endif
