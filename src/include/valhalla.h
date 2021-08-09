////////////////////////////////////////////////////////////////////////////////
//
// Copyright (C) 2021 Ripose
//
// This file is part of Valhalla.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
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

#ifndef __VALHALLA_H__
#define __VALHALLA_H__

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*
 * Top-level context for Valhalla.
 */
typedef struct vla_context vla_context;

/*
 * Private instance variables needed for managing a vla_request.
 */
typedef struct vla_request_private vla_request_private;

/* Forward declaration. */
typedef struct vla_request vla_request;

/*
 * Function pointer type for handler functions.
 */
typedef enum vla_handle_code (*vla_handler_func)(const vla_request *, void *);

/*
 * Function pointer type for middleware functions.
 */
typedef enum vla_handle_code (*vla_middleware_func)(const vla_request *, void *);

/*
 * HTTP methods request methods.
 */
enum vla_http_method
{
    /* A value for error checking. */
    VLA_HTTP_UNKNOWN = 0,

    VLA_HTTP_GET     = 1,
    VLA_HTTP_HEAD    = 1 << 1,
    VLA_HTTP_POST    = 1 << 2,
    VLA_HTTP_PUT     = 1 << 3,
    VLA_HTTP_DELETE  = 1 << 4,
    VLA_HTTP_CONNECT = 1 << 5,
    VLA_HTTP_OPTIONS = 1 << 6,
    VLA_HTTP_TRACE   = 1 << 7,
    VLA_HTTP_PATCH   = 1 << 8,

    /* Used to mark all methods at once. */
    VLA_HTTP_ALL = 0xFFFFFFFF,
};

/*
 * Struct tied to the current web request.
 * Used for getting information about the request and sending a response.
 */
typedef struct vla_request
{
    //////////////////
    // Request Info //
    //////////////////

    /* Raw query string. */
    const char *query_str;

    /* The HTTP verb associated with this request. */
    enum vla_http_method method;

    /* The HTTP Content-Type header. */
    const char *content_type;

    /* The HTTP Content-Length header. 0 if not present. */
    size_t content_length;

    /* Name of the current executing script. */
    const char *script_name;

    /* The unfiltered location requested. */
    const char *request_uri;

    /* The filtered location requested. */
    const char *document_uri;

    /* The document root of the files being served. */
    const char *document_root;

    /* The HTTP protocol of the request. Usually HTTP/1.0, 1.1, or 2.0. */
    const char *server_protocol;

    /* The scheme of the request, HTTP or HTTPS. */
    const char *request_scheme;

    /* Indicates if the request is over HTTPS. */
    int https;

    /* Protocol through which Valhalla is interacting with the webserver. */
    const char *gateway_interface;

    /* The name of the webserver. */
    const char *server_software;

    /* Address the request originated from. */
    const char *remote_addr;

    /* Port he request originated from. */
    const char *remote_port;

    /* The address of the webserver. */
    const char *server_addr;

    /* The port the webserver is running on. */
    const char *server_port;

    /* The name of the webserver. */
    const char *server_name;

    ////////////////////
    // End Public API //
    ////////////////////

    /* Private variables that should not be accessed by API users directly. */
    vla_request_private *priv;
} vla_request;

/* vla_handle_code flag that indicates a response should be sent. */
#define VLA_RESPOND_FLAG 0x1

/* vla_handle_code flag that indicates another request should be accepted. */
#define VLA_ACCEPT_FLAG 0x2

/*
 * Different return values for handler and middleware functions.
 */
enum vla_handle_code
{
    /*
     * Return value from a request handler.
     * Indicates a response should be sent and the next request should be
     * accepted.
     */
    VLA_HANDLE_RESPOND_ACCEPT = VLA_RESPOND_FLAG | VLA_ACCEPT_FLAG,

    /*
     * Return value from a request handler.
     * Indicates a response should be sent and to stop accepting requests.
     */
    VLA_HANDLE_RESPOND_TERM = VLA_RESPOND_FLAG,

    /*
     * Return value from a request handler.
     * Indicates no response should be sent and the next request should be
     * accepted.
     */
    VLA_HANDLE_IGNORE_ACCEPT = VLA_ACCEPT_FLAG,

    /*
     * Return value from a request handler.
     * Indicates no response should be sent and to stop accepting requests.
     */
    VLA_HANDLE_IGNORE_TERM = 0,
};

/* Struct defining an HTTP cookie. */
typedef struct vla_cookie_t
{
    /* The name of the cookie. Must be non-NULL. */
    const char *name;

    /* The value of the cookie. Must be non-NULL. */
    const char *value;

    /* The value of the 'Expires' cookie attribute in UTC.
     * If 0, the Expires= attribute is not included.
     */
    time_t expires;

    /* The value of the 'Max-Age' cookie attribute in seconds.
     * If 0, the Max-Age= attribute is not included.
     */
    uint64_t maxage;

    /* The value of the 'Domain' cookie attribute.
     * Not included if the value is NULL.
     */
    const char *domain;

    /* The value of the 'Path' cookie attribute.
     * Not included if the value is NULL.
     */
    const char *path;

    /* Includes the 'Secure' attribute if nonzero. */
    int secure;

    /* Includes the 'HttpOnly' attribute if nonzero. */
    int httponly;

    /* The value of the 'SameSite' cookie attribute.
     * Not included if the value is NULL.
     */
    const char *samesite;
} vla_cookie_t;

/*
 *==============================================================================
 * General
 *==============================================================================
 */

/**
 * Creates a new Valhalla context.
 *
 * @return A vla_context. Should be freed by vla_free(). NULL on error.
 */
vla_context *vla_init();

/**
 * Frees dynamically allocated memory.
 *
 * @param ptr The memory that should be freed. No-ops on NULL.
 *
 * @return 0 on success, -1 on failure.
 */
int vla_free(void *ptr);

/**
 * Initializes a cookie to its default values. By default, nothing is included
 * and the name and value are NULL.
 *
 * No memory is dynamically allocated, so do not call vla_free on anything.
 *
 * @param[out] cookie The vla_cookie_t to initialize.
 */
void vla_init_cookie(vla_cookie_t *cookie);

/**
 * Adds a new route. Routes cannot be deleted.
 *
 * An example call to this function looks like:
 *
 *      vla_add_route(
 *          ctx,
 *          VLA_HTTP_POST | VLA_HTTP_PUT,
 *          "/book/*",
 *          &book_handler, &book_info,
 *          &auth, &auth_ctx,
 *          &inject_headers, NULL,
 *          NULL
 *      );
 *
 * This will add a route for handling POST and PUT requests to any URI that
 * matches '/book/*' by sending it to the user-defined 'book_handler' function,
 * passing in as the second argument to that function a pointer to 'book_info'.
 * The request will pass first through the 'auth' middleware function which will
 * have a pointer to 'auth_ctx' passed in as the second argument, then to the
 * 'inject_headers' middleware function which will have NULL passed in as the
 * second argument. The final NULL terminates the argument list.
 *
 * @param ctx The context of this Valhalla instance.
 *
 * @param methods The methods this route should respond to.
 *                HTTP verb flags defined in the vla_http_method enum.
 *
 * @param route The location of the route. If a route contains a ':', everything
 *              after the colon up to the next '/' (or end of the string if that
 *              comes first) is matched. If a route contains a '*', everything
 *              after that '*' is matched.
 *
 *
 * @param handler A function for handling an incoming request.
 *                The vla_request belongs to Valhalla. It is freed after the
 *                return of handler and before accepting the next request.
 *                Returns either VLA_HANDLE_RESPOND_ACCEPT,
 *                VLA_HANDLE_RESPOND_TERM, VLA_HANDLE_IGNORE_ACCEPT, or
 *                VLA_HANDLE_IGNORE_TERM.
 *
 * @param handler_arg The second argument to the handler function.
 *
 * @param ... An alternating list of vla_middleware_func and void * arguments,
 *            terminated by NULL pointer to a vla_middleware_func (NOT a void *)
 *            argument. The middleware and middleware_arg params describe these
 *            functions.
 *
 * @param middleware Middleware are handlers that intercept a request and
 *                   potentially prevent the handler function from being
 *                   reached.
 *                   The vla_request belongs to Valhalla. It is freed after the
 *                   return of this function and before the next request.
 *                   A middleware function takes in a vla_request, and returns
 *                   either VLA_HANDLE_RESPOND_ACCEPT, VLA_HANDLE_RESPOND_TERM,
 *                   VLA_HANDLE_IGNORE_ACCEPT, or VLA_HANDLE_IGNORE_TERM.
 *
 * @param middleware_arg The second argument to a middleware function. Assumed
 *                       to be a void *.
 *
 * @return 0 if the route was added, 1 if the route overlaps with another, -1 if
 *         the route doesn't start with '/'.
 */
int vla_add_route(
    vla_context *ctx,
    uint32_t methods,
    const char *route,
    vla_handler_func handler,
    void *handler_arg,
    ...);

/**
 * Sets the handler to handle requests when no matching route is found.
 *
 * @param ctx The context of this Valhalla instance.
 *
 * @param handler A function for handling an incoming request.
 *                The vla_request belongs to Valhalla. It is freed after the
 *                return of handler and before accepting the next request.
 *                Returns either VLA_HANDLE_RESPOND_ACCEPT,
 *                VLA_HANDLE_RESPOND_TERM, VLA_HANDLE_IGNORE_ACCEPT, or
 *                VLA_HANDLE_IGNORE_TERM.
 *
 * @param handler_arg The second argument to the handler function.
 *
 * @param ... An alternating list of vla_middleware_func and void * arguments,
 *            terminated by NULL pointer to a vla_middleware_func (NOT a void *)
 *            argument. The middleware and middleware_arg params describe these
 *            functions.
 *
 * @param middleware Middleware are handlers that intercept a request and
 *                   potentially prevent the handler function from being
 *                   reached.
 *                   The vla_request belongs to Valhalla. It is freed after the
 *                   return of this function and before the next request.
 *                   A middleware function takes in a vla_request, and returns
 *                   either VLA_HANDLE_RESPOND_ACCEPT, VLA_HANDLE_RESPOND_TERM,
 *                   VLA_HANDLE_IGNORE_ACCEPT, or VLA_HANDLE_IGNORE_TERM.
 *
 * @param middleware_arg The second argument to a middleware function. Assumed
 *                       to be a void *.
 *
 * @return 0 on success, -1 on memory error.
 */
int vla_set_not_found_handler(
    vla_context *ctx,
    vla_handler_func handler,
    void *handler_arg,
    ...);

/**
 * Accepts incoming web requests. Blocks while waiting.
 *
 * @param ctx The context containing the route information.
 *
 * @return 0 if every request was handled successfully, -1 otherwise.
 */
int vla_accept(vla_context *ctx);

/*
 *==============================================================================
 * Request
 *==============================================================================
 */

/**
 * Gets the value of the query string from the request.
 *
 * @param req The vla_request to get the query string from.
 *
 * @param key The key value of the query string.
 *
 * @return The value of the key. NULL if the key doesn't exist of the query
 *         string was malformed.
 */
const char *vla_request_query_get(const vla_request *req, const char *key);

/**
 * Iterates the query string values. Ordering is random.
 *
 * @param req The vla_request to iterate over.
 *
 * @param callback The function to call for each value. The first argument is
 *                 the key, and the second is the value. Returns 0 to continue
 *                 iterating, nonzero to stop.
 *
 * @param arg The third argument to the callback function.
 *
 * @return 0 if every value was iterated over, 1 otherwise.
 */
int vla_request_query_iterate(
    const vla_request *req,
    int (*callback)(const char *, const char *, void *),
    void *arg);

/**
 * Gets a request header.
 *
 * @param req The vla_request to get the header from.
 *
 * @param header The header to get the value of.
 *
 * @return The header if it exists, NULL otherwise. Belongs to the vla_request.
 */
const char *vla_request_header_get(const vla_request *req, const char *header);

/**
 * Iterates over request headers.
 *
 * @param req The vla_request to iterate over.
 *
 * @param callback The function to call for each value. The first argument is
 *                 the header, and the second is the value. Return 0 to
 *                 continue iterating, nonzero to stop.
 *
 * @param arg The third argument to the callback function.
 *
 * @return 0 if every value was iterated over, 1 otherwise.
 */
int vla_request_header_iterate(
    const vla_request *req,
    int (*callback)(const char *, const char *, void *),
    void *arg);

/**
 * Gets the value of the cookie with given name.
 *
 * @param req The request to get the cookie from.
 *
 * @param name The name of the cookie to get the value of.
 *
 * @return The value of the cookie if it exists, NULL otherwise. Belongs to the
 *         request. Will be freed upon the completion of the request.
 */
const char *vla_request_cookie_get(const vla_request *req, const char *name);

/**
 * Iterates over sent cookies.
 *
 * @param req The vla_request to iterate over.
 *
 * @param callback The function to call for each value. The first argument is
 *                 the cookie name, and the second is the value. Return 0 to
 *                 continue iterating, nonzero to stop.
 *
 * @param arg The third argument to the callback function.
 *
 * @return 0 if every value was iterated over, 1 otherwise.
 */
int vla_request_cookie_iterate(
    const vla_request *req,
    int (*callback)(const char *, const char *, void *),
    void *arg);

/**
 * Gets the request body.
 *
 * This method will only read the message body up to size on the first
 * call. Subsequent calls will return what portion of the body was already
 * read.
 *
 * If the body should be read in chunks, use vla_request_body_chunk instead. Do
 * not use both.
 *
 * @param req The vla_request to get the body from.
 *
 * @param size The maximum amount (in bytes) of the body that can be read. If
 *             size == 0, the size in the "Content-Length" header is used. If
 *             the Content-Length header is not present, the empty string is
 *             returned. On all calls after the first call, the size parameter
 *             is ignored.
 *
 * @return The body of the request up to size. The body is always nul terminated
 *         regardless of the "Content-Type" header. Belongs to the vla_request.
 *         NULL if the memory could not be allocated.
 */
const char *vla_request_body_get(const vla_request *req, size_t size);

/**
 * Gets the size of the request body from vla_request_body_get.
 *
 * @param req The request to get the body size from.
 *
 * @return The size of the request body, 0 if it hasn't been read yet or if it
 *         doesn't have a body.
 */
size_t vla_request_body_get_length(const vla_request *req);

/**
 * Reads the request body in chunks into a buffer. Output is not nul terminated.
 *
 * If the body should be read all at once, consider using vla_request_body_get.
 * Do not use both.
 *
 * @param req The request to get the body from.
 *
 * @param[out] buffer A pointer to the buffer to write data to.
 *
 * @param cap The size of the buffer in bytes.
 *
 * @return The number of bytes written to the buffer.
 */
size_t vla_request_body_chunk(const vla_request *req, void *buffer, size_t cap);

/**
 * Gets the environment variable for this request. Runs in O(n) time.
 *
 * @param req The current request.
 *
 * @param var The name of the environment variable.
 *
 * @return The value of the environment variable. NULL if it doesn't exist.
 *         Belongs to vla_request. Belongs to the vla_request.
 */
const char *vla_request_getenv(const vla_request *req, const char *var);

/**
 * Iterates over environment variables.
 *
 * @param req The vla_request to iterate over.
 *
 * @param callback The function to call for each value. The first argument is
 *                 the name of the environment variable, and the second is the
 *                 value. Returns 0 to continue iterating, nonzero to stop.
 *
 * @param arg The third argument to the callback function.
 *
 * @return 0 if every value was iterated over, 1 if iterating stopped
 *         prematurely, -1 on error.
 */
int vla_request_env_iterate(
    const vla_request *req,
    int (*callback)(const char *, const char *, void *),
    void *arg);

/**
 * Moves to the next function in the request chain.
 * This could be either a middleware or handler function.
 *
 * @param req The current request being handled.
 *
 * @return A vla_response_code.
 */
enum vla_handle_code vla_request_next_func(const vla_request *req);

/*
 *==============================================================================
 * Response
 *==============================================================================
 */

/**
 * Adds a response header. Multiple headers with different value can be added.
 * They will be sent in the response in the order they are added.
 *
 * @param req The request to add the response header to.
 *
 * @param header The name of the header. Not case sensative.
 *
 * @param value The header value to add.
 *
 * @param[out] ind The index of the added header. Can be NULL.
 *
 * @return 0 on success, -1 on failure.
 */
int vla_response_header_add(
    const vla_request *req,
    const char *header,
    const char *value,
    size_t *ind);

/**
 * Replaces a header value.
 *
 * @param req The request to replace a header in.
 *
 * @param header The header value to replace. Not case sensative.
 *
 * @param value The value to insert.
 *
 * @param i The index of the header value to replace.
 *
 * @return 0 success, -1 if the header doesn't exist.
 */
int vla_response_header_replace(
    const vla_request *req,
    const char *header,
    const char *value,
    size_t i);

/**
 * Replaces all header values with a single header value. Creates it if it
 * doesn't exist.
 *
 * @param req The request to replace a header in.
 *
 * @param header The header value to replace. Not case sensative.
 *
 * @param value The value to insert.
 *
 * @return 0 on success, -1 on error.
 */
int vla_response_header_replace_all(
    const vla_request *req,
    const char *header,
    const char *value);

/**
 * Removes the header from the response. If there are multiple headers, they
 * will be moved down, (e.g. if the header at index 0 is removed, a new header
 * will be placed at index 0 if there are more than one header).
 *
 * @param req The request to remove the header from.
 *
 * @param header The header to remove. Not case sensative.
 *
 * @param i The index of the header to remove.
 *
 * @return 0 if the header was successfully removed, -1 if it didn't exist.
 */
int vla_response_header_remove(
    const vla_request *req,
    const char *header,
    size_t i);

/**
 * Removes all of the specified headers.
 *
 * @param req The request to remove the header from.
 *
 * @param header The header to remove. Not case sensative.
 *
 * @return 0 if the header was successfully removed, -1 if it didn't exist.
 */
int vla_response_header_remove_all(const vla_request *req, const char *header);

/**
 * Gets the value of a response header.
 *
 * @param req The request to get the response header from.
 *
 * @param header The header to get. Not case sensative.
 *
 * @param i The index of the header value.
 *
 * @return The value of the header, or NULL if it doesn't exist or there was an
 *         error. Belongs to the caller. Can be freed with vla_free or will be
 *         automatically freed upon the completion of the request.
 */
const char *vla_response_header_get(
    const vla_request *req,
    const char *header,
    size_t i);

/**
 * Gets the number of values associated with a header.
 *
 * @param req The request to get the header count from.
 *
 * @param header The header to get a count of. Not case sensative.
 *
 * @return The number of values associated with that header.
 */
size_t vla_response_header_count(const vla_request *req, const char *header);

/**
 * Sets the status code for this response.
 * This is equivalent to calling
 * vla_response_header_insert(req, "Status", code)
 *
 * @param req The vla_request to set the status code for.
 *
 * @param code The status code to return.
 *
 * @return 0 if the status code was succesfully set, nonzero on error.
 */
int vla_response_set_status_code(const vla_request *req, unsigned int code);

/**
 * Gets the currently set status code for the response.
 *
 * @param req The request to get the status code from.
 *
 * @return The current status code. 200 is set by default.
 */
unsigned int vla_response_get_status_code(const vla_request *req);

/**
 * Sets the value of the Content-Type header.
 *
 * @param req The request to set the header in.
 *
 * @param type The Content-Type to set.
 *
 * @return 0 on success, nonzero on error.
 */
int vla_response_set_content_type(const vla_request *req, const char *type);

/**
 * Gets the value of the Content-Type header.
 *
 * @param req The request to get the header from.
 *
 * @return The value of the Content-Type header. NULL if it doesn't exist.
 */
const char *vla_response_get_content_type(const vla_request *req);

/**
 * Sets the Set-Cookie header. If the cookie already exists, doesn't replace it,
 * just appends it to the Set-Cookie header. Values can be overwritten by
 * manually setting the Set-Cookie header, so be careful.
 *
 * @param req The request to add the cookie header to.
 *
 * @param cookie The vla_cookie_t defining the cookie to set.
 *
 * @return 0 on success, nonzero on error.
 */
int vla_response_set_cookie(const vla_request *req, const vla_cookie_t *cookie);

/**
 * Appends data to the body of a response. Data is buffered and not actually
 * sent until the handler/middleware function returns. This means headers can be
 * set after calling vla_printf if need be.
 *
 * @param req The request to append data to.
 *
 * @param fmt The format string.
 *
 * @return 0 on success, -1 on error.
 */
int vla_printf(const vla_request *req, const char *fmt, ...);

/**
 * Appends a string to the body of a response.
 *
 * @param req The request to append data to.
 *
 * @param s The string to append to the body.
 *
 * @return 0 on success, -1 on error.
 */
int vla_puts(const vla_request *req, const char *s);

/**
 * Appends a fixed amount of data to the body of a response.
 *
 * @param req The request to append data to.
 *
 * @param data The data to append.
 *
 * @param len The length of the data to append.
 *
 * @return 0 on success, -1 on error.
 */
int vla_write(const vla_request *req, const char *data, size_t len);

/**
 * Sends data directly to the webserver over stderr. Unlike vla_printf, data is
 * not buffered and is sent immediately.
 *
 * @param req The request containing the stderr stream.
 *
 * @param fmt The format string of the data to print.
 *
 * @return 0 on success, -1 on error.
 */
int vla_eprintf(const vla_request *req, const char *fmt, ...);

/**
 * Sends data directly to the webserver over stderr. Unlike vla_puts, data is
 * not buffered and is sent immediately.
 *
 * @param req The request containing the stderr stream.
 *
 * @param s The string of data to print.
 *
 * @return 0 on success, -1 on error.
 */
int vla_eputs(const vla_request *req, const char *s);

#endif // __VALHALLA_H__
