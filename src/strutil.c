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

#include "strutil.h"

#include <ctype.h>
#include <string.h>

#include <talloc.h>

const char *su_strchrnul(const char *s, int c)
{
    while (*s && *s != c)
    {
        ++s;
    }
    return s;
}

/**
 * URL encoding/decoding credit goes to Fred Bulback.
 * https://www.geekhideout.com/urlcode.shtml
 * Below is code modified from the public domain.
 */

#define NIBBLE_MASK 0xF
#define NIBBLE_SHIFT 4

/**
 * Converts a hex character to its integer value.
 * 
 * @param ch The character to convert. Must be a valid 0 - F hex character.
 * 
 * @return The integer equivalent ch.
 */
static char from_hex(char ch)
{
    return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

/**
 * Converts an integer value to its hex character.
 * 
 * @param code The integer value to convert.
 * 
 * @return A hex character corresponding to the lower four bits of code.
 */
static char to_hex(char code)
{
    static const char *hex = "0123456789ABCDEF";
    return hex[code & NIBBLE_MASK];
}

char *su_url_encode_l(const char *str, size_t len)
{
    const char *pstr = str;
    const char *pstr_end = &str[len];
    char *buf = talloc_array(NULL, char, len * 3 + 1);
    char *pbuf = buf;
    while (pstr < pstr_end)
    {
        if (isalnum(*pstr) ||
            *pstr == '-' ||
            *pstr == '_' ||
            *pstr == '.' ||
            *pstr == '~')
        {
            *pbuf++ = *pstr;
        }
        else if (*pstr == ' ')
        {
            *pbuf++ = '+';
        }
        else
        {
            *pbuf++ = '%';
            *pbuf++ = to_hex(*pstr >> NIBBLE_SHIFT);
            *pbuf++ = to_hex(*pstr & NIBBLE_MASK);
        }
        pstr++;
    }
    *pbuf = '\0';
    return buf;
}

char *su_url_encode(const char *str)
{
    return su_url_encode_l(str, strlen(str));
}

char *su_url_decode_l(const char *str, size_t len)
{
    const char *pstr = str;
    const char *pstr_end = &str[len];
    char *buf = talloc_array(NULL, char, len + 1);
    char *pbuf = buf;
    while (pstr < pstr_end)
    {
        if (*pstr == '%')
        {
            if (pstr[1] && pstr[2])
            {
                *pbuf++ = from_hex(pstr[1]) << NIBBLE_SHIFT | from_hex(pstr[2]);
                pstr += 2;
            }
        }
        else if (*pstr == '+')
        {
            *pbuf++ = ' ';
        }
        else
        {
            *pbuf++ = *pstr;
        }
        pstr++;
    }
    *pbuf = '\0';
    return buf;
}

char *su_url_decode(const char *str)
{
    return su_url_decode_l(str, strlen(str));
}

#undef NIBBLE_MASK
#undef NIBBLE_SHIFT
