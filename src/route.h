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

#ifndef __ROUTE_H__
#define __ROUTE_H__

#include "include/valhalla.h"

#include <stdarg.h>
#include <stdint.h>

/* A node in the route tree. Returned as a root outside of route.c. */
typedef struct route_node_t route_node_t;

/* Information about a route. */
typedef struct route_info_t
{
    /* A handler function. */
    vla_handler_func hdlr;

    /* The second argument to the route function. */
    void *hdlr_arg;

    /* A NULL terminated array of middleware functions. */
    vla_middleware_func *mw;

    /* A NULL terminated array of middleware function arguments. */
    void **mw_args;
} route_info_t;

/**
 * Initializes the root of a route tree.
 *
 * @param ctx The talloc context this root should belong to.
 *
 * @return A route tree.
 */
route_node_t *route_init_root(void *ctx);

/**
 * Initializes a route_info_t.
 *
 * @param ctx The talloc context the route_info_t should be a child of.
 *
 * @param hdlr The handler function for the route info.
 *
 * @param hdlr_arg The second argument to the hdlr function.
 *
 * @param ap A va_list containing alternating pointers to vla_middleware_func
 *           and void * arguments. Terminated with a NULL vla_middleware_func.
 *
 * @return A route_info_t containing a copy of all the route information.
 */
route_info_t *route_info_create(
    void *ctx,
    vla_handler_func hdlr,
    void *hdlr_arg,
    va_list ap);

/**
 * Adds a route.
 * Routes are expected to be URL decoded.
 *
 * @param root The root node of the route tree.
 *
 * @param methods Flags defining what HTTP methods should be handled by this
 *                route. Defined by ORing vla_http_verbs together.
 *
 * @param route The route to handle. Routing supports regular expressions. Each
 *              section of a route is delmited by /. For example '/*' will
 *              match '/foo' but not '/foo/bar'.
 *
 * @param hdlr The handler function for this route.
 *
 * @param hdlr_arg The second argument to the hdlr function.
 *
 * @param ap A va_list containing alternating pointers to vla_middleware_func
 *           and void * arguments. Terminated with a NULL vla_middleware_func.
 *
 * @return 0 on success, 1 if the route overlaps with another, -1 if the route
 *         doesn't start with '/'.
 */
int route_add(
    route_node_t *root,
    uint32_t methods,
    const char *route,
    vla_handler_func hdlr,
    void *hdlr_arg,
    va_list ap);

/**
 * Gets handlers for the route and method.
 * Routes are expected to be URL decoded.
 *
 * @param root The root of the route tree.
 *
 * @param route The route to get.
 *
 * @param method The method of the route to get.
 *
 * @return The route_info_t of the route and method if it exists, NULL
 *         otherwise.
 */
const route_info_t *route_get(
    route_node_t *root,
    const char *route,
    enum vla_http_method method);

#endif // __ROUTE_H__