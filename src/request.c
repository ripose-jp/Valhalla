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

#include "request.h"

#include <assert.h>

#include <fcgiapp.h>
#include <talloc.h>

#include "buffer/sds.h"
#include "containers/strcasemap.h"
#include "containers/strmap.h"
#include "context.h"
#include "strutil.h"

/**
 * Checks if a string has a prefix.
 *
 * @param __str The string to check the prefix of.
 *
 * @param __prefix A string literal prefix.
 *
 * @return 1 if true, 0 otherwise.
 */
#define HAS_PREFIX(__str, __prefix) \
    (strncmp(__str, __prefix, sizeof(__prefix) - 1) == 0)

#define HTTP_HEADER "HTTP_"

#define QUERY_STRING "QUERY_STRING="
#define REQUEST_METHOD "REQUEST_METHOD="
#define CONTENT_TYPE "CONTENT_TYPE="
#define CONTENT_LENGTH "CONTENT_LENGTH="

#define SCRIPT_NAME "SCRIPT_NAME="
#define REQUEST_URI "REQUEST_URI="
#define DOCUMENT_URI "DOCUMENT_URI="
#define DOCUMENT_ROOT "DOCUMENT_ROOT="
#define SERVER_PROTOCOL "SERVER_PROTOCOL="
#define REQUEST_SCHEME "REQUEST_SCHEME="

#define GATEWAY_INTERFACE "GATEWAY_INTERFACE="
#define SERVER_SOFTWARE "SERVER_SOFTWARE="

#define REMOTE_ADDR "REMOTE_ADDR="
#define REMOTE_PORT "REMOTE_PORT="
#define SERVER_ADDR "SERVER_ADDR="
#define SERVER_PORT "SERVER_PORT="
#define SERVER_NAME "SERVER_NAME="

typedef struct vla_request_private
{
    /* The FastCGI request tied to this request. */
    FCGX_Request *f_req;

    //////////////////
    // Request Info //
    //////////////////

    /* A hash map of HTTP request headers. */
    khash_t(strcase) *req_hdr_map;

    /* A hash map of query string key and values. */
    khash_t(str) *query_map;

    /* Body of the request. */
    char *req_body;

    /* The length of the request body. */
    size_t req_body_len;

    ///////////////////
    // Response Info //
    ///////////////////

    /* The status code. */
    unsigned int res_status;

    /* A map of HTTP response headers. */
    khash_t(strcase) *res_hdr_map;

    /* Body buffer. */
    sds res_body;

    //////////////
    // Handlers //
    //////////////

    /* The handlers for the current request. */
    route_info_t *info;

    /* The current array index into the middleware array. */
    size_t mw_i;
} vla_request_private;

/*
 *==============================================================================
 * Private
 *==============================================================================
 */

/**
 * Destructor for vla_request.
 *
 * @param req The vla_request to destruct.
 *
 * @return 0 on success, -1 on failure.
 */
static int request_destructor(vla_request *req)
{
    kh_destroy(strcase, req->priv->res_hdr_map);
    kh_destroy(strcase, req->priv->req_hdr_map);
    kh_destroy(str, req->priv->query_map);
    sdsfree(req->priv->res_body);
    return 0;
}

/**
 * Populates the query string map.
 *
 * @param req The vla_request to add the formatted values to.
 *
 * @param query The raw query string.
 */
static void pop_query_map(vla_request *req, const char *query)
{
    khash_t(str) *map = req->priv->query_map;

    while (*query)
    {
        const char *key = query;
        const char *val = strchr(key, '=');
        if (!val)
        {
            break;
        }
        val += 1;

        size_t keylen = val - key - 1;
        const char *valendptr = su_strchrnul(val, '&');
        size_t vallen = valendptr - val;

        char *t_key = su_url_decode_l(key, keylen);
        talloc_steal(req, t_key);

        char *t_val = su_url_decode_l(val, vallen);
        talloc_steal(req, t_val);

        int ret = 0;
        khiter_t it = kh_put(str, map, t_key, &ret);
        switch (ret)
        {
        case 0: // Key already exists
            talloc_free(t_key);
            talloc_free(kh_val(map, it));
            break;

        case 1: // Key doesn't exist
            kh_key(map, it) = t_key;
            break;

        case -1: // Error
            talloc_free(t_key);
            talloc_free(t_val);
            /* TODO: Logging */
            return;
        }
        kh_val(map, it) = t_val;

        query = val + vallen;
        if (*query == '&')
        {
            query += 1;
        }
    }
}

/**
 * Inserts a header into the hash table. Replaces it if it already exists.
 *
 * @param req The request associated with this. Used for memory management.
 *
 * @param map The map to append the header to.
 *
 * @param header The header to add. Not case sensative. Copied to the heap, so
 *               this function does NOT take ownership.
 *
 * @param value The header value to add. Copied to the heap, so this function
 *              does NOT take ownership.
 *
 * @return 0 on success, -1 on failure.
 */
static int header_insert(
    vla_request *req,
    khash_t(strcase) *map,
    const char *header,
    const char *value)
{
    int ret = 0;
    khiter_t it = kh_put(strcase, map, header, &ret);
    switch (ret)
    {
    case 0: // Key exists
        talloc_free(kh_val(map, it));
        break;

    case 1: // Key doesn't exist
        char *key = talloc_array(req, char, strlen(header) + 1);
        strcpy(key, header);
        kh_key(map, it) = key;
        break;

    case -1: // Error
        return -1;
    }

    char *val = talloc_array(req, char, strlen(value) + 1);
    strcpy(val, value);
    kh_val(map, it) = val;

    return 0;
}

/**
 * Appends a header to a map. Inserts it if it doesn't exist.
 *
 * @param req The request associated with this. Used for memory management.
 *
 * @param map The map to append the header value to.
 *
 * @param header The header to append to. Not case sensative. Copied to the
 *               heap, so this function does NOT take ownership.
 *
 * @param value The header value to append. Copied to the heap, so this function
 *              does NOT take ownership.
 *
 * @return 0 on success, -1 on error.
 */
static int header_append(
    vla_request *req,
    khash_t(strcase) *map,
    const char *header,
    const char *value)
{
    int ret;
    khiter_t it = kh_put(strcase, map, header, &ret);
    switch (ret)
    {
    case 0: // Key exists
    {
        char *val = kh_val(map, it);
        size_t old_val_len = talloc_array_length(val);
        size_t new_val_len = old_val_len + strlen(value) + 1;
        val = talloc_realloc(req, val, char, new_val_len);
        val[old_val_len - 1] = ',';
        strcpy(&val[old_val_len], value);
        return 0;
    }

    case 1: // Key doesn't exist
    {
        char *key = talloc_array(req, char, strlen(header) + 1);
        strcpy(key, header);
        kh_key(map, it) = key;

        char *val = talloc_array(req, char, strlen(value) + 1);
        strcpy(val, value);
        kh_key(map, it) = val;
        return 0;
    }
    }

    return -1;
}

/**
 * Deletes a header from the map.
 *
 * @param map The map to delete the header from.
 *
 * @param header The header to delete. Not case sensative.
 *
 * @return 0 on success, -1 if the header doesn't exist.
 */
static int header_delete(khash_t(strcase) *map, const char *header)
{
    khiter_t it = kh_get(strcase, map, header);
    if (it == kh_end(map))
    {
        return -1;
    }

    const char *key = kh_key(map, it);
    char *val = kh_val(map, it);

    kh_del(strcase, map, it);

    talloc_free((char *)key);
    talloc_free(val);

    return 0;
}

/**
 * Adds an HTTP request header to the header map. If a header is duplicated,
 * creates a comma seperated list.
 *
 * @param req The request to add the header to.
 *
 * @param envstr The environment string of the form 'HTTP_{NAME}={VAL}'
 *
 * @return 0 on success, -1 otherwise.
 */
static int request_add_header(vla_request *req, const char *envstr)
{
    envstr += sizeof(HTTP_HEADER) - 1;

    /* Get the value of the header. */
    const char *val = strchr(envstr, '=');
    if (!val)
    {
        /* TODO: error logging */
    }
    val += 1;

    /* Duplicate the header value. */
    char header[val - envstr];
    for (size_t i = 0; i < sizeof(header) - 1; ++i)
    {
        header[i] = envstr[i] == '_' ? '-' : envstr[i];
    }
    header[sizeof(header) - 1] = '\0';

    return header_insert(req, req->priv->req_hdr_map, header, val);
}

/**
 * Fills the fields a vla_request with the relevant request information.
 *
 * @param ctx The vla_context attatched to this request.
 *
 * @param req The request to populate the fields of.
 */
static void request_populate(vla_context *ctx, vla_request *req)
{
    for (const char **str = (const char **)req->priv->f_req->envp; *str; ++str)
    {
        const char *val = strchr(*str, '=') + 1;
        assert(val != NULL + 1);
        if (HAS_PREFIX(*str, HTTP_HEADER))
        {
            if (request_add_header(req, *str))
            {
                /* TODO error logging */
            }
        }
        else if (HAS_PREFIX(*str, QUERY_STRING))
        {
            req->query_str = val;
            pop_query_map(req, val);
        }
        else if (HAS_PREFIX(*str, REQUEST_METHOD))
        {
            if (strcasecmp(val, "GET") == 0)
            {
                req->method = VLA_HTTP_GET;
            }
            else if (strcasecmp(val, "HEAD") == 0)
            {
                req->method = VLA_HTTP_HEAD;
            }
            else if (strcasecmp(val, "POST") == 0)
            {
                req->method = VLA_HTTP_POST;
            }
            else if (strcasecmp(val, "PUT") == 0)
            {
                req->method = VLA_HTTP_PUT;
            }
            else if (strcasecmp(val, "DELTE") == 0)
            {
                req->method = VLA_HTTP_DELETE;
            }
            else if (strcasecmp(val, "CONNECT") == 0)
            {
                req->method = VLA_HTTP_CONNECT;
            }
            else if (strcasecmp(val, "OPTIONS") == 0)
            {
                req->method = VLA_HTTP_OPTIONS;
            }
            else if (strcasecmp(val, "TRACE") == 0)
            {
                req->method = VLA_HTTP_TRACE;
            }
            else if (strcasecmp(val, "PATCH") == 0)
            {
                req->method = VLA_HTTP_PATCH;
            }
            else
            {
                req->method = VLA_HTTP_UNKNOWN;
            }
        }
        else if (HAS_PREFIX(*str, CONTENT_TYPE))
        {
            req->content_type = val;
        }
        else if (HAS_PREFIX(*str, CONTENT_LENGTH))
        {
            int res = sscanf(val, "%zu", &req->content_length);
            if (res != 1)
            {
                req->content_length = 0;
            }
        }
        else if (HAS_PREFIX(*str, SCRIPT_NAME))
        {
            req->script_name = val;
        }
        else if (HAS_PREFIX(*str, REQUEST_URI))
        {
            req->request_uri = val;
        }
        else if (HAS_PREFIX(*str, DOCUMENT_URI))
        {
            req->document_uri = val;
        }
        else if (HAS_PREFIX(*str, DOCUMENT_ROOT))
        {
            req->document_root = val;
        }
        else if (HAS_PREFIX(*str, SERVER_PROTOCOL))
        {
            req->server_protocol = val;
        }
        else if (HAS_PREFIX(*str, REQUEST_SCHEME))
        {
            req->request_scheme = val;
            req->https = strcasecmp(val, "HTTPS") == 0;
        }
        else if (HAS_PREFIX(*str, GATEWAY_INTERFACE))
        {
            req->gateway_interface = val;
        }
        else if (HAS_PREFIX(*str, SERVER_SOFTWARE))
        {
            req->server_software = val;
        }
        else if (HAS_PREFIX(*str, REMOTE_ADDR))
        {
            req->remote_addr = val;
        }
        else if (HAS_PREFIX(*str, REMOTE_PORT))
        {
            req->remote_port = val;
        }
        else if (HAS_PREFIX(*str, SERVER_ADDR))
        {
            req->server_addr = val;
        }
        else if (HAS_PREFIX(*str, SERVER_PORT))
        {
            req->server_port = val;
        }
        else if (HAS_PREFIX(*str, SERVER_NAME))
        {
            req->server_name = val;
        }
    }
}

/*
 *==============================================================================
 * Private API
 *==============================================================================
 */

vla_request *request_new(vla_context *ctx, FCGX_Request *f_req)
{
    static const vla_middleware_func no_middleware[] = {NULL};
    static const void *no_middleware_args[] = {NULL};

    vla_request *req = talloc(ctx, vla_request);
    talloc_set_destructor(req, request_destructor);
    talloc_set_name_const(req, "Request not yet processed");
    bzero(req, sizeof(vla_request));

    req->priv = talloc(req, vla_request_private);
    *req->priv = (vla_request_private) {
        .f_req = f_req,

        .req_hdr_map = kh_init(strcase),
        .query_map = kh_init(str),
        .req_body = NULL,
        .req_body_len = 0,

        .res_status = 0,
        .res_hdr_map = kh_init(strcase),
        .res_body = sdsempty(),

        .mw_i = 0,
    };
    request_populate(ctx, req);
    req->priv->info = context_get_route(ctx, req->document_uri, req->method);
    vla_response_set_status_code(req, 200);

    talloc_set_name(
        req, "Request from %s:%s", req->remote_addr, req->remote_port
    );

    return req;
}

int response_header_iterate(
    vla_request *req,
    int (*callback)(const char *, const char *, void *),
    void *arg)
{
    khash_t(strcase) *map = req->priv->res_hdr_map;
    for (khiter_t it = 0; it < kh_end(map); ++it)
    {
        if (kh_exist(map, it))
        {
            if (callback(kh_key(map, it), kh_val(map, it), arg))
            {
                return -1;
            }
        }
    }
    return 0;
}

const char *response_get_body(vla_request *req)
{
    return req->priv->res_body;
}

/*
 *==============================================================================
 * Request
 *==============================================================================
 */

const char *vla_request_query_get(vla_request *req, const char *key)
{
    khash_t(str) *map = req->priv->query_map;
    khiter_t it = kh_get(str, map, key);
    if (it == kh_end(map))
    {
        return NULL;
    }
    return kh_val(map, it);
}

int vla_request_query_iterate(
    vla_request *req,
    int (*callback)(const char *, const char *, void *),
    void *arg)
{
    khash_t(str) *map = req->priv->query_map;
    for (khiter_t it = 0; it < kh_end(map); ++it)
    {
        if (kh_exist(map, it))
        {
            if (callback(kh_key(map, it), kh_val(map, it), arg))
            {
                return 1;
            }
        }
    }
    return 0;
}

const char *vla_request_header_get(vla_request *req, const char *header)
{
    khiter_t it = kh_get(strcase, req->priv->req_hdr_map, header);
    if (it == kh_end(req->priv->req_hdr_map))
    {
        return NULL;
    }
    return kh_val(req->priv->req_hdr_map, it);
}

int vla_request_header_iterate(
    vla_request *req,
    int (*callback)(const char *, const char *, void *),
    void *arg)
{
    khash_t(strcase) *map = req->priv->req_hdr_map;
    for (khiter_t it = 0; it < kh_end(map); ++it)
    {
        if (kh_exist(map, it))
        {
            if (callback(kh_key(map, it), kh_val(map, it), arg))
            {
                return 1;
            }
        }
    }
    return 0;
}

const char *vla_request_body_get(vla_request *req, size_t size)
{
    if (size == 0)
    {
        size = req->content_length;
    }
    vla_request_private *priv = req->priv;

    if (!priv->req_body)
    {
        priv->req_body = talloc_array(req, char, size + 1);
        priv->req_body_len = FCGX_GetStr(priv->req_body, size, priv->f_req->in);
        priv->req_body[priv->req_body_len] = '\0';
    }
    return priv->req_body;
}

size_t vla_request_body_get_length(vla_request *req)
{
    return req->priv->req_body_len;
}

const char *vla_request_getenv(vla_request *req, const char *var)
{
    return FCGX_GetParam(var, req->priv->f_req->envp);
}

int vla_request_env_iterate(
    vla_request *req,
    int (*callback)(const char *, const char *, void *),
    void *arg)
{
    for (const char **str = (const char **)req->priv->f_req->envp; *str; ++str)
    {
        const char *val = strchr(*str, '=') + 1;
        assert(val != NULL + 1);
        size_t key_len = val - *str - 1;
        char *key = talloc_array(req, char, key_len + 1);
        strncpy(key, *str, key_len);
        key[key_len] = '\0';

        int ret = callback(key, val, arg);
        talloc_free(key);
        if (ret)
        {
            return 1;
        }
    }
    return 0;
}

enum vla_handle_code vla_request_next_func(vla_request *req)
{
    vla_request_private *priv = req->priv;
    if (priv->info == NULL)
    {
        /* TODO: Error logging. */
        return VLA_HANDLE_IGNORE_TERM;
    }

    vla_middleware_func mw_func = priv->info->mw[priv->mw_i];
    void *mw_arg = priv->info->mw_args[priv->mw_i];
    priv->mw_i++;
    if (mw_func)
    {
        return mw_func(req, mw_arg);
    }

    if (priv->info->hdlr)
    {
        return priv->info->hdlr(req, priv->info->hdlr_arg);
    }

    /* TODO: Error logging. */
    return VLA_HANDLE_IGNORE_TERM;
}

/*
 *==============================================================================
 * Response
 *==============================================================================
 */

int vla_response_header_insert(
    vla_request *req,
    const char *header,
    const char *value)
{
    return header_insert(req, req->priv->res_hdr_map, header, value);
}

int vla_response_header_append(
    vla_request *req,
    const char *header,
    const char *value)
{
    return header_append(req, req->priv->res_hdr_map, header, value);
}

int vla_response_header_remove(vla_request *req, const char *header)
{
    return header_delete(req->priv->res_hdr_map, header);
}

const char *vla_response_header_get(vla_request *req, const char *header)
{
    khiter_t it = kh_get(strcase, req->priv->res_hdr_map, header);
    if (it == kh_end(req->priv->res_hdr_map))
    {
        return NULL;
    }
    return kh_val(req->priv->res_hdr_map, it);
}

int vla_response_set_status_code(vla_request *req, unsigned int code)
{
    req->priv->res_status = code;
    char buf[33];
    snprintf(buf, sizeof(buf) - 1, "%u", code);
    buf[sizeof(buf) - 1] = '\0';
    return vla_response_header_insert(req, "Status", buf);
}

unsigned int val_response_get_status_code(vla_request *req)
{
    return req->priv->res_status;
}

int vla_response_set_content_type(vla_request *req, const char *type)
{
    return vla_response_header_insert(req, "Content-Type", type);
}

const char *vla_response_get_content_type(vla_request *req)
{
    return vla_response_header_get(req, "Content-Type");
}

void vla_printf(vla_request *req, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    req->priv->res_body = sdscatvprintf(req->priv->res_body, fmt, ap);
    va_end(ap);
}

void vla_puts(vla_request *req, const char *s)
{
    req->priv->res_body = sdscat(req->priv->res_body, s);
}

void vla_write(vla_request *req, const char *data, size_t len)
{
    req->priv->res_body = sdscatlen(req->priv->res_body, data, len);
}
