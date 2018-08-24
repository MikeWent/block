#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <xcb/xcb.h>
#include <xcb/randr.h>

#include "randr.h"
#include "main.h"


int xr_screens = 0;
Rect *xr_resolutions = NULL;
static int has_randr = 0;
static int has_randr_1_5 = 0;


void randr_init(int *event_base, xcb_window_t root) {
    const xcb_query_extension_reply_t *extreply;

    extreply = xcb_get_extension_data(window.connection, &xcb_randr_id);
    if (!extreply->present) {
        return;
    }

    xcb_generic_error_t *err;
    xcb_randr_query_version_reply_t *randr_version =
        xcb_randr_query_version_reply(
            window.connection, xcb_randr_query_version(window.connection, XCB_RANDR_MAJOR_VERSION, XCB_RANDR_MINOR_VERSION), &err);
    if (err != NULL) {
        return;
    }

    has_randr = 1;
    has_randr_1_5 = (randr_version->major_version >= 1) &&
                    (randr_version->minor_version >= 5);

    free(randr_version);

    if (event_base != NULL)
        *event_base = extreply->first_event;

    xcb_randr_select_input(window.connection, root,
                           XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE |
                               XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE |
                               XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE |
                               XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY);

    xcb_flush(window.connection);
}

int randr_query(xcb_window_t root) {
    if(XCB_RANDR_MINOR_VERSION < 5) return 0;

    if (!has_randr_1_5) return 0;

    xcb_generic_error_t *err;
    xcb_randr_get_monitors_reply_t *monitors =
        xcb_randr_get_monitors_reply(
            window.connection, xcb_randr_get_monitors(window.connection, root, 1), &err);
    if (err != NULL) {
        free(err); return 0;
    }

    int screens = xcb_randr_get_monitors_monitors_length(monitors);

    Rect *resolutions = malloc(screens * sizeof(Rect));
    if (!resolutions) {
        free(monitors); return 0;
    }

    xcb_randr_monitor_info_iterator_t iter;
    int screen;
    for (iter = xcb_randr_get_monitors_monitors_iterator(monitors), screen = 0;
            iter.rem;
            xcb_randr_monitor_info_next(&iter), screen++) {
        const xcb_randr_monitor_info_t *monitor_info = iter.data;

        resolutions[screen].x = monitor_info->x;
        resolutions[screen].y = monitor_info->y;
        resolutions[screen].width = monitor_info->width;
        resolutions[screen].height = monitor_info->height;
    }
    free(xr_resolutions);
    xr_resolutions = resolutions;
    xr_screens = screens;

    free(monitors);
    return 1;
}
