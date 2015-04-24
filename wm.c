#include <signal.h>
#include <xcb/xcb.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>

enum { INACTIVE, ACTIVE };

/* global variables */
static int sigcode;
static xcb_connection_t *conn;
static xcb_screen_t *scr;
static xcb_window_t focuswin;
static void (*events[XCB_NO_OPERATION])(xcb_generic_event_t *e);
uint32_t values[3];

static void sigcatch(const int sig);
static void quit();
static void map_handler(xcb_generic_event_t *e);
static void create_handler(xcb_generic_event_t *e);
static void configure_handler(xcb_generic_event_t *e);
static void destroy_handler(xcb_generic_event_t *e);

#ifdef ENABLE_MOUSE
static void button_press_handler(xcb_generic_event_t *e);
static void button_release_handler(xcb_generic_event_t *e);
static void motion_notify_handler(xcb_generic_event_t *e);
#endif
#ifdef ENABLE_SLOPPY
static void enter_notify_handler(xcb_generic_event_t *e);
#endif

static void subscribe(xcb_window_t);
static void cleanup(void);
static  int deploy(void);
static void focus(xcb_window_t, int);

#include "config.h"

static void sigcatch(const int sig) {
    sigcode = sig;
}

static void quit() {
    cleanup();
    exit(EXIT_SUCCESS);
}

static void cleanup(void) {
    if (conn != NULL)
        xcb_disconnect(conn);
}

static void create_handler(xcb_generic_event_t *ev) {
    xcb_create_notify_event_t *e = (xcb_create_notify_event_t *)ev;
    if (!e->override_redirect) {
        subscribe(e->window);
        focus(e->window, ACTIVE);
    }
}

static void configure_handler(xcb_generic_event_t *ev) {
    xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t *)ev;
    if (e->window != focuswin)
        focus(e->window, INACTIVE);
    focus(focuswin, ACTIVE);
}

static void map_handler(xcb_generic_event_t *ev) {
    xcb_map_notify_event_t *e = (xcb_map_notify_event_t *)ev;
    if (!e->override_redirect) {
        xcb_map_window(conn, e->window);
        focus(e->window, ACTIVE);
    }
}

static void destroy_handler(xcb_generic_event_t *ev) {
    xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t *)ev;
    xcb_kill_client(conn, e->window);
}

#ifdef ENABLE_MOUSE
static void button_press_handler(xcb_generic_event_t *ev) {
    xcb_get_geometry_reply_t *geom;

    xcb_button_press_event_t *e = (xcb_button_press_event_t *)ev;
    xcb_window_t win = e->child;

    if (!win || win == scr->root)
        return;

    values[0] = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_STACK_MODE, values);
    geom = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, win), NULL);

    if (e->detail == 1) {
        values[2] = 1;
        xcb_warp_pointer(conn, XCB_NONE, win, 0, 0, 0,
            0, geom->width/2, geom->height/2);
    } else {
        values[2] = 3;
        xcb_warp_pointer(conn, XCB_NONE, win, 0, 0, 0,
                0, geom->width, geom->height);
    }
    xcb_grab_pointer(conn, 0, scr->root, XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION_HINT,
        XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, scr->root, XCB_NONE, XCB_CURRENT_TIME);
    xcb_flush(conn);
}

static void motion_notify_handler(xcb_generic_event_t *ev) {
    xcb_get_geometry_reply_t *geom;

    xcb_motion_notify_event_t *e = (xcb_motion_notify_event_t *)ev;
    xcb_window_t win = e->child;

    xcb_query_pointer_reply_t *pointer;
    pointer = xcb_query_pointer_reply(conn, xcb_query_pointer(conn, scr->root), 0);
    if (values[2] == 1) {
        geom = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, win), NULL);
        if (!geom)
            return;

        values[0] = (pointer->root_x + geom->width / 2 > scr->width_in_pixels - (BORDERWIDTH*2)) ? scr->width_in_pixels - geom->width - (BORDERWIDTH*2)
            : pointer->root_x - geom->width / 2;
        values[1] = (pointer->root_y + geom->height / 2 > scr->height_in_pixels - (BORDERWIDTH*2)) ? (scr->height_in_pixels - geom->height - (BORDERWIDTH*2))
            : pointer->root_y - geom->height / 2;

        if (pointer->root_x < geom->width/2)
            values[0] = 0;
        if (pointer->root_y < geom->height/2)
            values[1] = 0;

        xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
        xcb_flush(conn);
    } else if (values[2] == 3) {
        geom = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, win), NULL);
        values[0] = pointer->root_x - geom->x;
        values[1] = pointer->root_y - geom->y;
        xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
        xcb_flush(conn);
    }
}

static void button_release_handler(xcb_generic_event_t *ev) {
    xcb_button_release_event_t *e = (xcb_button_release_event_t *)ev;
    xcb_window_t win = e->child;
    focus(win, ACTIVE);
    xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
}
#endif

#ifdef ENABLE_SLOPPY
static void enter_notify_handler(xcb_generic_event_t *e) {
    focus(((xcb_enter_notify_event_t *) ev)->event, ACTIVE);
}
#endif

static int deploy(void) {

    for (unsigned int i=0; i < XCB_NO_OPERATION; i++) events[i] = NULL;
    events[XCB_MAP_NOTIFY] = map_handler;
    events[XCB_CREATE_NOTIFY] = create_handler;
    events[XCB_CONFIGURE_NOTIFY] = configure_handler;
    events[XCB_DESTROY_NOTIFY] = destroy_handler;
#ifdef ENABLE_MOUSE
    events[XCB_BUTTON_PRESS] = button_press_handler;
    events[XCB_BUTTON_RELEASE] = button_release_handler;
    events[XCB_MOTION_NOTIFY] = motion_notify_handler;
#endif
#ifdef ENABLE_SLOPPY
    events[XCB_ENTER_NOTIFY] = enter_notify_handler;
#endif

    /* init xcb and grab events */
    uint32_t values[2];
    int mask;

    if (xcb_connection_has_error(conn = xcb_connect(NULL, NULL)))
        return -1;

    scr = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
    focuswin = scr->root;

#ifdef ENABLE_MOUSE
    xcb_grab_button(conn, 0, scr->root, XCB_EVENT_MASK_BUTTON_PRESS |
            XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC,
            XCB_GRAB_MODE_ASYNC, scr->root, XCB_NONE, 1, MOD);

    xcb_grab_button(conn, 0, scr->root, XCB_EVENT_MASK_BUTTON_PRESS |
            XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC,
            XCB_GRAB_MODE_ASYNC, scr->root, XCB_NONE, 3, MOD);
#endif

    mask = XCB_CW_EVENT_MASK;
    values[0] = XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
    xcb_change_window_attributes_checked(conn, scr->root, mask, values);

    xcb_flush(conn);

    return 0;
}

static void focus(xcb_window_t win, int mode) {
    uint32_t values[1];
    values[0] = mode ? FOCUSCOL : UNFOCUSCOL;
    xcb_change_window_attributes(conn, win, XCB_CW_BORDER_PIXEL, values);

    if (mode == ACTIVE) {
        xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, win, XCB_CURRENT_TIME);
        if (win != focuswin) {
            focus(focuswin, INACTIVE);
            focuswin = win;
        }
    }
}

static void subscribe(xcb_window_t win) {
    uint32_t values[2];

    /* subscribe to events */
    values[0] = XCB_EVENT_MASK_ENTER_WINDOW;
    values[1] = XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
    xcb_change_window_attributes(conn, win, XCB_CW_EVENT_MASK, values);

    /* border width */
    values[0] = BORDERWIDTH;
    xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_BORDER_WIDTH, values);
}

static void run(void) {
    sigcode = 0;
    while(sigcode == 0) {
        xcb_flush(conn);
        if (xcb_connection_has_error(conn)) abort();
        xcb_generic_event_t *ev = xcb_wait_for_event(conn);
        if (ev && events[ev->response_type & ~0x80])
            events[ev->response_type & ~0x80](ev);
        free(ev);
    }
}

int main(void) {
    atexit(cleanup);
    signal(SIGINT, sigcatch);
    signal(SIGTERM, sigcatch);
    signal(SIGUSR1, quit);

    if (deploy() < 0)
        errx(EXIT_FAILURE, "error connecting to X");

    run();
    exit(sigcode);
}

/* vim: set softtabstop=4 shiftwidth=4 tabstop=4: */
