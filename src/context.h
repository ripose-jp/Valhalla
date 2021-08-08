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

#ifndef __CONTEXT_H__
#define __CONTEXT_H__

#include "include/valhalla.h"

#include "route.h"

/**
 * Gets the route_info_t corresponding to the route.
 *
 * @param ctx The vla_context to get the route from.
 *
 * @param uri The request URI.
 *
 * @param method The HTTP request method.
 *
 * @return The route_info_t corresponding to the route. NULL if it doesn't
 *         exist.
 */
const route_info_t *context_get_route(
    vla_context *ctx,
    const char *uri,
    enum vla_http_method method);

#endif // __CONTEXT_H__