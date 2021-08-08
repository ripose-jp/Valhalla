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

#include "unity/unity.h"

#include <stdarg.h>

#include <talloc.h>

#include "../src/route.h"

void setUp(void)
{

}

void tearDown(void)
{

}

/**
 * Creates a route info. Necessary because route_info_create takes a va_list.
 */
route_info_t *helper_create_route_info(
    vla_handler_func hdlr,
    void *hdlr_arg,
    ...)
{
    va_list ap;
    va_start(ap, hdlr_arg);
    route_info_t *info = route_info_create(NULL, hdlr, hdlr_arg, ap);
    va_end(ap);
    return info;
}

/**
 * Adds a route. Necessary because route_add takes a va_list.
 */
int helper_route_add(
    route_node_t *root,
    uint32_t methods,
    const char *route,
    vla_handler_func hdlr,
    void *hdlr_arg,
    ...)
{
    va_list ap;
    va_start(ap, hdlr_arg);
    int ret = route_add(root, methods, route, hdlr, hdlr_arg, ap);
    va_end(ap);
    return ret;
}

void test_init_root()
{
    route_node_t *root = route_init_root(NULL);
    TEST_ASSERT_NOT_NULL(root);
    talloc_free(root);
}

void test_route_info_create()
{
    vla_handler_func hdlr = (vla_handler_func)20;
    void *hdlr_arg = (void *)10;
    route_info_t *info = helper_create_route_info(hdlr, hdlr_arg, NULL);

    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_PTR(hdlr, info->hdlr);
    TEST_ASSERT_EQUAL_PTR(hdlr_arg, info->hdlr_arg);

    TEST_ASSERT_NOT_NULL(info->mw);
    TEST_ASSERT_NULL(info->mw[0]);

    TEST_ASSERT_NOT_NULL(info->mw_args);
    TEST_ASSERT_NULL(info->mw_args[0]);

    talloc_free(info);
}

void test_route_info_create_with_middleware()
{
    vla_middleware_func mw[] = {
        (vla_middleware_func) 10,
        (vla_middleware_func) 20,
        (vla_middleware_func) 30,
    };
    void *mw_args[] = {
        (void *) 11,
        (void *) 21,
        (void *) 31,
    };
    route_info_t *info = helper_create_route_info(
        NULL, NULL,
        mw[0], mw_args[0],
        mw[1], mw_args[1],
        mw[2], mw_args[2],
        NULL
    );

    TEST_ASSERT_NOT_NULL(info);

    TEST_ASSERT_EQUAL_PTR(mw[0], info->mw[0]);
    TEST_ASSERT_EQUAL_PTR(mw[1], info->mw[1]);
    TEST_ASSERT_EQUAL_PTR(mw[2], info->mw[2]);

    TEST_ASSERT_EQUAL_PTR(mw_args[0], info->mw_args[0]);
    TEST_ASSERT_EQUAL_PTR(mw_args[1], info->mw_args[1]);
    TEST_ASSERT_EQUAL_PTR(mw_args[2], info->mw_args[2]);

    talloc_free(info);
}

void test_route_exact()
{
    route_node_t *root = route_init_root(NULL);
    TEST_ASSERT_NOT_NULL(root);

    vla_handler_func hdlr = (vla_handler_func)33;
    void *hdlr_arg = (void *)44;

    int ret = helper_route_add(
        root,
        VLA_HTTP_GET | VLA_HTTP_POST,
        "/test",
        hdlr, hdlr_arg,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    const route_info_t *info_get = route_get(root, "/test", VLA_HTTP_GET);
    TEST_ASSERT_NOT_NULL(info_get);
    TEST_ASSERT_EQUAL_PTR(hdlr, info_get->hdlr);
    TEST_ASSERT_EQUAL_PTR(hdlr_arg, info_get->hdlr_arg);
    TEST_ASSERT_NULL(info_get->mw[0]);
    TEST_ASSERT_NULL(info_get->mw_args[0]);

    const route_info_t *info_post = route_get(root, "/test", VLA_HTTP_POST);
    TEST_ASSERT_NOT_NULL(info_post);
    TEST_ASSERT_EQUAL_PTR(hdlr, info_post->hdlr);
    TEST_ASSERT_EQUAL_PTR(hdlr_arg, info_post->hdlr_arg);
    TEST_ASSERT_NULL(info_post->mw[0]);
    TEST_ASSERT_NULL(info_post->mw_args[0]);

    TEST_ASSERT_EQUAL_PTR(info_get, info_post);

    talloc_free(root);
}

void test_route_exact_wrong_method()
{
    route_node_t *root = route_init_root(NULL);
    TEST_ASSERT_NOT_NULL(root);

    vla_handler_func hdlr = (vla_handler_func)33;
    void *hdlr_arg = (void *)44;

    int ret = helper_route_add(
        root,
        VLA_HTTP_GET | VLA_HTTP_POST,
        "/test",
        hdlr, hdlr_arg,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    const route_info_t *info = route_get(root, "/test", VLA_HTTP_PATCH);
    TEST_ASSERT_NULL(info);

    talloc_free(root);
}

void test_route_capture()
{
    route_node_t *root = route_init_root(NULL);
    TEST_ASSERT_NOT_NULL(root);

    vla_handler_func hdlr = (vla_handler_func)33;
    void *hdlr_arg = (void *)44;

    int ret = helper_route_add(
        root,
        VLA_HTTP_GET,
        "/test/:id",
        hdlr, hdlr_arg,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    const route_info_t *info = route_get(root, "/test/1", VLA_HTTP_GET);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_PTR(hdlr, info->hdlr);
    TEST_ASSERT_EQUAL_PTR(hdlr_arg, info->hdlr_arg);
    TEST_ASSERT_NULL(info->mw[0]);
    TEST_ASSERT_NULL(info->mw_args[0]);

    talloc_free(root);
}

void test_route_capture_empty()
{
    route_node_t *root = route_init_root(NULL);
    TEST_ASSERT_NOT_NULL(root);

    vla_handler_func hdlr = (vla_handler_func)33;
    void *hdlr_arg = (void *)44;

    int ret = helper_route_add(
        root,
        VLA_HTTP_GET,
        "/test/:id",
        hdlr, hdlr_arg,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    const route_info_t *info = route_get(root, "/test/", VLA_HTTP_GET);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_PTR(hdlr, info->hdlr);
    TEST_ASSERT_EQUAL_PTR(hdlr_arg, info->hdlr_arg);
    TEST_ASSERT_NULL(info->mw[0]);
    TEST_ASSERT_NULL(info->mw_args[0]);

    talloc_free(root);
}

void test_route_capture_middle()
{
    route_node_t *root = route_init_root(NULL);
    TEST_ASSERT_NOT_NULL(root);

    vla_handler_func hdlr = (vla_handler_func)33;
    void *hdlr_arg = (void *)44;

    int ret = helper_route_add(
        root,
        VLA_HTTP_GET,
        "/test/:id/book",
        hdlr, hdlr_arg,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    const route_info_t *info = route_get(root, "/test/1/book", VLA_HTTP_GET);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_PTR(hdlr, info->hdlr);
    TEST_ASSERT_EQUAL_PTR(hdlr_arg, info->hdlr_arg);
    TEST_ASSERT_NULL(info->mw[0]);
    TEST_ASSERT_NULL(info->mw_args[0]);

    talloc_free(root);
}

void test_route_capture_wrong()
{
    route_node_t *root = route_init_root(NULL);
    TEST_ASSERT_NOT_NULL(root);

    vla_handler_func hdlr = (vla_handler_func)33;
    void *hdlr_arg = (void *)44;

    int ret = helper_route_add(
        root,
        VLA_HTTP_GET,
        "/test/:id",
        hdlr, hdlr_arg,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Wrong Method */
    const route_info_t *info = route_get(root, "/test/1", VLA_HTTP_HEAD);
    TEST_ASSERT_NULL(info);

    /* Wrong URI */
    info = route_get(root, "/test", VLA_HTTP_GET);
    TEST_ASSERT_NULL(info);

    /* Wrong URI */
    info = route_get(root, "/test/1/delete", VLA_HTTP_GET);
    TEST_ASSERT_NULL(info);

    talloc_free(root);
}

void test_route_capture_middle_wrong()
{
    route_node_t *root = route_init_root(NULL);
    TEST_ASSERT_NOT_NULL(root);

    vla_handler_func hdlr = (vla_handler_func)33;
    void *hdlr_arg = (void *)44;

    int ret = helper_route_add(
        root,
        VLA_HTTP_GET,
        "/test/:id/book",
        hdlr, hdlr_arg,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Wrong Method */
    const route_info_t *info = route_get(root, "/test/2/book", VLA_HTTP_OPTIONS);
    TEST_ASSERT_NULL(info);

    /* Wrong URI */
    info = route_get(root, "/test/2", VLA_HTTP_GET);
    TEST_ASSERT_NULL(info);

    /* Wrong URI */
    info = route_get(root, "/test/1/book/", VLA_HTTP_GET);
    TEST_ASSERT_NULL(info);

    talloc_free(root);
}

void test_route_match_all()
{
    route_node_t *root = route_init_root(NULL);
    TEST_ASSERT_NOT_NULL(root);

    vla_handler_func hdlr = (vla_handler_func)33;
    void *hdlr_arg = (void *)44;

    int ret = helper_route_add(
        root,
        VLA_HTTP_GET,
        "/test*",
        hdlr, hdlr_arg,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    const char *routes[] = {
        "/test",
        "/test/",
        "/test/1/book"
        "/testttttt",
        "/test*",
        NULL,
    };
    for (const char **str = routes; *str; ++str)
    {
        const route_info_t *info = route_get(root, *str, VLA_HTTP_GET);
        TEST_ASSERT_NOT_NULL(info);
        TEST_ASSERT_EQUAL_PTR(hdlr, info->hdlr);
        TEST_ASSERT_EQUAL_PTR(hdlr_arg, info->hdlr_arg);
        TEST_ASSERT_NULL(info->mw[0]);
        TEST_ASSERT_NULL(info->mw_args[0]);
    }

    talloc_free(root);
}

void test_route_match_wrong()
{
    route_node_t *root = route_init_root(NULL);
    TEST_ASSERT_NOT_NULL(root);

    vla_handler_func hdlr = (vla_handler_func)33;
    void *hdlr_arg = (void *)44;

    int ret = helper_route_add(
        root,
        VLA_HTTP_POST,
        "/test*",
        hdlr, hdlr_arg,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    const char *routes[] = {
        "/tes",
        "/unrelated",
        "//test"
        "test",
        "test*",
        NULL,
    };
    for (const char **str = routes; *str; ++str)
    {
        const route_info_t *info = route_get(root, *str, VLA_HTTP_OPTIONS);
        TEST_ASSERT_NULL(info);
    }

    talloc_free(root);
}

void test_overlapping_routes()
{
    route_node_t *root = route_init_root(NULL);
    TEST_ASSERT_NOT_NULL(root);

    vla_handler_func hdlr = (vla_handler_func)33;
    void *hdlr_arg = (void *)44;

    int ret = helper_route_add(
        root,
        VLA_HTTP_GET,
        "/*",
        hdlr, hdlr_arg,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    ret = helper_route_add(
        root,
        VLA_HTTP_GET,
        "/*",
        NULL, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(1, ret);

    ret = helper_route_add(
        root,
        VLA_HTTP_GET,
        "/test/:",
        NULL, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(1, ret);

    ret = helper_route_add(
        root,
        VLA_HTTP_GET,
        "/book",
        NULL, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(1, ret);

    talloc_free(root);
}

void test_same_route_different_method()
{
    route_node_t *root = route_init_root(NULL);
    TEST_ASSERT_NOT_NULL(root);

    vla_handler_func hdlr = (vla_handler_func)33;
    void *hdlr_arg = (void *)44;

    int ret = helper_route_add(
        root,
        VLA_HTTP_GET,
        "/*",
        hdlr, hdlr_arg,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    ret = helper_route_add(
        root,
        VLA_HTTP_POST,
        "/*",
        NULL, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    const route_info_t *i_get = route_get(root, "/", VLA_HTTP_GET);
    TEST_ASSERT_NOT_NULL(i_get);

    const route_info_t *p_get = route_get(root, "/", VLA_HTTP_POST);
    TEST_ASSERT_NOT_NULL(p_get);

    TEST_ASSERT_NOT_EQUAL(i_get, p_get);

    talloc_free(root);
}

void test_malformed_route()
{
    route_node_t *root = route_init_root(NULL);
    TEST_ASSERT_NOT_NULL(root);

    vla_handler_func hdlr = (vla_handler_func)33;
    void *hdlr_arg = (void *)44;

    int ret = helper_route_add(
        root,
        VLA_HTTP_GET,
        "",
        hdlr, hdlr_arg,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(-1, ret);

    ret = helper_route_add(
        root,
        VLA_HTTP_GET,
        " /",
        hdlr, hdlr_arg,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(-1, ret);

    ret = helper_route_add(
        root,
        VLA_HTTP_GET,
        "*",
        hdlr, hdlr_arg,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(-1, ret);

    ret = helper_route_add(
        root,
        VLA_HTTP_GET,
        ":",
        hdlr, hdlr_arg,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(-1, ret);

    talloc_free(root);
}

void test_route_capture_and_match()
{
    route_node_t *root = route_init_root(NULL);
    TEST_ASSERT_NOT_NULL(root);

    vla_handler_func hdlr = (vla_handler_func)33;
    void *hdlr_arg = (void *)44;

    int ret = helper_route_add(
        root,
        VLA_HTTP_POST,
        "/book-:name/add/*",
        hdlr, hdlr_arg,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    const char *routes[] = {
        "/book-Catch22/add/",
        "/book-Catch22/add/Author/Heller",
        "/book-/add/Something",
        "/book-HP/add/LoveCraft",
        "/book-:/add/*",
        NULL,
    };
    for (const char **str = routes; *str; ++str)
    {
        const route_info_t *info = route_get(root, *str, VLA_HTTP_POST);
        TEST_ASSERT_NOT_NULL(info);
        TEST_ASSERT_EQUAL_PTR(hdlr, info->hdlr);
        TEST_ASSERT_EQUAL_PTR(hdlr_arg, info->hdlr_arg);
        TEST_ASSERT_NULL(info->mw[0]);
        TEST_ASSERT_NULL(info->mw_args[0]);
    }

    talloc_free(root);
}

void test_route_any_method()
{
    route_node_t *root = route_init_root(NULL);
    TEST_ASSERT_NOT_NULL(root);

    vla_handler_func hdlr = (vla_handler_func)33;
    void *hdlr_arg = (void *)44;

    int ret = helper_route_add(
        root,
        VLA_HTTP_ALL,
        "/",
        hdlr, hdlr_arg,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    enum vla_http_method methods[] = {
        VLA_HTTP_GET,
        VLA_HTTP_HEAD,
        VLA_HTTP_POST,
        VLA_HTTP_PUT,
        VLA_HTTP_DELETE,
        VLA_HTTP_CONNECT,
        VLA_HTTP_OPTIONS,
        VLA_HTTP_TRACE,
        VLA_HTTP_PATCH,
    };
    for (size_t i = 0; i < 9; ++i)
    {
        const route_info_t *info = route_get(root, "/", methods[i]);
        TEST_ASSERT_NOT_NULL(info);
        TEST_ASSERT_EQUAL_PTR(hdlr, info->hdlr);
        TEST_ASSERT_EQUAL_PTR(hdlr_arg, info->hdlr_arg);
        TEST_ASSERT_NULL(info->mw[0]);
        TEST_ASSERT_NULL(info->mw_args[0]);
    }

    talloc_free(root);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_root);
    RUN_TEST(test_route_info_create);
    RUN_TEST(test_route_info_create_with_middleware);
    RUN_TEST(test_route_exact);
    RUN_TEST(test_route_exact_wrong_method);
    RUN_TEST(test_route_capture);
    RUN_TEST(test_route_capture_empty);
    RUN_TEST(test_route_capture_middle);
    RUN_TEST(test_route_capture_wrong);
    RUN_TEST(test_route_capture_middle_wrong);
    RUN_TEST(test_route_match_all);
    RUN_TEST(test_route_match_wrong);
    RUN_TEST(test_overlapping_routes);
    RUN_TEST(test_same_route_different_method);
    RUN_TEST(test_malformed_route);
    RUN_TEST(test_route_capture_and_match);
    RUN_TEST(test_route_any_method);
    return UNITY_END();
}