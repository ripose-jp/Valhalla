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
#include <time.h>
#include <stdio.h>

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

    /* A hash map of cookie names and values. */
    khash_t(str) *cookie_map;

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
    const route_info_t *info;

    /* The current array index into the middleware array. */
    size_t mw_i;
} vla_request_private;

/* An struct for managing header values. */
typedef struct header_value_array
{
    /* The number of elements in the array. */
    size_t size;

    /* The array. The maximum capacity can be found with talloc_array_length. */
    char **arr;
} header_array;

/*
 *==============================================================================
 * Private
 *==============================================================================
 */

/**
 * Initializes a header_array.
 *
 * @param ctx The talloc context this array should be a child of.
 *
 * @param cap The initial capacity of the array.
 *
 * @return A new header_value_array with an initial capacity of cap. NULL on
 *         error.
 */
static header_array *init_header_array(void *ctx, size_t cap)
{
    header_array *ha = talloc(ctx, header_array);
    if (ha == NULL)
    {
        return NULL;
    }
    ha->size = 0;
    ha->arr = talloc_array(ha, char *, cap);
    if (ha->arr == NULL)
    {
        talloc_free(ha);
        return NULL;
    }
    return ha;
}

/**
 * Appends a value to the array.
 *
 * @param ha The header_array to push a value onto.
 *
 * @param val The value to push onto the array. Must be talloc allocated.
 *            Takes ownership.
 *
 * @return 0 on success, -1 if memory cannot be allocated.
 */
static int header_array_push(header_array *ha, char *val)
{
    size_t cap = talloc_array_length(ha->arr);
    if (ha->size >= cap)
    {
        char **arr = talloc_realloc(ha, ha->arr, char *, cap * 2);
        if (arr == NULL)
        {
            return -1;
        }
        ha->arr = arr;
    }
    talloc_steal(ha->arr, val);
    ha->arr[ha->size++] = val;
    return 0;
}

/**
 * Replaces the value at an index.
 *
 * @param ha The header array to replace the value in.
 *
 * @param i The index of the value.
 *
 * @param val The value to insert. Must be talloc allocated. Takes ownership.
 */
static void header_array_replace(header_array *ha, size_t i, char *val)
{
    talloc_free(ha->arr[i]);
    talloc_steal(ha->arr, val);
    ha->arr[i] = val;
}

/**
 * Removes the value at index i from the array and frees it.
 *
 * @param ha The header array to remove a value from.
 *
 * @param i The index of the item to remove. Must be less than size.
 */
static void header_array_remove(header_array *ha, size_t i)
{
    assert(i < ha->size);
    talloc_free(ha->arr[i]);
    --ha->size;
    while (i < ha->size)
    {
        ha->arr[i] = ha->arr[i + 1];
        ++i;
    }
}

/**
 * Removes all values from the header array and deallocates memory.
 *
 * @param ha The header array to remove values from.
 */
static void header_array_clear(header_array *ha)
{
    for (size_t i = 0; i < ha->size; ++i)
    {
        talloc_free(ha->arr[i]);
    }
    ha->size = 0;
}

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
    kh_destroy(str, req->priv->cookie_map);
    sdsfree(req->priv->res_body);
    return 0;
}

/**
 * Populates the query string map.
 *
 * @param req The vla_request to add the formatted values to.
 *
 * @param query The raw query string.
 *
 * @return 0 on success, -1 on error.
 */
static int pop_query_map(vla_request *req, const char *query)
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

        char *t_key = su_url_decode_l(req, key, keylen);
        if (t_key == NULL)
        {
            return -1;
        }
        char *t_val = su_url_decode_l(req, val, vallen);
        if (t_val == NULL)
        {
            talloc_free(t_key);
            return -1;
        }

        int ret = 0;
        khiter_t it = kh_put(str, map, t_key, &ret);
        switch (ret)
        {
        case 0: // Key already exists
            talloc_free(t_key);
            talloc_free(kh_val(map, it));
            break;

        case 1: // Key doesn't exist
        case 2: // Key did exist, since deleted
            kh_key(map, it) = t_key;
            break;

        case -1: // Error
        default:
            talloc_free(t_key);
            talloc_free(t_val);
            /* TODO: Logging */
            return -1;
        }
        kh_val(map, it) = t_val;

        query = val + vallen;
        if (*query == '&')
        {
            query += 1;
        }
    }

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
 * @param[out] ind The index of the added header. Can be NULL.
 *
 * @return 0 on success, -1 on error.
 */
static int header_add(
    vla_request *req,
    khash_t(strcase) *map,
    const char *header,
    const char *value,
    size_t *ind)
{
    int ret;
    khiter_t it = kh_put(strcase, map, header, &ret);
    switch (ret)
    {
    case 0: // Key exists
        break;

    case 1: // Key doesn't exist
    case 2: // Key did exist, doesn't anymore
    {
        char *t_key = su_tstrdup(req, header);
        if (t_key == NULL)
        {
            kh_del(strcase, map, it);
            return -1;
        }
        kh_key(map, it) = t_key;

        header_array *ha = init_header_array(t_key, 1);
        if (ha == NULL)
        {
            talloc_free(t_key);
            kh_del(strcase, map, it);
            return -1;
        }
        kh_val(map, it) = ha;
        break;
    }

    default: // Error
        return -1;
    }

    header_array *ha = kh_val(map, it);
    char *t_val = su_tstrdup((void *)kh_key(map, it), value);
    if (t_val == NULL)
    {
        return -1;
    }
    header_array_push(ha, t_val);
    if (ind)
    {
        *ind = ha->size - 1;
    }

    return 0;
}

/**
 * Removes all header values for the supplied header from the map.
 *
 * @param map The map to delete the header from.
 *
 * @param header The header to delete. Not case sensative.
 *
 * @return 0 on success, -1 if the header doesn't exist.
 */
static int header_remove_all(khash_t(strcase) *map, const char *header)
{
    khiter_t it = kh_get(strcase, map, header);
    if (it == kh_end(map))
    {
        return -1;
    }

    talloc_free((char *)kh_key(map, it));
    kh_del(strcase, map, it);

    return 0;
}

/**
 * Removes a specific header and value from the map.
 *
 * @param map The map to delete the header from.
 *
 * @param header The header to delete. Not case sensative.
 *
 * @param i The index of the header to remove.
 *
 * @return 0 on success, -1 if the header/value doesn't exist.
 */
static int header_remove(
    khash_t(strcase) *map,
    const char *header,
    size_t i)
{
    khiter_t it = kh_get(strcase, map, header);
    if (it == kh_end(map))
    {
        return -1;
    }

    header_array *ha = kh_val(map, it);
    if (i >= ha->size)
    {
        return -1;
    }
    header_array_remove(ha, i);

    /* If that was the only value associated with this header, free the header
     * as well. */
    if (ha->size == 0)
    {
        talloc_free((char *)kh_key(map, it));
        kh_del(strcase, map, it);
    }

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
    if (val == NULL)
    {
        /* TODO: error logging */
        return -1;
    }
    val += 1;

    /* Duplicate the header value. */
    char header[val - envstr];
    for (size_t i = 0; i < sizeof(header) - 1; ++i)
    {
        header[i] = envstr[i] == '_' ? '-' : envstr[i];
    }
    header[sizeof(header) - 1] = '\0';

    return header_add(req, req->priv->req_hdr_map, header, val, NULL);
}

/**
 * Adds cookies from the 'Cookie' header.
 *
 * @param req The request to get the 'Cookie' header from and populate with
 *            values.
 *
 * @return 0 on success, -1 otherwise.
 */
static int request_add_cookies(vla_request *req)
{
    const char *cookies = vla_request_header_get(req, "Cookie");
    if (cookies == NULL)
    {
        return 0;
    }

    while (*cookies)
    {
        const char *name = cookies;
        const char *name_end = su_strchrnul(name, '=');
        size_t name_len = name_end - name;

        if (*name_end == '\0')
        {
            return -1;
        }

        const char *value = name_end + 1;
        const char *value_end = su_strchrnul(value, ';');
        size_t value_len = value_end - value;

        char *t_name = su_tstrndup(req, name, name_len);
        if (t_name == NULL)
        {
            return -1;
        }
        char *t_value = su_tstrndup(req, value, value_len);
        if (t_value == NULL)
        {
            talloc_free(t_name);
            return -1;
        }

        int ret;
        khiter_t it = kh_put(str, req->priv->cookie_map, t_name, &ret);
        switch (ret)
        {
        case -1: // Error
            talloc_free((char *)t_name);
            talloc_free((char *)t_value);
            return -1;

        case 0: // Cookie already exists
            talloc_free((char *)kh_key(req->priv->cookie_map, it));
            talloc_free((char *)kh_val(req->priv->cookie_map, it));
        case 1: // Cookie doesn't exist
        case 2: // Cookie was deleted
            kh_key(req->priv->cookie_map, it) = t_name;
            kh_val(req->priv->cookie_map, it) = t_value;
            break;
        }

        cookies = value_end;
        if (*cookies == ';')
        {
            ++cookies;
            while (*cookies && *cookies == ' ')
            {
                ++cookies;
            }
        }
    }

    return 0;
}

/**
 * Fills the fields a vla_request with the relevant request information.
 *
 * @param ctx The vla_context attatched to this request.
 *
 * @param req The request to populate the fields of.
 *
 * @return 0 on success, -1 on error.
 */
static int request_populate(vla_context *ctx, vla_request *req)
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
                return -1;
            }
        }
        else if (HAS_PREFIX(*str, QUERY_STRING))
        {
            req->query_str = val;
            if (pop_query_map(req, val))
            {
                /* TODO error logging */
                return -1;
            }
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
    if (request_add_cookies(req))
    {
        /* TODO: Logging. */
        return -1;
    }

    return 0;
}

/*
 *==============================================================================
 * Private API
 *==============================================================================
 */

const vla_request *request_new(vla_context *ctx, FCGX_Request *f_req)
{
    static const vla_middleware_func no_middleware[] = {NULL};
    static const void *no_middleware_args[] = {NULL};

    vla_request *req = talloc(ctx, vla_request);
    if (req == NULL)
    {
        return NULL;
    }
    talloc_set_destructor(req, request_destructor);
    talloc_set_name_const(req, "Request not yet processed");
    bzero(req, sizeof(vla_request));

    req->priv = talloc(req, vla_request_private);
    if (req->priv == NULL)
    {
        /* TODO Logging */
        talloc_free(req);
        return NULL;
    }
    *req->priv = (vla_request_private) {
        .f_req = f_req,

        .req_hdr_map = kh_init(strcase),
        .query_map = kh_init(str),
        .cookie_map = kh_init(str),
        .req_body = NULL,
        .req_body_len = 0,

        .res_status = 0,
        .res_hdr_map = kh_init(strcase),
        .res_body = sdsempty(),

        .mw_i = 0,
    };
    if (req->priv->req_hdr_map == NULL ||
        req->priv->query_map == NULL ||
        req->priv->cookie_map == NULL ||
        req->priv->res_hdr_map == NULL ||
        req->priv->res_body == NULL)
    {
        /* TODO Logging */
        talloc_free(req);
        return NULL;
    }
    if (request_populate(ctx, req))
    {
        /* TODO Logging */
        talloc_free(req);
        return NULL;
    }
    req->priv->info = context_get_route(ctx, req->document_uri, req->method);
    if (vla_response_set_status_code(req, 200))
    {
        /* TODO error logging */
        talloc_free(req);
        return NULL;
    }

    const char *name = talloc_set_name(
        req, "Request from %s:%s", req->remote_addr, req->remote_port
    );
    if (name == NULL)
    {
        /* TODO error logging */
        return NULL;
    }

    return req;
}

int response_header_iterate(
    const vla_request *req,
    int (*callback)(const char *, const char *, void *),
    void *arg)
{
    khash_t(strcase) *map = req->priv->res_hdr_map;
    for (khiter_t it = 0; it < kh_end(map); ++it)
    {
        if (!kh_exist(map, it))
        {
            continue;
        }
        header_array *ha = kh_val(map, it);
        for (size_t i = 0; i < ha->size; ++i)
        {
            if (callback(kh_key(map, it), ha->arr[i], arg))
            {
                return -1;
            }
        }
    }
    return 0;
}

const char *response_get_body(const vla_request *req)
{
    return req->priv->res_body;
}

size_t response_get_body_length(const vla_request *req)
{
    if (req->priv->res_body == NULL)
    {
        return 0;
    }
    return sdslen(req->priv->res_body);
}

/*
 *==============================================================================
 * Request
 *==============================================================================
 */

const char *vla_request_query_get(const vla_request *req, const char *key)
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
    const vla_request *req,
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

const char *vla_request_header_get(const vla_request *req, const char *header)
{
    khiter_t it = kh_get(strcase, req->priv->req_hdr_map, header);
    if (it == kh_end(req->priv->req_hdr_map))
    {
        return NULL;
    }
    header_array *ha = kh_val(req->priv->req_hdr_map, it);
    return ha->arr[0];
}

int vla_request_header_iterate(
    const vla_request *req,
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

const char *vla_request_cookie_get(const vla_request *req, const char *name)
{
    khiter_t it = kh_get(str, req->priv->cookie_map, name);
    if (it == kh_end(req->priv->cookie_map))
    {
        return NULL;
    }
    return kh_val(req->priv->cookie_map, it);
}

int vla_request_cookie_iterate(
    const vla_request *req,
    int (*callback)(const char *, const char *, void *),
    void *arg)
{
    khash_t(str) *map = req->priv->cookie_map;
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

const char *vla_request_body_get(const vla_request *req, size_t size)
{
    if (size == 0)
    {
        size = req->content_length;
    }
    vla_request_private *priv = req->priv;

    if (!priv->req_body)
    {
        priv->req_body = talloc_array(req, char, size + 1);
        if (priv->req_body == NULL)
        {
            return NULL;
        }
        priv->req_body_len = FCGX_GetStr(priv->req_body, size, priv->f_req->in);
        priv->req_body[priv->req_body_len] = '\0';
    }
    return priv->req_body;
}

size_t vla_request_body_get_length(const vla_request *req)
{
    return req->priv->req_body_len;
}

size_t vla_request_body_chunk(const vla_request *req, void *buffer, size_t cap)
{
    FCGX_Request *f_req = req->priv->f_req;
    char *buf = buffer;
    return FCGX_GetStr(buf, cap, f_req->in);
}

const char *vla_request_getenv(const vla_request *req, const char *var)
{
    return FCGX_GetParam(var, req->priv->f_req->envp);
}

int vla_request_env_iterate(
    const vla_request *req,
    int (*callback)(const char *, const char *, void *),
    void *arg)
{
    for (const char **str = (const char **)req->priv->f_req->envp; *str; ++str)
    {
        const char *val = strchr(*str, '=') + 1;
        assert(val != NULL + 1);
        size_t key_len = val - *str - 1;
        char *key = talloc_array(req, char, key_len + 1);
        if (key == NULL)
        {
            return -1;
        }
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

enum vla_handle_code vla_request_next_func(const vla_request *req)
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
    return VLA_HANDLE_IGNORE_ACCEPT;
}

/*
 *==============================================================================
 * Response
 *==============================================================================
 */

int vla_response_header_add(
    const vla_request *req,
    const char *header,
    const char *value,
    size_t *ind)
{
    return header_add((void *)req, req->priv->res_hdr_map, header, value, ind);
}

int vla_response_header_replace(
    const vla_request *req,
    const char *header,
    const char *value,
    size_t i)
{
    khash_t(strcase) *map = req->priv->res_hdr_map;

    khiter_t it = kh_get(strcase, map, header);
    if (it == kh_end(map))
    {
        return -1;
    }

    header_array *ha = kh_val(map, it);
    if (i >= ha->size)
    {
        return -1;
    }
    char *t_val = su_tstrdup(NULL, value);
    if (t_val == NULL)
    {
        return -1;
    }
    header_array_replace(ha, i, t_val);;

    return 0;
}

int vla_response_header_replace_all(
    const vla_request *req,
    const char *header,
    const char *value)
{
    khash_t(strcase) *map = req->priv->res_hdr_map;

    khiter_t it = kh_get(strcase, map, header);
    if (it == kh_end(map))
    {
        return vla_response_header_add(req, header, value, NULL);
    }

    header_array *ha = kh_val(map, it);
    header_array_clear(ha);
    char *t_val = su_tstrdup(NULL, value);
    if (t_val == NULL)
    {
        /* TODO Error Logging */
        return -1;
    }
    if (header_array_push(ha, t_val))
    {
        /* TODO Error Logging */
        return -1;
    }

    return 0;
}

int vla_response_header_remove(
    const vla_request *req,
    const char *header,
    size_t i)
{
    return header_remove(req->priv->res_hdr_map, header, i);
}

int vla_response_header_remove_all(const vla_request *req, const char *header)
{
    return header_remove_all(req->priv->res_hdr_map, header);
}

const char *vla_response_header_get(
    const vla_request *req,
    const char *header,
    size_t i)
{
    khiter_t it = kh_get(strcase, req->priv->res_hdr_map, header);
    if (it == kh_end(req->priv->res_hdr_map))
    {
        return NULL;
    }
    header_array *ha = kh_val(req->priv->res_hdr_map, it);
    if (i >= ha->size)
    {
        return NULL;
    }
    return su_tstrdup((void *)req, ha->arr[i]);
}

size_t vla_response_header_count(const vla_request *req, const char *header)
{
    khiter_t it = kh_get(strcase, req->priv->res_hdr_map, header);
    if (it == kh_end(req->priv->res_hdr_map))
    {
        return 0;
    }
    header_array *ha = kh_val(req->priv->res_hdr_map, it);
    return ha->size;
}

int vla_response_set_status_code(const vla_request *req, unsigned int code)
{
    req->priv->res_status = code;
    char buf[33];
    snprintf(buf, sizeof(buf) - 1, "%u", code);
    buf[sizeof(buf) - 1] = '\0';
    return vla_response_header_replace_all(req, "Status", buf);
}

unsigned int vla_response_get_status_code(const vla_request *req)
{
    return req->priv->res_status;
}

int vla_response_set_content_type(const vla_request *req, const char *type)
{
    return vla_response_header_replace_all(req, "Content-Type", type);
}

const char *vla_response_get_content_type(const vla_request *req)
{
    return vla_response_header_get(req, "Content-Type", 0);
}

int vla_response_set_cookie(const vla_request *req, const vla_cookie_t *cookie)
{
    if (cookie->name == NULL || cookie->value == NULL)
    {
        return -1;
    }

    sds buf = sdsempty();
    if (buf == NULL)
    {
        return -1;
    }
    buf = sdscatfmt(buf, "%s=%s", cookie->name, cookie->value);
    if (buf == NULL)
    {
        return -1;
    }
    if (cookie->expires)
    {
        struct tm utc;
        gmtime_r(&cookie->expires, &utc);
        char timestr[1024];
        strftime(timestr, sizeof(timestr), "%a, %d %b %Y %H:%M:%S %Z", &utc);
        timestr[sizeof(timestr) - 1] = '\0';

        buf = sdscatfmt(buf, "; Expires=%s", timestr);
        if (buf == NULL)
        {
            return -1;
        }
    }
    if (cookie->maxage)
    {
        buf = sdscatfmt(buf, "; Max-Age=%U", cookie->maxage);
        if (buf == NULL)
        {
            return -1;
        }
    }
    if (cookie->domain)
    {
        buf = sdscatfmt(buf, "; Domain=%s", cookie->domain);
        if (buf == NULL)
        {
            return -1;
        }
    }
    if (cookie->path)
    {
        buf = sdscatfmt(buf, "; Path=%s", cookie->path);
        if (buf == NULL)
        {
            return -1;
        }
    }
    if (cookie->secure)
    {
        buf = sdscat(buf, "; Secure");
        if (buf == NULL)
        {
            return -1;
        }
    }
    if (cookie->httponly)
    {
        buf = sdscat(buf, "; HttpOnly");
        if (buf == NULL)
        {
            return -1;
        }
    }
    if (cookie->samesite)
    {
        buf = sdscatfmt(buf, "; SameSite=%s", cookie->samesite);
        if (buf == NULL)
        {
            return -1;
        }
    }
    int ret = vla_response_header_add(req, "Set-Cookie", buf, NULL);
    sdsfree(buf);
    return ret;
}

int vla_printf(const vla_request *req, const char *fmt, ...)
{
    if (req->priv->res_body == NULL)
    {
        return -1;
    }
    va_list ap;
    va_start(ap, fmt);
    req->priv->res_body = sdscatvprintf(req->priv->res_body, fmt, ap);
    va_end(ap);
    return 0;
}

int vla_puts(const vla_request *req, const char *s)
{
    if (req->priv->res_body == NULL)
    {
        return -1;
    }
    req->priv->res_body = sdscat(req->priv->res_body, s);
    return 0;
}

int vla_putf(const vla_request *req, const char *path, int bin)
{
    if (req->priv->res_body == NULL)
    {
        return -1;
    }

    FILE *f = fopen(path, bin ? "rb" : "r");
    if (f == NULL)
    {
        return 1;
    }

    char buf[BUFSIZ];
    size_t read = 0;
    do
    {
        read = fread(buf, sizeof(char), BUFSIZ, f);
        req->priv->res_body = sdscatlen(req->priv->res_body, buf, read);
        if (req->priv->res_body == NULL)
        {
            fclose(f);
            return -1;
        }
    } while (read == BUFSIZ);

    if (!feof(f))
    {
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

int vla_write(const vla_request *req, const char *data, size_t len)
{
    if (req->priv->res_body == NULL)
    {
        return -1;
    }
    req->priv->res_body = sdscatlen(req->priv->res_body, data, len);
    if (req->priv->res_body == NULL)
    {
        return -1;
    }
    return 0;
}

int vla_eprintf(const vla_request *req, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (FCGX_VFPrintF(req->priv->f_req->err, fmt, ap) < 0)
    {
        return -1;
    }
    va_end(ap);
    return 0;
}

int vla_eputs(const vla_request *req, const char *s)
{
    if (FCGX_PutS(s, req->priv->f_req->err) < 0)
    {
        return -1;
    }
    return 0;
}