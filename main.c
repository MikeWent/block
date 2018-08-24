#include <security/pam_appl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <argp.h>
#include <math.h>
#include <time.h>
#include <pwd.h>
#include <ev.h>

#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_image.h>
#include <xcb/xkb.h>
#include <cairo.h>
#include <cairo-xcb.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-x11.h>

#include <main.h>
#include <randr.h>


struct colour {
    double r, g, b, a;
};

const char *argp_program_version = "BotterLock 0.0.1";
const char *argp_program_bug_address = "<bottersnike237@gmail.com>";
static char doc[] = "Basic screen locker.";
static char args_doc[] = "<PNG image>";
static struct argp_option options[] = {
    { "cover",   'c', 0, 0, "Cover with image" },
    { "fit",     'f', 0, 0, "Fit image" },
    { "centre",  'C', 0, 0, "Centre image" },
    { "grow",    'g', 0, 0, "Grow image when needed (otherwise, centre)" },

    { "radius",  'r', "px", 0, "Radius of the lock dots" },
    { "pad",     'p', "px", 0, "Padding between the lock dots" },

    { 0,         'x', "px", 0, "X position of the lock dots" },
    { 0,         'y', "px", 0, "Y position of the lock dots" },

    { "colour",  'd', "hex", 0, "Hex colour for lock dots" },
    { "colour2", 'l', "hex", 0, "Hex colour for lock dots while checking" },

    { "format",  'F', "format", 0, "Clock format" },
    { "font",    'a', "font",   0, "Clock font face" },
    { "size",    's', "pt",     0, "Clock font size" },
    { "colour3", 't', "hex",    0, "Clock colour" },

    { "shadow",  'S', "px", 0, "Lock dots shadow size" },

    { 0,         'X', "px", 0, "Clock X" },
    { 0,         'Y', "px", 0, "Clock Y" },

    { "no-full",  3, 0, 0, "Stretch a single image over all monitors" },
    { "extend",   4, 0, 0, "Extend dots while checking" },

    { 0 }
};
struct arguments {
    enum {MODE_COVER, MODE_FIT, MODE_CENTRE, MODE_GROW} fill_mode;
    char *image;
    int pad, rad, x, y, x_set, y_set, extend, X, Y, X_set, Y_set, shad, full;
    double font_size;
    char *format;
    char *font_face;
    struct colour dots, ldots, clk;
};

const int CHECK_SIZE = 12;

const char *name = "Screen Locked";
const char *class[2] = {
    "lock", "BLock"
};

static struct xkb_state *xkb_state;
static struct xkb_context *xkb_context;
static struct xkb_keymap *xkb_keymap;
static struct xkb_compose_table *xkb_compose_table;
static struct xkb_compose_state *xkb_compose_state;
static uint8_t xkb_base_event;
static uint8_t xkb_base_error;
struct pam_response *reply;

struct ev_loop *main_loop;

xcb_visualtype_iterator_t visual_iter;
xcb_depth_iterator_t depth_iter;
cairo_surface_t* scaled_image;
xcb_atom_t wm_delete_window;
cairo_surface_t* image;

int skip_repeated_empty_password = 0;
static char password_str[512];
static int randr_base = -1;
int input_position = 0;
int do_redraw = 1;
char *timetext;
char *username;


#define isutf(c) (((c)&0xC0) != 0x80)
void u8_dec(char *s, int *i) {
    (void)(isutf(s[--(*i)]) || isutf(s[--(*i)]) || isutf(s[--(*i)]) || --(*i));
}

struct colour parse_hex(char *arg) {
    if (arg[0] == '#') {
        arg++;
    }

    int r, g, b, a;
    struct colour new_colour;
    if (strlen(arg) == 6) {
        if (sscanf(arg, "%02x%02x%02x", &r, &g, &b)) {
            new_colour.r = r / 255.0; new_colour.g = g / 255.0;
            new_colour.b = b / 255.0; new_colour.a = 1.0;
            return new_colour;
        }
    }
    if (strlen(arg) == 8) {
        if (sscanf(arg, "%02x%02x%02x%02x", &r, &g, &b, &a)) {
            new_colour.r = r / 255.0; new_colour.g = g / 255.0;
            new_colour.b = b / 255.0; new_colour.a = a / 255.0;
            return new_colour;
        }
    }
    fprintf(stderr, "Invalid HEX colour: %s\n", arg);
    exit(1);
}

/* --- */

int function_conversation(int num_msg, const struct pam_message **msg, struct pam_response **resp, void *appdata_ptr) {
    *resp = reply;
    return PAM_SUCCESS;
}

int authenticate_system(const char *username, const char *password, int quiet) {
    const struct pam_conv local_conversation = { function_conversation, NULL };
    pam_handle_t *local_auth_handle = NULL;

    int retval;
    retval = pam_start("block", username, &local_conversation, &local_auth_handle);

    if (retval != PAM_SUCCESS) {
        if (!quiet)
            printf("main: pam_start: %s\n", pam_strerror(local_auth_handle, retval));
        return 0;
    }

    reply = (struct pam_response *)malloc(sizeof(struct pam_response));

    reply[0].resp = strdup(password);
    reply[0].resp_retcode = 0;
    retval = pam_authenticate(local_auth_handle, 0);

    switch (retval) {
        case PAM_SUCCESS:
            break;
        default:
            if (!quiet)
                fprintf(stderr, "%s\n", pam_strerror(local_auth_handle, retval));
            free(reply); return 0;
    }

    retval = pam_end(local_auth_handle, retval);
    if (retval != PAM_SUCCESS) {
        if (!quiet)
            fprintf(stderr, "main: pam_end: %s\n", pam_strerror(local_auth_handle, retval));
        free(reply); return 0;
    }
    free(reply); return 1;
}

/* --- */

static int load_keymap(void) {
    if (xkb_context == NULL) {
        if ((xkb_context = xkb_context_new(0)) == NULL) {
            fprintf(stderr, "Could not create xkbcommon ctx!\n");
            return 0;
        }
    }

    xkb_keymap_unref(xkb_keymap);

    int32_t device_id = xkb_x11_get_core_keyboard_device_id(window.connection);
    if ((xkb_keymap = xkb_x11_keymap_new_from_device(xkb_context, window.connection, device_id, 0)) == NULL) {
        fprintf(stderr, "xkb_x11_keymap_new_from_device failed!\n");
        return 0;
    }

    struct xkb_state *new_state = xkb_x11_state_new_from_device(xkb_keymap, window.connection, device_id);
    if (new_state == NULL) {
        fprintf(stderr, "xkb_x11_state_new_from_device failed!\n");
        return 0;
    }

    xkb_state_unref(xkb_state);
    xkb_state = new_state;

    return 1;
}

static int load_compose_table() {
    const char *locale = getenv("LC_ALL");
    if (!locale || !*locale) locale = getenv("LC_CTYPE");
    if (!locale || !*locale) locale = getenv("LANG");
    if (!locale || !*locale) locale = "C";

    xkb_compose_table_unref(xkb_compose_table);

    if ((xkb_compose_table = xkb_compose_table_new_from_locale(xkb_context, locale, 0)) == NULL) {
        fprintf(stderr, "xkb_compose_table_new_from_locale failed\n");
        return 0;
    }

    struct xkb_compose_state *new_compose_state = xkb_compose_state_new(xkb_compose_table, 0);
    if (new_compose_state == NULL) {
        fprintf(stderr, "xkb_compose_state_new failed\n");
        return 0;
    }

    xkb_compose_state_unref(xkb_compose_state);
    xkb_compose_state = new_compose_state;

    return 1;
}

void clear_input() {
    input_position = 0;
    do_redraw = 1;
    memset(password_str, '\0', sizeof(password_str));
}

void finish_input(struct arguments *arguments) {
    password_str[input_position] = '\0';

    render_cairo(1, arguments);
    if (authenticate_system(username, password_str, 1)) exit(0);

    clear_input();
}

/* --- */

void handle_key_press(xcb_button_press_event_t* event, struct arguments *arguments) {
    xkb_keysym_t ksym;
    char buffer[128];
    int n, ctrl, composed = 0;

    ksym = xkb_state_key_get_one_sym(xkb_state, event->detail);
    ctrl = xkb_state_mod_name_is_active(xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_DEPRESSED);

    memset(buffer, '\0', sizeof(buffer));

    if (xkb_compose_state && xkb_compose_state_feed(xkb_compose_state, ksym) == XKB_COMPOSE_FEED_ACCEPTED) {
        switch (xkb_compose_state_get_status(xkb_compose_state)) {
            case XKB_COMPOSE_NOTHING:
                break;
            case XKB_COMPOSE_COMPOSING:
                return;
            case XKB_COMPOSE_COMPOSED:
                n = xkb_compose_state_get_utf8(xkb_compose_state, buffer, sizeof(buffer)) + 1;
                ksym = xkb_compose_state_get_one_sym(xkb_compose_state);
                composed = 1;
                break;
            case XKB_COMPOSE_CANCELLED:
                xkb_compose_state_reset(xkb_compose_state);
                return;
        }
    }

    if (!composed) n = xkb_keysym_to_utf8(ksym, buffer, sizeof(buffer));

    switch (ksym) {
        case XKB_KEY_j:
        case XKB_KEY_m:
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
        case XKB_KEY_XF86ScreenSaver:
            if ((ksym == XKB_KEY_j || ksym == XKB_KEY_m) && !ctrl) break;

            if (skip_repeated_empty_password && input_position != 0) {
                clear_input();
                return;
            }

            finish_input(arguments);
            skip_repeated_empty_password = 1;
            do_redraw = 1;
            return;
        default:
            skip_repeated_empty_password = 0;
    }

    switch (ksym) {
        case XKB_KEY_Escape:
            clear_input();
            return;
        case XKB_KEY_Delete:
        case XKB_KEY_KP_Delete:
            return;
        case XKB_KEY_BackSpace:
            do_redraw = 1;
            if (input_position == 0) return;

            u8_dec(password_str, &input_position);
            password_str[input_position] = '\0';
            return;
    }

    if ((input_position + 8) >= (int)sizeof(password_str)) return;
    if (n < 2) return;

    memcpy(password_str + input_position, buffer, n - 1);
    input_position += n - 1;
    do_redraw = 1;

    return;
}

void handle_visibility_notify(xcb_connection_t *conn, xcb_visibility_notify_event_t *event) {
    if (event->state != XCB_VISIBILITY_UNOBSCURED) {
        uint32_t values[] = {XCB_STACK_MODE_ABOVE};
        xcb_configure_window(conn, event->window, XCB_CONFIG_WINDOW_STACK_MODE, values);
        xcb_flush(conn);
    }
}

void process_xkb_event(xcb_generic_event_t *gevent) {
    union xkb_event {
        struct {
            uint8_t response_type;
            uint8_t xkbType;
            uint16_t sequence;
            xcb_timestamp_t time;
            uint8_t deviceID;
        } any;
        xcb_xkb_new_keyboard_notify_event_t new_keyboard_notify;
        xcb_xkb_map_notify_event_t map_notify;
        xcb_xkb_state_notify_event_t state_notify;
    } *event = (union xkb_event *)gevent;

    if (event->any.deviceID != xkb_x11_get_core_keyboard_device_id(window.connection)) return;

    switch (event->any.xkbType) {
        case XCB_XKB_NEW_KEYBOARD_NOTIFY:
            if (event->new_keyboard_notify.changed & XCB_XKB_NKN_DETAIL_KEYCODES)
                (void)load_keymap();
            break;
        case XCB_XKB_MAP_NOTIFY:
            (void)load_keymap();
            break;
        case XCB_XKB_STATE_NOTIFY:
            xkb_state_update_mask(xkb_state,
                event->state_notify.baseMods,
                event->state_notify.latchedMods,
                event->state_notify.lockedMods,
                event->state_notify.baseGroup,
                event->state_notify.latchedGroup,
                event->state_notify.lockedGroup);
            break;
    }
}

/* --- */

void set_focused_window(const xcb_window_t to_window) {
    xcb_client_message_event_t ev;
    memset(&ev, '\0', sizeof(xcb_client_message_event_t));

    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(
        window.connection,
        xcb_intern_atom(window.connection, 0, strlen("_NET_ACTIVE_WINDOW"),
        "_NET_ACTIVE_WINDOW"), 0);
    xcb_atom_t _NET_ACTIVE_WINDOW = reply->atom;

    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.window = to_window;
    ev.type = _NET_ACTIVE_WINDOW;
    ev.format = 32;
    ev.data.data32[0] = 2;

    xcb_send_event(window.connection, 0, window.scr->root,
                   XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (char *)&ev);
    xcb_flush(window.connection);
}

xcb_window_t get_focused_window() {
    xcb_window_t result = XCB_NONE;

    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(
        window.connection,
        xcb_intern_atom(window.connection, 0, strlen("_NET_ACTIVE_WINDOW"), "_NET_ACTIVE_WINDOW"),
        0);
    xcb_atom_t _NET_ACTIVE_WINDOW = reply->atom;

    xcb_get_property_reply_t *prop_reply = xcb_get_property_reply(
        window.connection, xcb_get_property_unchecked(
            window.connection, 0, window.scr->root, _NET_ACTIVE_WINDOW,
            XCB_GET_PROPERTY_TYPE_ANY, 0, 1
        ), NULL
    );

    if (prop_reply == NULL) goto out;
    if (xcb_get_property_value_length(prop_reply) == 0) goto out_prop;
    if (prop_reply->type != XCB_ATOM_WINDOW) goto out_prop;

    result = *((xcb_window_t *)xcb_get_property_value(prop_reply));

    out_prop:
        free(prop_reply);
    out:
        return result;
}

void take_focus() {
    xcb_set_input_focus(window.connection, XCB_INPUT_FOCUS_PARENT,
                        window.win, XCB_CURRENT_TIME);
}

void set_wm_flags() {
    gethostname(&window.hostname, 1023);

    /* Listen for delete window */
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(window.connection, 1, 12, "WM_PROTOCOLS");
    xcb_intern_atom_cookie_t cookie2 = xcb_intern_atom(window.connection, 0, 16, "WM_DELETE_WINDOW");
    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(window.connection, cookie, 0);
    wm_delete_window  = (*xcb_intern_atom_reply(window.connection, cookie2, 0)).atom;

    xcb_change_property(window.connection, XCB_PROP_MODE_REPLACE, window.win, (*reply).atom, 4, 32, 1, &wm_delete_window);

    /* Put us on top */
    uint32_t values[1] = {XCB_STACK_MODE_ABOVE};
    xcb_configure_window(window.connection, window.win, XCB_CONFIG_WINDOW_STACK_MODE, values);

    /* Construct and set the class */
    int class_l = strlen(class[0]) + strlen(class[1]) + 1;
    char class_s[class_l];
    for (int i = 0; i < strlen(class[0]); i++)
        class_s[i] = class[0][i];
    for (int i = 0; i < strlen(class[1]); i++)
        class_s[i + strlen(class[0]) + 1] = class[1][i];

    xcb_change_property(window.connection, XCB_PROP_MODE_REPLACE, window.win, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, class_l, &class_s);

    /* Title and hostname */
    xcb_change_property(window.connection, XCB_PROP_MODE_REPLACE, window.win, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, strlen(name), name);
    xcb_icccm_set_wm_client_machine(window.connection, window.win, XCB_ATOM_STRING, 8, strlen(&window.hostname), &window.hostname);
}

int grab_p_and_k(xcb_cursor_t cursor, int tries) {
    xcb_grab_pointer_cookie_t pcookie;
    xcb_grab_pointer_reply_t *preply;

    xcb_grab_keyboard_cookie_t kcookie;
    xcb_grab_keyboard_reply_t *kreply;

    while (tries-- > 0) {
        pcookie = xcb_grab_pointer(
            window.connection, 0, window.scr->root,
            XCB_NONE, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
            XCB_NONE, cursor, XCB_CURRENT_TIME
        );

        if ((preply = xcb_grab_pointer_reply(window.connection, pcookie, NULL)) &&
            preply->status == XCB_GRAB_STATUS_SUCCESS) {
                free(preply);
                break;
        }
        free(preply);
        usleep(50);
    }

    while (tries-- > 0) {
        kcookie = xcb_grab_keyboard(
            window.connection, 1, window.scr->root,
            XCB_CURRENT_TIME, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC
        );

        if ((kreply = xcb_grab_keyboard_reply(window.connection, kcookie, NULL)) &&
            kreply->status == XCB_GRAB_STATUS_SUCCESS) {
                free(kreply);
                break;
        }
        free(kreply);
        usleep(50);
    }

    return (tries > 0);
}

static xcb_cursor_t create_cursor() {
    xcb_pixmap_t bitmap;
    xcb_pixmap_t mask;
    xcb_cursor_t cursor;

    static unsigned char curs_bits[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    static unsigned char mask_bits[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    int curs_w = 8,
        curs_h = 8;

    bitmap = xcb_create_pixmap_from_bitmap_data(window.connection,
        window.win, curs_bits, curs_w, curs_h, 1, window.scr->white_pixel,
        window.scr->black_pixel, NULL);
    mask = xcb_create_pixmap_from_bitmap_data(window.connection,
        window.win, mask_bits, curs_w, curs_h, 1, window.scr->white_pixel,
        window.scr->black_pixel, NULL);
    cursor = xcb_generate_id(window.connection);

    xcb_create_cursor(window.connection, cursor, bitmap, mask,
                      65535, 65535, 65535, 0, 0, 0, 0, 0);
    xcb_free_pixmap(window.connection, bitmap);
    xcb_free_pixmap(window.connection, mask);

    return cursor;
}

/* --- */

void init_xkb() {
    if (xkb_x11_setup_xkb_extension(window.connection,
        XKB_X11_MIN_MAJOR_XKB_VERSION,
        XKB_X11_MIN_MINOR_XKB_VERSION,
        0, NULL, NULL, &xkb_base_event, &xkb_base_error
    ) != 1) {
        fprintf(stderr, "Could not setup XKB!");
        exit(1);
    }

    static const xcb_xkb_map_part_t required_map_parts =
        (XCB_XKB_MAP_PART_KEY_TYPES |
         XCB_XKB_MAP_PART_KEY_SYMS |
         XCB_XKB_MAP_PART_MODIFIER_MAP |
         XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS |
         XCB_XKB_MAP_PART_KEY_ACTIONS |
         XCB_XKB_MAP_PART_VIRTUAL_MODS |
         XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP);
    static const xcb_xkb_event_type_t required_events =
        (XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY |
         XCB_XKB_EVENT_TYPE_MAP_NOTIFY |
         XCB_XKB_EVENT_TYPE_STATE_NOTIFY);

    xcb_xkb_select_events(window.connection,
        xkb_x11_get_core_keyboard_device_id(window.connection),
        required_events, 0, required_events, required_map_parts,
        required_map_parts, 0
    );
}

void init_connection() {
    window.connection = xcb_connect(NULL, &window.scrno);
    if (xcb_connection_has_error(window.connection)) {
        fprintf(stderr, "Error opening display.\n");
        exit(1);
    }

    window.scr = xcb_setup_roots_iterator(xcb_get_setup(window.connection)).data;

    window.width = window.scr->width_in_pixels;
    window.height = window.scr->height_in_pixels;
    window.x = window.y = 0;
}

void make_window() {
    window.win = xcb_generate_id(window.connection);

    xcb_change_window_attributes(window.connection, window.scr->root,
                                XCB_CW_EVENT_MASK,
                                (uint32_t[]){XCB_EVENT_MASK_STRUCTURE_NOTIFY});

    uint32_t mask = 0;
    uint32_t values[3];

    mask |= XCB_CW_BACK_PIXEL;
    values[0] = window.scr->white_pixel;

    mask |= XCB_CW_OVERRIDE_REDIRECT;
    values[1] = 1;

    mask |= XCB_CW_EVENT_MASK;
    values[2] = XCB_EVENT_MASK_EXPOSURE |
                XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                XCB_EVENT_MASK_VISIBILITY_CHANGE |
                XCB_EVENT_MASK_STRUCTURE_NOTIFY;

    xcb_create_window(window.connection, XCB_COPY_FROM_PARENT,
                    window.win, window.scr->root, window.x, window.y,
                    window.width, window.height, 0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    XCB_WINDOW_CLASS_COPY_FROM_PARENT,
                    mask, values);

    xcb_cursor_t cursor = create_cursor();
    grab_p_and_k(cursor, 1000);

    xcb_map_window(window.connection, window.win);
    set_wm_flags();
    take_focus();
    xcb_flush(window.connection);
}

/* --- */

static cairo_surface_t* _draw_check(int width, int height) {
    cairo_surface_t* surface;
    cairo_t* cr;

    surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, CHECK_SIZE, CHECK_SIZE);
    cr = cairo_create(surface);
    cairo_surface_destroy(surface);

    cairo_set_source_rgb(cr, 0.56, 0.56, 0.56);
    cairo_paint(cr);

    cairo_set_source_rgb(cr, 0.39, 0.39, 0.39);

    cairo_rectangle(cr, width / 2,  0, width / 2, height / 2);
    cairo_rectangle(cr, 0, height / 2, width / 2, height / 2);
    cairo_fill(cr);

    surface = cairo_surface_reference(cairo_get_target(cr));
    cairo_destroy(cr);

    return surface;
}

void cairo_paint_checkered(cairo_t* cr) {
    cairo_surface_t *check;

    check = _draw_check(CHECK_SIZE, CHECK_SIZE);

    cairo_save(cr);
    cairo_set_source_surface(cr, check, 0, 0);
    cairo_surface_destroy(check);

    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
    cairo_paint(cr);

    cairo_restore(cr);
}

double get_scale(int width, int height, int w, int h) {
    double scale_x = (double)width / (double)w;
    double scale_y = (double)height / (double)h;

    double scale;
    switch (window.arguments->fill_mode) {
        case MODE_FIT:
            scale = (scale_x > scale_y) ? scale_y : scale_x; break;
        case MODE_COVER:
            scale = (scale_x > scale_y) ? scale_x : scale_y; break;
        case MODE_CENTRE:
            scale = 1.0; break;
        case MODE_GROW:
            if (w < width || h < height)
                scale = (scale_x > scale_y) ? scale_x : scale_y;
            else
                scale = 1.0;
            break;
    }

    return scale;
}

void scale_image() {
    scaled_image = cairo_image_surface_create(CAIRO_FORMAT_RGB24, window.width, window.height);
    cairo_t *cr = cairo_create(scaled_image);

    int w = cairo_image_surface_get_width(image);
    int h = cairo_image_surface_get_height(image);

    cairo_paint_checkered(cr);
    double scale, x, y;
    if (window.arguments->full) {
        scale = get_scale(window.width, window.height, w, h);

        x = (window.width - w * scale) / (2 * scale);
        y = (window.height - h * scale) / (2 * scale);

        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, image, x, y);
        cairo_paint(cr);
    } else {
        double ox, oy, wid, hei;
        for (int scr=0; scr<xr_screens; scr++) {
            wid = xr_resolutions[scr].width;
            hei = xr_resolutions[scr].height;
            ox = xr_resolutions[scr].x;
            oy = xr_resolutions[scr].y;

            scale = get_scale(wid, hei, w, h);

            x = (wid - w * scale) / (2 * scale);
            y = (hei - h * scale) / (2 * scale);

            cairo_rectangle(cr, ox, oy, wid, hei);
            cairo_clip(cr);

            cairo_scale(cr, scale, scale);
            cairo_set_source_surface(cr, image, x + (ox / scale), y + (oy / scale));
            cairo_paint(cr);
            cairo_identity_matrix(cr);

            cairo_reset_clip(cr);
        }
    }

    cairo_surface_flush(scaled_image);
}

void load_cairo(char *file_path) {
    depth_iter = xcb_screen_allowed_depths_iterator(window.scr);
    visual_iter = xcb_depth_visuals_iterator(depth_iter.data);

    image = cairo_image_surface_create_from_png(file_path);

    if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "Failed to load image.\n");
        exit(1);
    }

    scale_image();
}

xcb_pixmap_t do_cairo_draw(int is_checking, struct arguments *arguments) {
    xcb_pixmap_t bg_pixmap = xcb_generate_id(window.connection);
    xcb_create_pixmap(window.connection, window.scr->root_depth,
                      bg_pixmap, window.scr->root, window.width,
                      window.height);

    cairo_surface_t *output = cairo_xcb_surface_create(window.connection, bg_pixmap, visual_iter.data, window.width, window.height);
    cairo_t *cr = cairo_create(output);
    double x, y;
    int width, height, ox, oy;

    cairo_set_source_surface(cr, scaled_image, 0, 0);
    cairo_paint(cr);

    cairo_text_extents_t extents;
    if (arguments->font_size) {
        cairo_select_font_face(cr, arguments->font_face, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, arguments->font_size);

        time_t curtime = time(NULL);
        struct tm *tm = localtime(&curtime);
        strftime(timetext, 100, arguments->format, tm);

        cairo_text_extents(cr, timetext, &extents);
    }

    const int count = (arguments->extend && is_checking) ? 256 : input_position;
    const int each = arguments->rad * 2 + arguments->pad;
    const int total = each * count - arguments->pad * 2;

    for (int scr=0; scr<xr_screens; scr++) {
        width = xr_resolutions[scr].width;
        height = xr_resolutions[scr].height;
        ox = xr_resolutions[scr].x;
        oy = xr_resolutions[scr].y;

        if (arguments->font_size) {
            x = (double)((arguments->X_set) ? (arguments->X >= 0) ? (arguments->X) : (width - extents.width - arguments->X) : (width / 2) - (extents.width / 2)) - extents.x_bearing;
            y = (double)((arguments->Y_set) ? (arguments->Y >= 0) ? (arguments->Y) : (height - extents.height - arguments->Y) : ((height / 2) - (extents.height / 2))) - extents.y_bearing;

            cairo_move_to(cr, x + ox, y + oy);
            cairo_set_source_rgba(cr, arguments->clk.r, arguments->clk.g, arguments->clk.b, arguments->clk.a);
            cairo_show_text(cr, timetext);
        }

        double start_x = (double)((arguments->x_set) ? (arguments->x >= 0) ? (arguments->x + arguments->rad) : (window.width - total - arguments->x) : ((width - (count * each - arguments->pad)) / 2.0));
        y = (arguments->y_set) ? (arguments->y >= 0) ? (double)(arguments->y + arguments->rad) : (double)(height + arguments->y - arguments->rad) : (height / 4.0 * 3.0);

        start_x += ox; y += oy;

        if (arguments->shad) {
            cairo_set_source_rgba(cr, 0, 0, 0, 0.2);
            for (int i = 0; i < count; i++) {
                cairo_move_to(cr, start_x + i * each + arguments->rad, y);
                cairo_arc(cr, start_x + i * each, y, arguments->rad + arguments->shad, 0, 2 * M_PI);
                cairo_stroke_preserve(cr);
                cairo_fill(cr);
            }
        }

        if (is_checking) cairo_set_source_rgba(cr, arguments->ldots.r, arguments->ldots.g, arguments->ldots.b, arguments->ldots.a);
        else cairo_set_source_rgba(cr, arguments->dots.r, arguments->dots.g, arguments->dots.b, arguments->dots.a);
        for (int i = 0; i < count; i++) {
            cairo_move_to(cr, start_x + i * each + arguments->rad, y);
            cairo_arc(cr, start_x + i * each, y, arguments->rad, 0, 2 * M_PI);
            cairo_stroke_preserve(cr);
            cairo_fill(cr);
        }
    }

    cairo_destroy(cr);
    cairo_surface_flush(output);
    cairo_surface_destroy(output);

    return bg_pixmap;
}

void render_cairo(int is_checking, struct arguments *arguments) {
    xcb_pixmap_t bg_pixmap = do_cairo_draw(is_checking, arguments);
    xcb_change_window_attributes(window.connection, window.win, XCB_CW_BACK_PIXMAP, (uint32_t[1]){bg_pixmap});

    xcb_clear_area(window.connection, 0, window.win, 0, 0, window.width, window.height);
    xcb_free_pixmap(window.connection, bg_pixmap);
    xcb_flush(window.connection);

    do_redraw = 0;
}

/* --- */

static void xcb_got_event(EV_P_ struct ev_io *w, int revents) {}

static void xcb_prepare_cb(EV_P_ struct ev_prepare *w, int revents) {
    xcb_flush(window.connection);
}

void xcb_check_cb(EV_P_ struct ev_check *w, int revents) {
    xcb_get_geometry_cookie_t geo_cookie;
    xcb_get_geometry_reply_t *geometry;
    xcb_generic_event_t *event;
    xcb_generic_error_t *err;

    while ((event = xcb_poll_for_event(window.connection)) != NULL) {
        // Client messages have the 8th bit set.
        switch (event->response_type & ~0x80) {
            case XCB_KEY_PRESS:
                handle_key_press((xcb_button_press_event_t*)event, window.arguments);
                break;
            case XCB_CLIENT_MESSAGE:
                if((*(xcb_client_message_event_t*)event).data.data32[0] == wm_delete_window) {
                    // WM wants us dead. Too bad.
                }
                break;
            case XCB_EXPOSE:
                geo_cookie = xcb_get_geometry(window.connection, window.win);
                geometry = xcb_get_geometry_reply(window.connection, geo_cookie, &err);

                window.width = geometry->width;
                window.height = geometry->height;
                window.x = geometry->x;
                window.y = geometry->y;

                cairo_surface_destroy(scaled_image);
                scale_image();

                free(geometry); do_redraw = 1; break;
            case XCB_VISIBILITY_NOTIFY:
                handle_visibility_notify(window.connection, (xcb_visibility_notify_event_t *)event);
            default:
                if (event->response_type == xkb_base_event) {
                    process_xkb_event(event);
                }
                break;
        }
        free(event);

        if (do_redraw) {
            render_cairo(0, window.arguments);
        }
    }
}

static void time_redraw_cb(EV_P_ struct ev_periodic *w, int revents) {
    render_cairo(0, window.arguments);
}

static void raise_loop() {
    xcb_connection_t *conn;
    xcb_generic_event_t *event;
    int screens;

    if ((conn = xcb_connect(NULL, &screens)) == NULL || xcb_connection_has_error(conn)) {
        fprintf(stderr, "Can't open dispaly\n");
        exit(1);
    }

    xcb_change_window_attributes(conn, window.win, XCB_CW_EVENT_MASK,
        (uint32_t[]){
            XCB_EVENT_MASK_VISIBILITY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
        });
    xcb_flush(conn);

    while ((event = xcb_wait_for_event(conn)) != NULL) {
        switch (event->response_type & 0x7F) {
            case XCB_VISIBILITY_NOTIFY:
                handle_visibility_notify(conn, (xcb_visibility_notify_event_t *)event);
            case XCB_UNMAP_NOTIFY:
                if (((xcb_unmap_notify_event_t *)event)->window == window.win)
                    exit(0);
                break;
            case XCB_DESTROY_NOTIFY:
                if (((xcb_destroy_notify_event_t *)event)->window == window.win)
                    exit(0);
                break;
        }
        free(event);
    }
}

int do_xcb_things() {
    struct passwd *pw;
    if ((pw = getpwuid(getuid())) == NULL) {
        fprintf(stderr, "getpwuid() failed\n");
        return 1;
    }
    if ((username = pw->pw_name) == NULL) {
        fprintf(stderr, "pw->pw_name is NULL");
        return 1;
    }

    init_connection();
    init_xkb();

    if (!load_keymap()) {
        fprintf(stderr, "Refusing to start without keymap loaded!\n");
        return 1;
    }
    load_compose_table();

    randr_init(&randr_base, window.scr->root);
    if (!randr_query(window.scr->root)) {
        fprintf(stderr, "Failed to get monitor informatin. Do you have randr >= 1.5?\n");
        return 1;
    }

    load_cairo(window.arguments->image);
    xcb_window_t original_focus = get_focused_window();
    make_window();

    pid_t pid = fork();
    if (pid == 0) {
        close(xcb_get_file_descriptor(window.connection));

        const char *sleep_lock_fd = getenv("XSS_SLEEP_LOCK_FD");
        char *endptr;
        if (sleep_lock_fd && *sleep_lock_fd != 0) {
            long int fd = strtol(sleep_lock_fd, &endptr, 10);
            if (*endptr == 0) close(fd);
        }
        raise_loop(window.win);
        exit(0);
    }
    (void)load_keymap();

    main_loop = EV_DEFAULT;
    if (main_loop == NULL) {
        fprintf(stderr, "Could not start libev.");
        exit(1);
    }
    render_cairo(0, window.arguments);

    struct ev_io *xcb_watcher = calloc(sizeof(struct ev_io), 1);
    struct ev_check *xcb_check = calloc(sizeof(struct ev_check), 1);
    struct ev_prepare *xcb_prepare = calloc(sizeof(struct ev_prepare), 1);
    struct ev_periodic *time_redraw = calloc(sizeof(struct ev_periodic), 1);

    ev_io_init(xcb_watcher, xcb_got_event, xcb_get_file_descriptor(window.connection), EV_READ);
    ev_io_start(main_loop, xcb_watcher);

    ev_check_init(xcb_check, xcb_check_cb);
    ev_check_start(main_loop, xcb_check);

    ev_prepare_init(xcb_prepare, xcb_prepare_cb);
    ev_prepare_start(main_loop, xcb_prepare);

    ev_periodic_init(time_redraw, time_redraw_cb, 1.0, 0.5, 0);
    ev_periodic_start(main_loop, time_redraw);

    ev_invoke(main_loop, xcb_check, 0);
    ev_loop(main_loop, 0);

    xcb_disconnect(window.connection);
    set_focused_window(original_focus);

    return 0;
}

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *arguments = state->input;
    switch (key) {
        case 'c': arguments->fill_mode = MODE_COVER; break;
        case 'C': arguments->fill_mode = MODE_CENTRE; break;
        case 'f': arguments->fill_mode = MODE_FIT; break;
        case 'g': arguments->fill_mode = MODE_GROW; break;

        case 'r': arguments->rad = (int)strtol(arg, NULL, 10); break;
        case 'p': arguments->pad = (int)strtol(arg, NULL, 10); break;

        case 'x': arguments->x_set = 1; arguments->x = (int)strtol(arg, NULL, 10); break;
        case 'y': arguments->y_set = 1; arguments->y = (int)strtol(arg, NULL, 10); break;

        case 'X': arguments->X_set = 1; arguments->X = (int)strtol(arg, NULL, 10); break;
        case 'Y': arguments->Y_set = 1; arguments->Y = (int)strtol(arg, NULL, 10); break;

        case 's': sscanf(arg, "%lf", &arguments->font_size); break;
        case 'S': arguments->shad = (int)strtol(arg, NULL, 10); break;

        case 'F': arguments->format = arg; break;
        case 'a': arguments->font_face = arg; break;

        case 'l': arguments->ldots = parse_hex(arg); break;
        case 'd': arguments->dots = parse_hex(arg); break;
        case 't': arguments->clk = parse_hex(arg); break;

        case 4: arguments->extend = 1; break;

        case 3: arguments->full = 0; break;

        case ARGP_KEY_ARG:
            if (state->arg_num >= 1) argp_usage(state);
            arguments->image = arg;
            break;
        case ARGP_KEY_END:
            if (state->arg_num < 1) argp_usage(state);
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

int main(int argc, char **argv) {
    timetext = malloc(512);

    struct arguments arguments;
    arguments.fill_mode = MODE_COVER;
    arguments.pad = arguments.rad = 8;
    arguments.x_set = arguments.y_set = 0;
    arguments.X_set = arguments.Y_set = 0;
    arguments.extend = arguments.full = 0;
    arguments.shad = 2;

    arguments.dots.r = arguments.dots.g = arguments.dots.b = arguments.clk.r = arguments.clk.g = arguments.clk.b = 0.95;
    arguments.dots.a = arguments.clk.a = arguments.ldots.a = 1.0;
    arguments.ldots.r = 0.93; arguments.ldots.g = 0.64;
    arguments.ldots.b = 0.36;

    arguments.format = "%H:%M";
    arguments.font_face = "sans";
    arguments.font_size = 152.;

    static struct argp argp = {options, parse_opt, args_doc, doc, 0, 0, 0};
    argp_parse(&argp, argc, argv, 0, 0, &arguments);

    window.arguments = &arguments;
    return do_xcb_things();
}
