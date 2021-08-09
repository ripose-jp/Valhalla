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

#ifndef __STRUTIL_H__
#define __STRUTIL_H__

#include <stddef.h>

/**
 * strchr except if the character isn't found, a pointer to the nul terminator
 * is returned.
 *
 * @param s The string to search through.
 *
 * @param c The character to look for.
 *
 * @return A pointer to the first occurance of c in s. A pointer to the nul
 *         terminator in s if c isn't found.
 */
const char *su_strchrnul(const char *s, int c);

/**
 * Duplicates the first len characters of a string.
 *
 * @param ctx The talloc context to the string should be a child of.
 *
 * @param str The string to duplicate.
 *
 * @param len The maximum number of characters to duplicate.
 *
 * @return A talloc allocated string that is a child of ctx. NULL on error.
 */
char *su_tstrndup(void *ctx, const char *str, size_t len);

/**
 * Duplicates a string.
 *
 * @param ctx The talloc context to the string should be a child of.
 *
 * @param str The string to duplicate.
 *
 * @param len The maximum number of characters to duplicate.
 *
 * @return A talloc allocated string that is a child of ctx. NULL on error.
 */
char *su_tstrdup(void *ctx, const char *str);

/**
 * Encodes a string into a URL-encoded string.
 *
 * @param ctx The talloc context the string should belong to.
 *
 * @param str The string to URL encode.
 *
 * @return The string encoded into URL-encoding. NULL on error.
 */
char *su_url_encode(void *ctx, const char *str);

/**
 * Encodes a string into a URL-encoded string.
 *
 * @param ctx The talloc context the string should belong to.
 *
 * @param str The string to URL encode.
 *
 * @param len The length of the string.
 *
 * @return The string encoded into URL-encoding. NULL on error.
 */
char *su_url_encode_l(void *ctx, const char *str, size_t len);

/**
 * Decodes a URL-encoded string.
 *
 * @param ctx The talloc context the string should belong to.
 *
 * @param str The URL-encoded string to decode.
 *
 * @return A URL decoded into UTF-8. NULL on error.
 */
char *su_url_decode(void *ctx, const char *str);

/**
 * Decodes a URL-encoded string.
 *
 * @param ctx The talloc context the string should belong to.
 *
 * @param str The URL-encoded string to decode.
 *
 * @param len The length of the string.
 *
 * @return A URL decoded into UTF-8. NULL on error.
 */
char *su_url_decode_l(void *ctx, const char *str, size_t len);

#endif // __STRUTIL_H__