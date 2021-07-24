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

#ifndef __REQUEST_H__
#define __REQUEST_H__

#include "include/valhalla.h"

typedef struct FCGX_Request FCGX_Request;

/**
 * Initializes a new vla_request.
 * 
 * @param ctx The vla_context that this request is a child of.
 * 
 * @param f_req The FastCGI request tied to this request.
 * 
 * @return A newly allocated vla_request that is a child of ctx. Should be freed
 *         with talloc_free().
 */
vla_request *request_new(vla_context *ctx, FCGX_Request *f_req);

/**
 * Iterates through every response header and value.
 * 
 * @param req The request tied to the response.
 * 
 * @param callback The callback function. The first argument is the header, and
 *                 the second argument is the value. Return 0 to continue
 *                 iterating, return nonzero to stop.
 * 
 * @param arg The third argument to the callback function.
 * 
 * @return 0 if every header was iterated through, -1 otherwise.
 */
int response_header_iterate(
    vla_request *req,
    int (*callback)(const char *, const char *, void *),
    void *arg);

/**
 * Gets the body of the request.
 * 
 * @param req The request to get the response body from.
 * 
 * @return The body of the response.
 */
const char *response_get_body(vla_request *req);

#endif // __REQUEST_H__
