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
typedef enum vla_handle_code (*vla_handler_func)(vla_request *, void *);

/*
 * Function pointer type for middleware functions.
 */
typedef enum vla_handle_code (*vla_middleware_func)(vla_request *, void *);

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
 *              after the colon up to the next '/' (or end of string if that
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
 */
void vla_set_not_found_handler(
    vla_context *ctx,
    vla_handler_func handler,
    void *handler_arg,
    ...);

/**
 * Accepts incoming web requests. Blocks while waiting.
 * 
 * @param ctx The context containing the route information.
 */
void vla_accept(vla_context *ctx);

/*
 *==============================================================================
 * Request
 *==============================================================================
 */

/**
 * Gets the request body. 
 * This method will only read the message body up to size on the first 
 * call. Subsequent calls will return what portion of the body was already 
 * read.
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
 */
const char *vla_request_body_get(vla_request *req, size_t size);

/**
 * Gets the size of the request body.
 * 
 * @param req The request to get the body size from.
 * 
 * @return The size of the request body, 0 if it hasn't been read yet or if it
 *         doesn't have a body.
 */
size_t vla_request_body_get_length(vla_request *req);

/**
 * Gets a request header.
 * 
 * @param req The vla_request to get the header from.
 * 
 * @param header The header to get the value of.
 * 
 * @return The header if it exists, NULL otherwise. Belongs to the vla_request.
 */
const char *vla_request_header_get(vla_request *req, const char *header);

/**
 * Iterates over request headers.
 * 
 * @param req The vla_request to iterate over.
 * 
 * @param callback The function to call for each value. The first argument is
 *                 the header, and the second is the value. Returns 0 to
 *                 continue iterating, nonzero to stop.
 * 
 * @param arg The third argument to the callback function.
 * 
 * @return 0 if every value was iterated over, 1 otherwise.
 */
int vla_request_header_iterate(
    vla_request *req,
    int (*callback)(const char *, const char *, void *),
    void *arg);

/**
 * Gets the value of the Content-Type header.
 * 
 * @param req The vla_request to get the header from.
 * 
 * @return The Content-Type if it exists, NULL otherwise. Belongs to the
 *         vla_request.
 */
const char *vla_request_get_content_type(vla_request *req);

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
const char *vla_request_getenv(vla_request *req, const char *var);

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
 * @return 0 if every value was iterated over, 1 otherwise.
 */
int vla_request_env_iterate(
    vla_request *req,
    int (*callback)(const char *, const char *, void *),
    void *arg);

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
const char *vla_request_query_get(vla_request *req, const char *key);

/**
 * Iterates the query string values.
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
    vla_request *req,
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
enum vla_handle_code vla_request_next_func(vla_request *req);

/*
 *==============================================================================
 * Response
 *==============================================================================
 */

/**
 * Adds a header to the response. Replaces it if it already exists.
 * 
 * @param req The request to add the response header to.
 * 
 * @param header The name of the header. Not case sensative.
 * 
 * @param value The value of the header.
 * 
 * @return 0 if the action occurred succesfully, nonzero on error.
 */
int vla_response_header_insert(
    vla_request *req,
    const char *header,
    const char *value);

/**
 * Appends values to a response header in as a comma seperated list. Inserts the
 * header and value if it doesn't already exist.
 * 
 * @param req The request to add the response header to.
 * 
 * @param header The name of the header. Not case sensative.
 * 
 * @param value The value to append to the header.
 * 
 * @return 0 if the action occurred succesfully, nonzero on error.
 */
int vla_response_header_append(
    vla_request *req,
    const char *header,
    const char *value);

/**
 * Removes the header from the response.
 * 
 * @param req The request to remove the header from.
 * 
 * @param header The header to remove.
 * 
 * @return 0 if the header was successfully removed, -1 if it didn't exist.
 */
int vla_response_header_remove(vla_request *req, const char *header);

/**
 * Gets the value of the response header.
 * 
 * @param req The request to get the response header from.
 * 
 * @param header The header to get.
 * 
 * @return The value of the header, NULL if it doesn't exist.
 */
const char *vla_response_header_get(vla_request *req, const char *header);

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
int vla_response_set_status_code(vla_request *req, unsigned int code);

/**
 * Gets the currently set status code for the response.
 * 
 * @param req The request to get the status code from.
 * 
 * @return The current status code. 200 is set by default.
 */
unsigned int vla_response_get_status_code(vla_request *req);

/**
 * Sets the value of the Content-Type header.
 * 
 * @param req The request to set the header in.
 * 
 * @param type The Content-Type to set.
 * 
 * @return 0 on success, nonzero on error.
 */
int vla_response_set_content_type(vla_request *req, const char *type);

/**
 * Gets the value of the Content-Type header.
 * 
 * @param req The request to get the header from.
 * 
 * @return The value of the Content-Type header. NULL if it doesn't exist.
 */
const char *vla_response_get_content_type(vla_request *req);

/**
 * Appends data to the body of a response. Data is buffered and not actually
 * sent until the handler/middleware function returns. This means headers can be
 * set after calling vla_printf if need be.
 * 
 * @param req The request to append data to.
 * 
 * @param fmt The format string.
 */
void vla_printf(vla_request *req, const char *fmt, ...);

/**
 * Appends a string to the body of a response.
 * 
 * @param req The request to append data to.
 * 
 * @param s The string to append to the body.
 */
void vla_puts(vla_request *req, const char *s);

/**
 * Appends a fixed amount of data to the body of a response.
 * 
 * @param req The request to append data to.
 * 
 * @param data The data to append.
 * 
 * @param len The length of the data to append.
 */
void vla_write(vla_request *req, const char *data, size_t len);

#endif // __VALHALLA_H__
