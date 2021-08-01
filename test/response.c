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

#include "../src/request.h"

#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include <curl/curl.h>
#include <talloc.h>

/* Request parameters for curl. */
typedef struct request_params
{
    /* Url of the request. */
    const char *url;

    /* Request method (eg GET). */
    const char *method;

    /* Array of headers. */
    struct curl_slist *headers;

    /* Body of the request. */
    const char *body;
} request_params;

static vla_context *ctx = NULL;

static request_params r_params;

static CURL *c = NULL;

static long res_code;

static char **res_hdrs = NULL;
static size_t res_hdrs_size = 0;

static char *res_body = NULL;

void setUp(void)
{
    r_params.url = "http://localhost/response";
    r_params.method = "GET";
    r_params.headers = NULL;
    r_params.body = NULL;

    c = curl_easy_init();
    TEST_ASSERT_NOT_NULL(c);

    ctx = vla_init();
    TEST_ASSERT_NOT_NULL(ctx);

    res_code = 0;
    res_body = NULL;
    res_hdrs = talloc_array(NULL, char *, 100);
    TEST_ASSERT_NOT_NULL(res_hdrs);
    res_hdrs_size = 0;
}

void tearDown(void)
{
    int ret = vla_free(ctx);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ctx = NULL;

    curl_easy_cleanup(c);
    c = NULL;
    curl_slist_free_all(r_params.headers);
    r_params.headers = NULL;

    talloc_free(res_hdrs);
    res_hdrs = NULL;

    talloc_free(res_body);
    res_body = NULL;
}

/* Reads the body of the request into res_body. */
static size_t read_header(void *data, size_t size, size_t nmemb, void *nul)
{
    size_t len = talloc_array_length(res_hdrs);
    if (res_hdrs_size >= len)
    {
        return 0;
    }

    size_t realsize = size * nmemb;
    res_hdrs[res_hdrs_size] = talloc_array(res_hdrs, char, realsize + 1);
    TEST_ASSERT_NOT_NULL(res_hdrs[res_hdrs_size]);
    memcpy(res_hdrs[res_hdrs_size], data, realsize);
    res_hdrs[res_hdrs_size][realsize] = '\0';

    res_hdrs_size++;

    return realsize;
}

/* Reads the body of the request into res_body. */
static size_t read_body(void *data, size_t size, size_t nmemb, void *nul)
{
    size_t realsize = size * nmemb;
    res_body = talloc_array(NULL, char, realsize + 1);
    TEST_ASSERT_NOT_NULL(res_body);
    memcpy(res_body, data, realsize);
    res_body[realsize] = '\0';
    return realsize;
}

/* Send a request as described in r_params. */
static void *thread_send_request(void *arg)
{
    usleep(1 * 1000); // 1ms

    curl_easy_setopt(c, CURLOPT_URL, r_params.url);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, r_params.method);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, r_params.headers);
    if (r_params.body)
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, r_params.body);

    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, read_header);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, read_body);

    CURLcode code = curl_easy_perform(c);
    TEST_ASSERT_EQUAL(CURLE_OK, code);

    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &res_code);

    return NULL;
}

/* Starts a requets and returns upon its completion */
static void start_request()
{
    pthread_t tid;
    int code = pthread_create(&tid, NULL, thread_send_request, NULL);
    TEST_ASSERT_EQUAL_INT(0, code);
    vla_accept(ctx);
    pthread_join(tid, NULL);
}

/**
 * Verifies a header (not case sensative) exists and it of the value (case
 * sensative)
 */
void helper_header_value_exists(const char *hdr, const char *val)
{
    size_t hdr_l = strlen(hdr);
    size_t val_l = strlen(val);

    const char *actual_hdr = NULL;
    for (size_t i = 0; i < res_hdrs_size; ++i)
    {
        if (strncasecmp(res_hdrs[i], hdr, hdr_l) == 0)
        {
            TEST_ASSERT_NULL(actual_hdr);
            actual_hdr = res_hdrs[i];
        }
    }
    TEST_ASSERT_NOT_NULL(actual_hdr);

    int cmp = strncasecmp(actual_hdr, hdr, hdr_l);
    TEST_ASSERT_EQUAL_INT(0, cmp);

    const char *actual_val = &actual_hdr[hdr_l];
    cmp = strncasecmp(actual_val, val, val_l);
    TEST_ASSERT_EQUAL_INT(0, cmp);
}

void helper_header_not_exist(const char *hdr)
{
    size_t hdr_l = strlen(hdr);

    for (size_t i = 0; i < res_hdrs_size; ++i)
    {
        if (strncasecmp(res_hdrs[i], hdr, hdr_l) == 0)
        {
            TEST_ASSERT_EQUAL_STRING(NULL, hdr);
        }
    }
}

enum vla_handle_code handler_header_insert(vla_request *req, void *nul)
{
    int ret = vla_response_header_insert(req, "X-Test-Header", "Cheese");
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_insert()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_insert, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "Cheese");
}

enum vla_handle_code handler_header_replace(vla_request *req, void *nul)
{
    int ret = vla_response_header_insert(req, "X-Test-Header", "Cheese");
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_insert(req, "X-Test-Header", "Tomato");
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_replace()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_replace, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "Tomato");
}

enum vla_handle_code handler_header_append(vla_request *req, void *nul)
{
    int ret = vla_response_header_append(req, "X-Test-Header", "Cheese");
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_append(req, "X-Test-Header", "Tomato");
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_append()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_append, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "Cheese,Tomato");
}

enum vla_handle_code handler_header_append_multi(vla_request *req, void *nul)
{
    int ret = vla_response_header_append(req, "X-Test-Header", "Cheese");
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_append(req, "X-Test-Header", "Tomato");
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_append(req, "X-Test-Header", "Bacon");
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_append_multi()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_append_multi, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "Cheese,Tomato,Bacon");
}

enum vla_handle_code handler_header_append_single(vla_request *req, void *nul)
{
    int ret = vla_response_header_append(req, "X-Test-Header", "Cheese");
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_append_single()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_append_single, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "Cheese");
}

enum vla_handle_code handler_header_delete(vla_request *req, void *nul)
{
    int ret = vla_response_header_insert(req, "X-Test-Header", "Cheese");
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_remove(req, "X-Test-Header");
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_delete()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_delete, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_not_exist("x-test-header: ");
}

enum vla_handle_code handler_header_delete_not_exist(vla_request *req, void *nul)
{
    int ret = vla_response_header_remove(req, "X-Test-Header");
    TEST_ASSERT_EQUAL_INT(-1, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_delete_not_exist()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_delete_not_exist, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_not_exist("x-test-header: ");
}

enum vla_handle_code handler_header_multi(vla_request *req, void *nul)
{
    int ret = vla_response_header_insert(req, "X-Test-Header", "Test");
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_insert(req, "X-Accept-Stuff", "en_US;utf-8");
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_append(req, "X-Accept-Stuff", "fr_FR;s-jis");
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_append(req, "X-app", "1");
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_append(req, "X-app", "2");
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_remove(req, "x-app");
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_remove(req, "x-app");
    TEST_ASSERT_EQUAL_INT(-1, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_multi()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_multi, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "Test");
    helper_header_value_exists("x-accept-stuff: ", "en_US;utf-8,fr_FR;s-jis");
    helper_header_not_exist("x-app");
}

enum vla_handle_code handler_header_get(vla_request *req, void *nul)
{
    int ret = vla_response_header_insert(req, "X-Test-Header", "Test");
    TEST_ASSERT_EQUAL_INT(0, ret);
    const char *val = vla_response_header_get(req, "x-test-header");
    TEST_ASSERT_EQUAL_STRING("Test", val);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_get()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_get, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "Test");
}

enum vla_handle_code handler_header_get_null(vla_request *req, void *nul)
{
    const char *val = vla_response_header_get(req, "x-test-header");
    TEST_ASSERT_NULL(val);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_get_null()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_get_null, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_not_exist("x-test-header: ");
}

enum vla_handle_code handler_status(vla_request *req, void *nul)
{
    unsigned int ret = vla_response_get_status_code(req);
    TEST_ASSERT_EQUAL_UINT(200, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_status()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_status, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    TEST_ASSERT_EQUAL(200, res_code);
}

enum vla_handle_code handler_set_status(vla_request *req, void *nul)
{
    int ret = vla_response_set_status_code(req, 404);
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_set_status()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_set_status, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    TEST_ASSERT_EQUAL(404, res_code);
}

enum vla_handle_code handler_set_status_twice(vla_request *req, void *nul)
{
    int ret = vla_response_set_status_code(req, 404);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_set_status_code(req, 501);
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_set_status_twice()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_set_status_twice, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    TEST_ASSERT_EQUAL(501, res_code);
}

enum vla_handle_code handler_get_status(vla_request *req, void *nul)
{
    int ret = vla_response_set_status_code(req, 300);
    TEST_ASSERT_EQUAL_INT(0, ret);
    unsigned int code = vla_response_get_status_code(req);
    TEST_ASSERT_EQUAL_UINT(300, code);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_status()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_get_status, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    TEST_ASSERT_EQUAL(300, res_code);
}

enum vla_handle_code handler_set_content_type(vla_request *req, void *nul)
{
    int ret = vla_response_set_content_type(req, "text/plain");
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_set_content_type()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_set_content_type, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("Content-Type: ", "text/plain");
}

enum vla_handle_code handler_get_content_type(vla_request *req, void *nul)
{
    int ret = vla_response_set_content_type(req, "text/html");
    TEST_ASSERT_EQUAL_INT(0, ret);
    const char *type = vla_response_get_content_type(req);
    TEST_ASSERT_EQUAL_STRING("text/html", type);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_content_type()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_get_content_type, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("Content-Type: ", "text/html");
}

enum vla_handle_code handler_get_content_type_null(vla_request *req, void *nul)
{
    const char *type = vla_response_get_content_type(req);
    TEST_ASSERT_NULL(type);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_content_type_null()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_get_content_type_null, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_not_exist("Content-Type: ");
}

enum vla_handle_code handler_printf(vla_request *req, void *nul)
{
    vla_printf(req, "%s\n%d", "Test", -3);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_printf()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_printf, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    TEST_ASSERT_EQUAL_STRING("Test\n-3", res_body);
}

enum vla_handle_code handler_puts(vla_request *req, void *nul)
{
    vla_puts(req, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    return VLA_HANDLE_RESPOND_TERM;
}

void test_puts()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_puts, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    TEST_ASSERT_EQUAL_STRING("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ", res_body);
}

enum vla_handle_code handler_write(vla_request *req, void *nul)
{
    vla_write(req, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ", 10);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_write()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_write, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    TEST_ASSERT_EQUAL_STRING("0123456789", res_body);
}

enum vla_handle_code handler_write_binary(vla_request *req, void *nul)
{
    const char write[] = "\x00\x00\x00\x90\x90";
    vla_write(req, write, sizeof(write) - 1);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_write_binary()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_write_binary, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    const char write[] = "\x00\x00\x00\x90\x90";
    TEST_ASSERT_EQUAL_CHAR_ARRAY(write, res_body, sizeof(write) - 1);
}

enum vla_handle_code handler_multi_print(vla_request *req, void *nul)
{
    vla_puts(req, "1: puts\n");
    vla_printf(req, "%u: %s%c", 2, "printf", '\n');
    char bin[] = "3: \x90\x90\x00\x27\xf7\x22";
    vla_write(req, bin, sizeof(bin) - 1);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_multi_print()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_multi_print, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    const char body[] = "1: puts\n"
        "2: printf\n"
        "3: \x90\x90\x00\x27\xf7\x22";
    TEST_ASSERT_EQUAL_CHAR_ARRAY(body, res_body, sizeof(body) - 1);
}

enum vla_handle_code handler_header_and_print(vla_request *req, void *nul)
{
    vla_response_set_status_code(req, 301);
    vla_response_header_insert(req, "x-test-header", "test");
    vla_puts(req, "Bacon ");
    vla_puts(req, "Lettuce ");
    vla_puts(req, "Tomato");
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_and_print()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_and_print, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    TEST_ASSERT_EQUAL(301, res_code);
    helper_header_value_exists("x-test-header: ", "test");
    TEST_ASSERT_EQUAL_STRING("Bacon Lettuce Tomato", res_body);
}

enum vla_handle_code handler_header_and_print_order(vla_request *req, void *nul)
{
    vla_puts(req, "Rock ");
    vla_response_set_status_code(req, 504);
    vla_response_header_insert(req, "x-best-hdr", "best");
    vla_puts(req, "Paper ");
    vla_puts(req, "Scizzors");
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_and_print_order()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_and_print_order, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    TEST_ASSERT_EQUAL(504, res_code);
    helper_header_value_exists("x-best-hdr: ", "best");
    TEST_ASSERT_EQUAL_STRING("Rock Paper Scizzors", res_body);
}

void main()
{
    UNITY_BEGIN();

    RUN_TEST(test_header_insert);
    RUN_TEST(test_header_replace);
    RUN_TEST(test_header_append);
    RUN_TEST(test_header_append_multi);
    RUN_TEST(test_header_append_single);
    RUN_TEST(test_header_delete);
    RUN_TEST(test_header_delete_not_exist);
    RUN_TEST(test_header_multi);
    RUN_TEST(test_header_get);
    RUN_TEST(test_header_get_null);

    RUN_TEST(test_status);
    RUN_TEST(test_set_status);
    RUN_TEST(test_set_status_twice);
    RUN_TEST(test_get_status);

    RUN_TEST(test_set_content_type);
    RUN_TEST(test_get_content_type);
    RUN_TEST(test_get_content_type_null);

    RUN_TEST(test_printf);
    RUN_TEST(test_puts);
    RUN_TEST(test_write);
    RUN_TEST(test_write_binary);
    RUN_TEST(test_multi_print);

    RUN_TEST(test_header_and_print);
    RUN_TEST(test_header_and_print_order);

    UNITY_END();
}