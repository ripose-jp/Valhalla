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

#ifndef __STRCASEMAP_H__
#define __STRCASEMAP_H__

#include "khash.h"

#include <ctype.h>

/**
 * Hashes a string ignoring case.
 * 
 * @param ptr A pointer to the string to hash.
 * 
 * @param unused Length of ptr. Unused because strings are nul terminated.
 * 
 * @param seed The seed to use in the hash function.
 * 
 * @return The hash of ptr.
 */
static size_t hash_ignorecase(const char *str)
{
    char buf[512];
    size_t len;
    for (len = 0; len < sizeof(buf) - 1 && str[len]; ++len)
    {
        buf[len] = tolower(str[len]);
    }
    buf[len] = '\0';
    
    return kh_str_hash_func(buf);
}

#define strcase_str_hash_equal(a, b) (strcasecmp(a, b) == 0)

KHASH_INIT(strcase, kh_cstr_t, char *, 1, hash_ignorecase, strcase_str_hash_equal);

#endif // __STRCASEMAP_H__