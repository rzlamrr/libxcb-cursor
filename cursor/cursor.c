/*
 * vim:ts=4:sw=4:expandtab
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <xcb/xcb.h>
#include <xcb/xcb_renderutil.h>

#include "cursor.h"
#include "xcb_cursor.h"

/*
 * Parses the root window’s RESOURCE_MANAGER atom contents and stores the
 * attributes declared above in resource_manager_val.
 *
 */
static void parse_resource_manager(xcb_cursor_context_t *c, const xcb_get_property_reply_t *rm_reply) {
    int rm_length;
    char *rm = NULL;
    char *saveptr = NULL;
    char *line = NULL;
    char *sep = NULL;

    if (rm_reply == NULL || (rm_length = xcb_get_property_value_length(rm_reply)) == 0)
        return;

    if (asprintf(&rm, "%.*s", rm_length, (char*)xcb_get_property_value(rm_reply)) == -1)
        return;

    for (char *str = rm; ; str = NULL) {
        if ((line = strtok_r(str, "\n", &saveptr)) == NULL)
            break;
        /* Split the string at the delimiting : */
        if ((sep = strchr(line, ':')) == NULL) {
            /* Invalid line?! */
            free(rm);
            return;
        }
        *(sep++) = '\0';
        while (isspace(*sep))
            sep++;
        /* strdup() may return NULL, which is interpreted later as the key not
         * being available. */
        if (strcmp(line, "Xcursor.theme") == 0)
            c->rm[RM_XCURSOR_THEME] = strdup(sep);
        else if (strcmp(line, "Xcursor.size") == 0)
            c->rm[RM_XCURSOR_SIZE] = strdup(sep);
        else if (strcmp(line, "Xft.dpi") == 0)
            c->rm[RM_XFT_DPI] = strdup(sep);
    }

    free(rm);
}

/*
 * Tries to figure out the cursor size by checking:
 * 1. The environment variable XCURSOR_SIZE
 * 2. The RESOURCE_MANAGER entry Xcursor.size
 * 3. Guess with the RESOURCE_MANAGER entry Xft.dpi * 16 / 72
 * 4. Guess with the display size.
 *
 */
static uint32_t get_default_size(xcb_cursor_context_t *c, xcb_screen_t *screen) {
    char *env;
    uint16_t dim;

    if ((env = getenv("XCURSOR_SIZE")) != NULL)
        return atoi(env);

    if (c->rm[RM_XCURSOR_SIZE] != NULL)
        return atoi(c->rm[RM_XCURSOR_SIZE]);

    if (c->rm[RM_XFT_DPI] != NULL) {
        const int dpi = atoi(c->rm[RM_XFT_DPI]);
        if (dpi > 0)
            return dpi * 16 / 72;
    }

    if (screen->height_in_pixels < screen->width_in_pixels)
        dim = screen->height_in_pixels;
    else
        dim = screen->width_in_pixels;

    return dim / 48;
}

int xcb_cursor_context_new(xcb_cursor_context_t **ctx, xcb_connection_t *conn) {
    xcb_cursor_context_t *c;
    xcb_screen_t *screen = NULL;
    xcb_get_property_cookie_t rm_cookie;
    xcb_get_property_reply_t *rm_reply;
    xcb_render_query_pict_formats_cookie_t pf_cookie;

    if ((*ctx = calloc(1, sizeof(struct xcb_cursor_context_t))) == NULL)
        return -errno;

    c = *ctx;
    c->conn = conn;

    // TODO: error checking?
    screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
    c->root = screen->root;
    // XXX: Is it maybe necessary to ever use long_offset != 0?
    // XXX: proper length? xlib seems to use 100 MB o_O
    rm_cookie = xcb_get_property(conn, 0, screen->root, XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, 0, 16 * 1024);
    pf_cookie = xcb_render_query_pict_formats(conn);
    c->cursor_font = xcb_generate_id(conn);
    xcb_open_font(conn, c->cursor_font, strlen("cursor"), "cursor");

    rm_reply = xcb_get_property_reply(conn, rm_cookie, NULL);
    parse_resource_manager(c, rm_reply);
    free(rm_reply);

    c->pf_reply = xcb_render_query_pict_formats_reply(conn, pf_cookie, NULL);
    c->pict_format = xcb_render_util_find_standard_format(c->pf_reply, XCB_PICT_STANDARD_ARGB_32);

    c->size = get_default_size(c, screen);

    return 0;
}

void xcb_cursor_context_free(xcb_cursor_context_t *c) {
#define FREE(p) do { \
    if (p) \
        free(p); \
} while (0)
    FREE(c->rm[RM_XCURSOR_THEME]);
    FREE(c->rm[RM_XCURSOR_SIZE]);
    FREE(c->rm[RM_XFT_DPI]);
    free(c->pf_reply);
    free(c);
}
