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

#include "context.h"

#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include <fcgiapp.h>
#include <talloc.h>

#include "request.h"

typedef struct vla_context
{
    route_node_t *route_tree_root;

    route_info_t *unknown_info;
} vla_context;

vla_context *vla_init()
{
    if (FCGX_Init())
    {
        return NULL;
    }

    vla_context *ctx = talloc(NULL, vla_context);
    if (ctx == NULL)
    {
        return NULL;
    }
    talloc_set_name_const(ctx, "Top Level Valhalla Context");
    ctx->route_tree_root = route_init_root(ctx);
    if (ctx->route_tree_root == NULL)
    {
        /* TODO Logging */
        talloc_free(ctx);
        return NULL;
    }
    ctx->unknown_info = NULL;
    return ctx;
}

int vla_free(void *ptr)
{
    return talloc_free(ptr);
}

void vla_init_cookie(vla_cookie_t *cookie)
{
    bzero(cookie, sizeof(vla_cookie_t));
}

int vla_add_route(
    vla_context *ctx,
    uint32_t methods,
    const char *route,
    vla_handler_func handler,
    void *handler_arg,
    ...)
{
    va_list ap;
    va_start(ap, handler_arg);
    int ret = route_add(
        ctx->route_tree_root,
        methods,
        route,
        handler,
        handler_arg,
        ap
    );
    va_end(ap);
    return ret;
}

int vla_set_not_found_handler(
    vla_context *ctx,
    vla_handler_func handler,
    void *handler_arg,
    ...)
{
    talloc_free(ctx->unknown_info);

    va_list ap;
    va_start(ap, handler_arg);
    ctx->unknown_info = route_info_create(ctx, handler, handler_arg, ap);
    va_end(ap);
    if (ctx->unknown_info == NULL)
    {
        /* TODO Logging */
        return -1;
    }
    return 0;
}

const route_info_t *context_get_route(
    vla_context *ctx,
    const char *uri,
    enum vla_http_method method)
{
    const route_info_t *route = route_get(ctx->route_tree_root, uri, method);
    return route ? route : ctx->unknown_info;
}

/**
 * Callback function for handling iterating over response headers. Prints
 * headers to the response.
 *
 * @param hdr The header.
 *
 * @param val The value of the header.
 *
 * @param ptr A pointer to the FCGI request.
 *
 * @return 0 on success, -1 on error.
 */
int resp_header_handler(const char *hdr, const char *val, void *ptr)
{
    FCGX_Request *f_req = (FCGX_Request *)ptr;
    if (FCGX_FPrintF(f_req->out, "%s: %s\r\n", hdr, val) < 0)
    {
        return -1;
    }
    return 0;
}

/**
 * Sends a response to the web server.
 *
 * @param f_req The FCGI request.
 *
 * @param req The request containg the response information.
 *
 * @return 0 if the response was successfully sent, -1 otherwise.
 */
static int send_response(FCGX_Request *f_req, const vla_request *req)
{
    if (response_header_iterate(req, resp_header_handler, f_req))
    {
        return -1;
    }
    if (FCGX_PutS("\r\n", f_req->out) < 0)
    {
        return -1;
    }
    const char *body = response_get_body(req);
    size_t body_len = response_get_body_length(req);
    if (FCGX_PutStr(body, body_len, f_req->out) < 0)
    {
        return -1;
    }
    return 0;
}

int vla_accept(vla_context *ctx)
{
    FCGX_Request f_req;
    if (FCGX_InitRequest(&f_req, 0, 0))
    {
        /* TODO: Debugging */
        return -1;
    }

    while (FCGX_Accept_r(&f_req) == 0)
    {
        const vla_request *req = request_new(ctx, &f_req);
        if (req == NULL)
        {
            /* TODO error logging. */
            goto error;
        }
        enum vla_handle_code code = vla_request_next_func(req);

        if (code & VLA_RESPOND_FLAG)
        {
            if (send_response(&f_req, req))
            {
                /* TODO Error logging */
                goto error;
            }
        }

        talloc_free((vla_request *)req);
        FCGX_Finish_r(&f_req);

        if (!(code & VLA_ACCEPT_FLAG))
        {
            break;
        }
    }

    FCGX_Free(&f_req, 0);

    return 0;

error:
    FCGX_Finish_r(&f_req);
    FCGX_Free(&f_req, 0);
    return -1;
}
