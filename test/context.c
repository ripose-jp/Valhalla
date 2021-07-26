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

#include "../src/context.h"

#include "unity/unity.h"

static vla_context *ctx = NULL;

void setUp(void)
{
    ctx = vla_init();
    TEST_ASSERT_NOT_NULL(ctx);

    int ret = vla_add_route(
        ctx,
        VLA_HTTP_PUT | VLA_HTTP_PATCH | VLA_HTTP_POST,
        "/books",
        (vla_handler_func)1, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    ret = vla_add_route(
        ctx,
        VLA_HTTP_GET,
        "/books/:id",
        (vla_handler_func)2, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    ret = vla_add_route(
        ctx,
        VLA_HTTP_GET,
        "/books/:id/:page",
        (vla_handler_func)3, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    ret = vla_add_route(
        ctx,
        VLA_HTTP_ALL,
        "/hole/*",
        (vla_handler_func)4, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void tearDown(void)
{
    int ret = vla_free(ctx);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_add_overlapping_route()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET,
        "/books/:id/:title",
        (vla_handler_func)5, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(1, ret);
}

void test_add_malformed_route()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET,
        "*",
        (vla_handler_func)6, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_add_new_method_route()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_DELETE,
        "/books/:id",
        (vla_handler_func)7, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_get_route()
{
    route_info_t *info = context_get_route(ctx, "/books/4", VLA_HTTP_GET);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_PTR((vla_handler_func)2, info->hdlr);
}

void test_get_missing_route()
{
    route_info_t *info = context_get_route(ctx, "/movies/2", VLA_HTTP_GET);
    TEST_ASSERT_NULL(info);
}

void test_unknown_route()
{
    vla_set_not_found_handler(
        ctx,
        (vla_handler_func)-1, (void *)-2,
        (vla_middleware_func)-3, (void *)-4,
        NULL
    );
    route_info_t *info = context_get_route(ctx, "/movies/2", VLA_HTTP_GET);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_PTR((vla_handler_func)-1, info->hdlr);
    TEST_ASSERT_EQUAL_PTR((void *)-2, info->hdlr_arg);
    TEST_ASSERT_EQUAL_PTR((vla_middleware_func)-3, info->mw[0]);
    TEST_ASSERT_EQUAL_PTR((void *)-4, info->mw_args[0]);
    TEST_ASSERT_NULL(info->mw[1]);
    TEST_ASSERT_NULL(info->mw_args[1]);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_add_overlapping_route);
    RUN_TEST(test_add_malformed_route);
    RUN_TEST(test_add_new_method_route);
    RUN_TEST(test_get_route);
    RUN_TEST(test_get_missing_route);
    RUN_TEST(test_unknown_route);

    return UNITY_END();
}