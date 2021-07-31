////////////////////////////////////////////////////////////////////////////////
//
// Copyright (C) 2021 Ripose
//
// This file is part of Valhalla.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License version 3 as
// published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// Additional permission under GNU AGPL version 3 section 7
//
// If you modify this Program, or any covered work, by linking or combining it
// with FastCGI (or a modified version of that library), containing parts
// covered by the terms of the FastCGI Open Market Licence, the licensors of
// this Program grant you additional permission to convey the resulting work.
//
////////////////////////////////////////////////////////////////////////////////

#include "route.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

#include <talloc.h>

#include "containers/khash.h"
#include "strutil.h"

KHASH_INIT(cn, char, route_node_t *, 1, kh_int_hash_func, kh_int_hash_equal)

/* Used for easily converting vla_http_method flags into lookups. */
enum http_method_lookup
{
    HTTP_GET     = 0,
    HTTP_HEAD    = 1,
    HTTP_POST    = 2,
    HTTP_PUT     = 3,
    HTTP_DELETE  = 4,
    HTTP_CONNECT = 5,
    HTTP_OPTIONS = 6,
    HTTP_TRACE   = 7,
    HTTP_PATCH   = 8,

    HTTP_SIZE,
};

/* Describes the type of node. */
enum node_type
{
    /* Only exact routes should end up on this node. */
    NODE_EXACT,

    /* Capture everything up to the end of the string or the next '/'. */
    NODE_CAPTURE,

    /* This node has no children because everything that reaches it matches. */
    NODE_ALL,
};

/* Node in the route tree. */
typedef struct route_node_t
{
    /* An array of route infos indexed into via the http_method_lookup enum. */
    route_info_t *infos[HTTP_SIZE];

    /* Describes what type of node this is. Determines what kind of children it
     * has.
     */
    enum node_type type;

    /* Map containing all this node's children. Will be NULL is type is
     * NODE_ALL.
     */
    khash_t(cn) *map;
} route_node_t;

/**
 * Destructs a route_node_t.
 *
 * @param node The route_node_t to destruct.
 *
 * @return Always 0.
 */
static int destruct_route_node(route_node_t *node)
{
    kh_destroy(cn, node->map);
    return 0;
}

/**
 * Initializes a route_node_t.
 *
 * @param parent The parent of the route_node_t. Used for talloc garbage
 *               collection.
 */
static route_node_t *init_route_node(void *parent, enum node_type type)
{
    route_node_t *node = talloc(parent, route_node_t);
    talloc_set_destructor(node, destruct_route_node);
    bzero(node, sizeof(route_node_t));
    node->type = type;
    switch (type)
    {
    case NODE_EXACT:
    case NODE_CAPTURE:
    default:
        node->map = kh_init(cn);
        break;
    }
    return node;
}

route_node_t *route_init_root(void *ctx)
{
    return init_route_node(ctx, NODE_EXACT);
}

/**
 * Creates a path in the tree for the route.
 *
 * @param root The root node of the route tree.
 *
 * @param route The route to add.
 *
 * @return The terminating node for the route. NULL if the route overlaps with
 *         another existing route or error.
 */
static route_node_t *create_route_path(route_node_t *root, const char *route)
{
    route_node_t *current = root;
    while (*route)
    {
        khiter_t it = kh_get(cn, current->map, *route);
        if (it == kh_end(current->map))
        {
            enum node_type type = NODE_EXACT;
            if (route[1] == ':')
            {
                type = NODE_CAPTURE;
            }
            else if (route[1] == '*')
            {
                type = NODE_ALL;
            }
            route_node_t *next = init_route_node(current, type);

            int ret;
            it = kh_put(cn, current->map, *route, &ret);
            switch (ret)
            {
            case 1: // Key doesn't exist
                kh_key(current->map, it) = *route;
                kh_val(current->map, it) = next;
                break;

            case -1: // Error
                /* TODO Logging */
                return NULL;

            default: // Key exists (should never happen)
                assert(0);
            }
            current = next;
        }
        else
        {
            current = kh_val(current->map, it);
        }

        switch (current->type)
        {
        case NODE_EXACT:
            if (route[1] == ':' || route[1] == '*')
            {
                return NULL;
            }
            break;

        case NODE_CAPTURE:
            if (route[1] != ':')
            {
                return NULL;
            }
            route = su_strchrnul(++route, '/');
            if (*route == '\0')
            {
                return current;
            }
            --route;
            break;

        case NODE_ALL:
            if (route[1] != '*')
            {
                return NULL;
            }
            return current;
        }

        ++route;
    }
    return current;
}

/**
 * Get the node associated with a route in the tree.
 *
 * @param root The root of the route tree.
 *
 * @param route The route this route info should be associated with.
 *
 * @return The route node the route leads to, NULL if it doesn't.
 */
static route_node_t *get_route_node(route_node_t *root, const char *route)
{
    route_node_t *current = root;
    while (*route)
    {
        khiter_t it = kh_get(cn, current->map, *route);
        if (it == kh_end(current->map))
        {
            return NULL;
        }
        current = kh_val(current->map, it);

        switch (current->type)
        {
        case NODE_CAPTURE:
            route = su_strchrnul(++route, '/');
            if (*route == '\0')
            {
                return current;
            }
            --route;
            break;

        case NODE_ALL:
            return current;
        }

        ++route;
    }
    return current;
}

route_info_t *route_info_create(
    void *ctx,
    vla_handler_func hdlr,
    void *hdlr_arg,
    va_list ap)
{
    va_list cpy;

    /* Gets the length of the middleware array. */
    va_copy(cpy, ap);
    size_t mw_len = 0;
    while (va_arg(cpy, vla_middleware_func))
    {
        va_arg(cpy, void *);
        ++mw_len;
    }
    va_end(cpy);

    /* Initialize the route_info_t. */
    route_info_t *info = talloc(ctx, route_info_t);
    *info = (route_info_t) {
        .hdlr = hdlr,
        .hdlr_arg = hdlr_arg,
        .mw = talloc_array(info, vla_middleware_func, mw_len + 1),
        .mw_args = talloc_array(info, void *, mw_len + 1),
    };

    /* Copy the middleware. */
    va_copy(cpy, ap);
    for (size_t i = 0; i < mw_len; ++i)
    {
        info->mw[i] = va_arg(cpy, vla_middleware_func);
        info->mw_args[i] = va_arg(cpy, void *);
    }
    va_end(cpy);
    info->mw[mw_len] = NULL;
    info->mw_args[mw_len] = NULL;

    return info;
}

int route_add(
    route_node_t *root,
    uint32_t methods,
    const char *route,
    vla_handler_func hdlr,
    void *hdlr_arg,
    va_list ap)
{
    /* Make sure the route starts with a '/' */
    if (*route != '/')
    {
        return -1;
    }

    /* Get the node for the route. */
    route_node_t *node = create_route_path(root, route);
    if (node == NULL)
    {
        return 1;
    }

    /* Make sure the route doesn't already exist. */
    if (methods & VLA_HTTP_GET     && node->infos[HTTP_GET]     ||
        methods & VLA_HTTP_HEAD    && node->infos[HTTP_HEAD]    ||
        methods & VLA_HTTP_POST    && node->infos[HTTP_POST]    ||
        methods & VLA_HTTP_PUT     && node->infos[HTTP_PUT]     ||
        methods & VLA_HTTP_DELETE  && node->infos[HTTP_DELETE]  ||
        methods & VLA_HTTP_CONNECT && node->infos[HTTP_CONNECT] ||
        methods & VLA_HTTP_OPTIONS && node->infos[HTTP_OPTIONS] ||
        methods & VLA_HTTP_TRACE   && node->infos[HTTP_TRACE]   ||
        methods & VLA_HTTP_PATCH   && node->infos[HTTP_PATCH])
    {
        return 1;
    }

    /* Initialize the route_info_t. */
    route_info_t *info = route_info_create(node, hdlr, hdlr_arg, ap);

    /* Insert the route info into the tree. */
    if (methods & VLA_HTTP_GET)
        node->infos[HTTP_GET] = info;
    if (methods & VLA_HTTP_HEAD)
        node->infos[HTTP_HEAD] = info;
    if (methods & VLA_HTTP_POST)
        node->infos[HTTP_POST] = info;
    if (methods & VLA_HTTP_PUT)
        node->infos[HTTP_PUT] = info;
    if (methods & VLA_HTTP_DELETE)
        node->infos[HTTP_DELETE] = info;
    if (methods & VLA_HTTP_CONNECT)
        node->infos[HTTP_CONNECT] = info;
    if (methods & VLA_HTTP_OPTIONS)
        node->infos[HTTP_OPTIONS] = info;
    if (methods & VLA_HTTP_TRACE)
        node->infos[HTTP_TRACE] = info;
    if (methods & VLA_HTTP_PATCH)
        node->infos[HTTP_PATCH] = info;

    return 0;
}

route_info_t *route_get(
    route_node_t *root,
    const char *route,
    enum vla_http_method method)
{
    route_node_t *node = get_route_node(root, route);
    if (node == NULL)
    {
        return NULL;
    }

    switch (method)
    {
    case VLA_HTTP_GET:
        return node->infos[HTTP_GET];
    case VLA_HTTP_HEAD:
        return node->infos[HTTP_HEAD];
    case VLA_HTTP_POST:
        return node->infos[HTTP_POST];
    case VLA_HTTP_PUT:
        return node->infos[HTTP_PUT];
    case VLA_HTTP_DELETE:
        return node->infos[HTTP_DELETE];
    case VLA_HTTP_CONNECT:
        return node->infos[HTTP_CONNECT];
    case VLA_HTTP_OPTIONS:
        return node->infos[HTTP_OPTIONS];
    case VLA_HTTP_TRACE:
        return node->infos[HTTP_TRACE];
    case VLA_HTTP_PATCH:
        return node->infos[HTTP_PATCH];
    }

    return NULL;
}
