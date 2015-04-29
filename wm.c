#include <signal.h>
#include <xcb/xcb.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <stdbool.h>
#include <syslog.h>
#include <sys/queue.h>

#define MOD XCB_MOD_MASK_4
#define BORDERWITH 2
#define FOCUSCOL   0x00ffff33
#define UNFOCUSCOL 0x15000000
#define ENABLE_MOUSE

static int sigcode;
static xcb_connection_t *conn;
static xcb_screen_t *scr;
static xcb_window_t focuswin;
static void (*events[XCB_NO_OPERATION])(xcb_generic_event_t *e);
static bool moved;

static void sigcatch(const int);
static void quit();
static void map_notify_handler(xcb_generic_event_t*);
static void create_notify_handler(xcb_generic_event_t*);
static void destroy_handler(xcb_generic_event_t*);

#ifdef ENABLE_MOUSE
static void button_press_handler(xcb_generic_event_t*);
static void button_release_handler(xcb_generic_event_t*);
static void motion_notify_handler(xcb_generic_event_t*);
#endif

static void cleanup(void);
static void focus(xcb_window_t);
static int deploy(void);

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
    closelog();
}

static void create_notify_handler(xcb_generic_event_t *ev) {
    syslog(LOG_INFO, "create_notify_handler()");
    xcb_create_notify_event_t *e = (xcb_create_notify_event_t *)ev;
    if (!e->override_redirect) {
        uint32_t values[] = { XCB_EVENT_MASK_ENTER_WINDOW, XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY };
        xcb_change_window_attributes(conn, e->window, XCB_CW_EVENT_MASK, values);
        uint32_t values2[] = { BORDERWITH };
        xcb_configure_window(conn, e->window, XCB_CONFIG_WINDOW_BORDER_WIDTH, values2);
    }
}

static void map_notify_handler(xcb_generic_event_t *ev) {
    syslog(LOG_INFO, "map_notify_handler()");
    xcb_map_notify_event_t *e = (xcb_map_notify_event_t *)ev;
    if (!e->override_redirect) {
        xcb_map_window(conn, e->window);
        focus(e->window);
    }
}

static void destroy_handler(xcb_generic_event_t *ev) {
    xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t *)ev;
    xcb_kill_client(conn, e->window);
}

#ifdef ENABLE_MOUSE
static void button_press_handler(xcb_generic_event_t *ev) {
    syslog(LOG_INFO, "button_press_handler()");
    xcb_button_press_event_t *e = (xcb_button_press_event_t *)ev;

    if (e->child && e->child != scr->root) {
        xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, e->child, XCB_CURRENT_TIME);

        uint32_t values[] = { XCB_STACK_MODE_ABOVE };
        xcb_configure_window(conn, e->child, XCB_CONFIG_WINDOW_STACK_MODE, values);

        moved = e->detail == 1 ? true : moved;

        focus(e->child);

        xcb_grab_pointer(conn, 0, scr->root, XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION_HINT,
            XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, scr->root, XCB_NONE, XCB_CURRENT_TIME);
        xcb_flush(conn);
    }
}

static void motion_notify_handler(xcb_generic_event_t *ev) {
    xcb_motion_notify_event_t *e = (xcb_motion_notify_event_t *)ev;
    if (moved) {
        xcb_get_geometry_reply_t *geom = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, e->child), NULL);
        if (geom) {
            uint32_t values[2];

            xcb_query_pointer_reply_t *pointer = xcb_query_pointer_reply(conn, xcb_query_pointer(conn, scr->root), 0);
            if (pointer->root_x < geom->width / 2)
                values[0] = 0;
            else
                values[0] = (pointer->root_x + geom->width / 2 > scr->width_in_pixels - BORDERWITH*2) ? scr->width_in_pixels - geom->width - BORDERWITH*2 : pointer->root_x - geom->width / 2;

            if (pointer->root_y < geom->height / 2)
                values[1] = 0;
            else
                values[1] = (pointer->root_y + geom->height / 2 > scr->height_in_pixels) ? scr->height_in_pixels - geom->height - BORDERWITH*2 : pointer->root_y - geom->height / 2;

            xcb_configure_window(conn, e->child, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
            xcb_flush(conn);
        }
    }
}

static void button_release_handler(xcb_generic_event_t *ev) {
    xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
}
#endif

static int deploy(void) {

    for (unsigned int i=0; i < XCB_NO_OPERATION; i++) events[i] = NULL;
    events[XCB_MAP_NOTIFY] = map_notify_handler;
    events[XCB_CREATE_NOTIFY] = create_notify_handler;
    events[XCB_DESTROY_NOTIFY] = destroy_handler;
#ifdef ENABLE_MOUSE
    events[XCB_BUTTON_PRESS] = button_press_handler;
    events[XCB_BUTTON_RELEASE] = button_release_handler;
    events[XCB_MOTION_NOTIFY] = motion_notify_handler;
#endif

    openlog ("wm", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);

    if (xcb_connection_has_error(conn = xcb_connect(NULL, NULL)))
        return -1;

    scr = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
    focuswin = scr->root;

#ifdef ENABLE_MOUSE
    xcb_grab_button(conn, 0, scr->root, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC,
            XCB_GRAB_MODE_ASYNC, scr->root, XCB_NONE, XCB_STACK_MODE_ABOVE, MOD);
#endif

    uint32_t values[] = { XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY };
    xcb_change_window_attributes_checked(conn, scr->root, XCB_CW_EVENT_MASK, values);
    xcb_flush(conn);

    return 0;
}

static void focus(xcb_window_t win) {
    /* focus current window*/
    uint32_t values[] = { FOCUSCOL };
    xcb_change_window_attributes(conn, win, XCB_CW_BORDER_PIXEL, values);
    xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, win, XCB_CURRENT_TIME);

    /* unfocus previous window*/
    if (focuswin != win) {
        values[0] = UNFOCUSCOL;
        xcb_change_window_attributes(conn, focuswin, XCB_CW_BORDER_PIXEL, values);
        focuswin = win;
    }
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
