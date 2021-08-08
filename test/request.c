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

    /* Cookies to set */
    const char *cookies;

    /* Body of the request. */
    const char *body;
} request_params;

static vla_context *ctx = NULL;

static request_params r_params;

static CURL *c = NULL;

static char *res_body = NULL;

void setUp(void)
{
    r_params.url = "http://localhost/request";
    r_params.method = "GET";
    r_params.headers = NULL;
    r_params.cookies = NULL;
    r_params.body = NULL;

    c = curl_easy_init();
    TEST_ASSERT_NOT_NULL(c);

    ctx = vla_init();
    TEST_ASSERT_NOT_NULL(ctx);

    res_body = NULL;
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

    talloc_free(res_body);
    res_body = NULL;
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
    if (r_params.cookies)
        curl_easy_setopt(c, CURLOPT_COOKIE, r_params.cookies);
    if (r_params.body)
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, r_params.body);

    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, read_body);

    CURLcode code = curl_easy_perform(c);
    TEST_ASSERT_EQUAL(CURLE_OK, code);

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

enum vla_handle_code handler_get_query(const vla_request *req, void *nul)
{
    const char *val = vla_request_query_get(req, "key");
    TEST_ASSERT_EQUAL_STRING("val", val);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_query()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_get_query, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.url = "http://localhost/request?key=val";

    start_request();
}

enum vla_handle_code handler_get_query_utf8(const vla_request *req, void *nul)
{
    const char *val = vla_request_query_get(req, "かぎ");
    TEST_ASSERT_EQUAL_STRING("値", val);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_query_utf8()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_get_query_utf8, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.url = "http://localhost/request?かぎ=値";

    start_request();
}

enum vla_handle_code handler_get_query_multi(const vla_request *req, void *nul)
{
    const char *val = vla_request_query_get(req, "key1");
    TEST_ASSERT_EQUAL_STRING("val1", val);
    val = vla_request_query_get(req, "key2");
    TEST_ASSERT_EQUAL_STRING("val2", val);
    val = vla_request_query_get(req, "Key3");
    TEST_ASSERT_EQUAL_STRING("Val3", val);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_query_multi()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_get_query_multi, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.url = "http://localhost/request?key1=val1&key2=val2&Key3=Val3";

    start_request();
}

enum vla_handle_code handler_get_query_case(const vla_request *req, void *nul)
{
    const char *val = vla_request_query_get(req, "vAl1");
    TEST_ASSERT_NULL(val);
    val = vla_request_query_get(req, "VAL2");
    TEST_ASSERT_NULL(val);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_query_case()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_get_query_case, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.url = "http://localhost/request?val1=key1&val2=key2";

    start_request();
}

enum vla_handle_code handler_get_query_missing(
    const vla_request *req,
    void *nul)
{
    const char *val = vla_request_query_get(req, "fake");
    TEST_ASSERT_NULL(val);
    val = vla_request_query_get(req, "false");
    TEST_ASSERT_NULL(val);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_query_missing()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_get_query_missing, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.url = "http://localhost/request?key1=val1&key2=val2";

    start_request();
}

int callback_query_iterate(const char *key, const char *val, void *num)
{
    static const char *lastkey = NULL;
    static const char *lastval = NULL;

    size_t *count = num;
    ++*count;

    if (lastkey)
    {
        int res = strcmp(key, lastkey);
        TEST_ASSERT_NOT_EQUAL(0, res);
    }
    if (lastval)
    {
        int res = strcmp(val, lastval);
        TEST_ASSERT_NOT_EQUAL(0, res);
    }

    lastkey = key;
    lastval = val;

    return 0;
}

enum vla_handle_code handler_query_iterate(const vla_request *req, void *num)
{
    int ret = vla_request_query_iterate(req, callback_query_iterate, num);
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_query_iterate()
{
    size_t count = 0;
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_query_iterate, &count,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.url = "http://localhost/request?key1=val1&key2=val2";

    start_request();

    TEST_ASSERT_EQUAL(2, count);
}

int callback_query_iterate_early(const char *key, const char *val, void *num)
{
    size_t *count = num;
    ++*count;
    return -1;
}

enum vla_handle_code handler_query_iterate_early(
    const vla_request *req,
    void *num)
{
    int ret = vla_request_query_iterate(req, callback_query_iterate_early, num);
    TEST_ASSERT_EQUAL_INT(1, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_query_iterate_early()
{
    size_t count = 0;
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_query_iterate_early, &count,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.url = "http://localhost/request?key1=val1&key2=val2&key3=val3";

    start_request();

    TEST_ASSERT_EQUAL(1, count);
}

int callback_query_iterate_empty(const char *key, const char *val, void *num)
{
    size_t *count = num;
    ++*count;
    return 0;
}

enum vla_handle_code handler_query_iterate_empty(
    const vla_request *req,
    void *num)
{
    int ret = vla_request_query_iterate(req, callback_query_iterate_empty, num);
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_query_iterate_empty()
{
    size_t count = 0;
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_query_iterate_empty, &count,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    TEST_ASSERT_EQUAL(0, count);
}

enum vla_handle_code handler_get_header(const vla_request *req, void *nul)
{
    const char *val = vla_request_header_get(req, "x-test-header");
    TEST_ASSERT_EQUAL_STRING("test", val);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_header()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_get_header, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.headers = curl_slist_append(
        r_params.headers, "x-test-header: test"
    );

    start_request();
}

void test_get_header_case()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_get_header, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.headers = curl_slist_append(
        r_params.headers, "X-Test-Header: test"
    );

    start_request();
}

enum vla_handle_code handler_get_header_missing(
    const vla_request *req,
    void *nul)
{
    const char *val = vla_request_header_get(req, "x-test-header");
    TEST_ASSERT_NULL(val);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_header_missing()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_get_header_missing, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();
}

enum vla_handle_code handler_get_cookie(const vla_request *req, void *nul)
{
    const char *val = vla_request_cookie_get(req, "name");
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQUAL_STRING("value", val);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_cookie()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_get_cookie, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.cookies = "name=value";

    start_request();
}

void test_get_cookie_alt()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_get_cookie, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.cookies = "name=value;";

    start_request();
}

void test_get_cookie_alt2()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_get_cookie, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.cookies = "name=value; ";

    start_request();
}

void test_get_cookie_alt3()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_get_cookie, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.cookies = "name=value;         ";

    start_request();
}

enum vla_handle_code handler_get_cookie_multi(
    const vla_request *req,
    void *nul)
{
    const char *val = vla_request_cookie_get(req, "first");
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQUAL_STRING("fval", val);

    val = vla_request_cookie_get(req, "second");
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQUAL_STRING("sval", val);

    val = vla_request_cookie_get(req, "third");
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQUAL_STRING("tval", val);

    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_cookie_multi()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_get_cookie_multi, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.cookies = "first=fval; second=sval; third=tval";

    start_request();
}

void test_get_cookie_multi_alt()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_get_cookie_multi, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.cookies = "first=fval; second=sval; third=tval;";

    start_request();
}

void test_get_cookie_multi_alt2()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_get_cookie_multi, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.cookies = "first=fval; second=sval;      third=tval";

    start_request();
}

void test_get_cookie_multi_alt3()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_get_cookie_multi, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.cookies = "first=fval; second=sval; third=tval;           ";

    start_request();
}

enum vla_handle_code handler_get_cookie_not_exist(
    const vla_request *req,
    void *nul)
{
    const char *val = vla_request_cookie_get(req, "name");
    TEST_ASSERT_NULL(val);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_cookie_not_exist()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_get_cookie_not_exist, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();
}

int callback_cookie_iterate(const char *name, const char *val, void *ptr)
{
    int *actual = ptr;
    if (strcmp("one", name) == 0 && strcmp("val1", val) == 0)
    {
        actual[0] = 1;
    }
    else if (strcmp("two", name) == 0 && strcmp("val2", val) == 0)
    {
        actual[1] = 1;
    }
    else if (strcmp("three", name) == 0 && strcmp("val3", val) == 0)
    {
        actual[2] = 1;
    }
    return 0;
}

enum vla_handle_code handler_cookie_iterate(const vla_request *req, void *arr)
{
    int ret = vla_request_cookie_iterate(req, callback_cookie_iterate, arr);
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_cookie_iterate()
{
    r_params.cookies = "one=val1; two=val2; three=val3";
    int actual[] = {0, 0, 0};
    const int expected[] = {1, 1, 1};

    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_cookie_iterate, actual,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    TEST_ASSERT_EQUAL_INT_ARRAY(expected, actual, 3);
}

int callback_cookie_iterate_early(const char *name, const char *val, void *ptr)
{
    size_t *i = ptr;
    if (++*i == 2)
    {
        return 1;
    }
    return 0;
}

enum vla_handle_code handler_cookie_iterate_early(
    const vla_request *req,
    void *nul)
{
    size_t i = 0;
    int ret = vla_request_cookie_iterate(
        req, callback_cookie_iterate_early, &i
    );
    TEST_ASSERT_EQUAL_INT(1, ret);
    TEST_ASSERT_EQUAL_size_t(2, i);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_cookie_iterate_early()
{
    r_params.cookies = "one=val1; two=val2; three=val3";

    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_cookie_iterate_early, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();
}

int callback_cookie_iterate_empty(const char *name, const char *val, void *ptr)
{
    size_t *i = ptr;
    ++*i;
    return 0;
}

enum vla_handle_code handler_cookie_iterate_empty(
    const vla_request *req,
    void *nul)
{
    size_t i = 0;
    int ret = vla_request_cookie_iterate(
        req, callback_cookie_iterate_empty, &i
    );
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_size_t(0, i);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_cookie_iterate_empty()
{
    r_params.cookies = "";

    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_cookie_iterate_empty, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();
}

enum vla_handle_code handler_get_body(const vla_request *req, void *nul)
{
    const char *body = vla_request_body_get(req, 0);
    TEST_ASSERT_EQUAL_STRING(r_params.body, body);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_body()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_POST, route,
        handler_get_body, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.method = "POST";
    r_params.body = "Tea and Honey";

    start_request();
}

enum vla_handle_code handler_get_body_empty(const vla_request *req, void *nul)
{
    const char *body = vla_request_body_get(req, 0);
    TEST_ASSERT_EQUAL_STRING(r_params.body, body);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_body_empty()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_POST, route,
        handler_get_body_empty, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.method = "POST";
    r_params.body = "";

    start_request();
}

enum vla_handle_code handler_get_body_length(const vla_request *req, void *nul)
{
    const char *body = vla_request_body_get(req, 3);
    TEST_ASSERT_EQUAL_STRING("Tea", body);
    size_t len = vla_request_body_get_length(req);
    TEST_ASSERT_EQUAL(3, len);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_body_length()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_POST, route,
        handler_get_body_length, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.method = "POST";
    r_params.body = "Tea and Honey";

    start_request();
}

enum vla_handle_code handler_get_body_length_repeat(
    const vla_request *req,
    void *nul)
{
    const char *body = vla_request_body_get(req, 3);
    TEST_ASSERT_EQUAL_STRING("Tea", body);
    size_t len = vla_request_body_get_length(req);
    TEST_ASSERT_EQUAL(3, len);

    body = vla_request_body_get(req, 0);
    TEST_ASSERT_EQUAL_STRING("Tea", body);
    len = vla_request_body_get_length(req);
    TEST_ASSERT_EQUAL(3, len);

    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_body_length_repeat()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_POST, route,
        handler_get_body_length_repeat, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.method = "POST";
    r_params.body = "Tea and Honey";

    start_request();
}

enum vla_handle_code handler_get_body_length_gt(
    const vla_request *req,
    void *nul)
{
    const char *body = vla_request_body_get(req, 200);
    TEST_ASSERT_EQUAL_STRING(r_params.body, body);
    size_t len = vla_request_body_get_length(req);
    TEST_ASSERT_EQUAL(strlen(r_params.body), len);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_body_length_gt()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_POST, route,
        handler_get_body_length_gt, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.method = "POST";
    r_params.body = "Tea and Honey";

    start_request();
}

enum vla_handle_code handler_get_body_chunk(const vla_request *req, void *nul)
{
    char buf[256];
    size_t read = 0;

    read += vla_request_body_chunk(req, &buf[read], 3);
    TEST_ASSERT_EQUAL_size_t(3, read);
    TEST_ASSERT_EQUAL_CHAR_ARRAY("Tea", buf, read);

    read += vla_request_body_chunk(req, &buf[read], 4);
    TEST_ASSERT_EQUAL_size_t(7, read);
    TEST_ASSERT_EQUAL_CHAR_ARRAY("Tea and", buf, read);

    read += vla_request_body_chunk(req, &buf[read], 6);
    TEST_ASSERT_EQUAL_size_t(13, read);
    TEST_ASSERT_EQUAL_CHAR_ARRAY("Tea and Honey", buf, read);

    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_body_chunk()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_POST, route,
        handler_get_body_chunk, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.method = "POST";
    r_params.body = "Tea and Honey";

    start_request();
}

enum vla_handle_code handler_get_body_chunk_gt(
    const vla_request *req,
    void *nul)
{
    char buf[256];
    size_t read;

    read = vla_request_body_chunk(req, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(strlen(r_params.body), read);
    TEST_ASSERT_EQUAL_CHAR_ARRAY(r_params.body, buf, read);

    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_body_chunk_gt()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_POST, route,
        handler_get_body_chunk_gt, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.method = "POST";
    r_params.body = "Chunk and Chunkier";

    start_request();
}

enum vla_handle_code handler_get_body_chunk_empty(
    const vla_request *req,
    void *nul)
{
    char buf[256];
    size_t read;

    read = vla_request_body_chunk(req, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(strlen(r_params.body), read);

    return VLA_HANDLE_RESPOND_TERM;
}

void test_get_body_chunk_empty()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_POST, route,
        handler_get_body_chunk_empty, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    r_params.method = "POST";
    r_params.body = "";

    start_request();
}

enum vla_handle_code handler_getenv(const vla_request *req, void *nul)
{
    const char *body = vla_request_getenv(req, "REMOTE_ADDR");
    TEST_ASSERT_EQUAL_STRING("127.0.0.1", body);
    body = vla_request_getenv(req, "DOESNT_EXIST");
    TEST_ASSERT_NULL(body);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_getenv()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_getenv, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();
}

int callback_env_iterate(const char *key, const char *val, void *num)
{
    size_t *count = num;
    ++*count;
    return 0;
}

enum vla_handle_code handler_env_iterate(const vla_request *req, void *num)
{
    size_t count = 0;
    int ret = vla_request_env_iterate(req, callback_env_iterate, &count);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_GREATER_THAN_INT(0, count);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_env_iterate()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_env_iterate, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();
}

int callback_env_iterate_early(const char *key, const char *val, void *num)
{
    size_t *count = num;
    ++*count;
    return -1;
}

enum vla_handle_code handler_env_iterate_early(
    const vla_request *req,
    void *num)
{
    size_t count = 0;
    int ret = vla_request_env_iterate(req, callback_env_iterate_early, &count);
    TEST_ASSERT_EQUAL_INT(1, ret);
    TEST_ASSERT_EQUAL_INT(1, count);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_env_iterate_early()
{
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_env_iterate_early, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();
}

enum vla_handle_code handler_middleware(const vla_request *req, void *num)
{
    vla_response_set_content_type(req, "text/plain");
    vla_puts(req, "Success!");
    return VLA_HANDLE_RESPOND_TERM;
}

enum vla_handle_code middleware_middleware(const vla_request *req, void *num)
{
    size_t *count = num;
    ++*count;
    return vla_request_next_func(req);
}

void test_middleware()
{
    size_t count = 0;
    const char *route = "/request";
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, route,
        handler_middleware, NULL,
        middleware_middleware, &count,
        middleware_middleware, &count,
        middleware_middleware, &count,
        middleware_middleware, &count,
        middleware_middleware, &count,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    TEST_ASSERT_EQUAL_size_t(5, count);
    TEST_ASSERT_EQUAL_STRING("Success!", res_body);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_get_query);
    RUN_TEST(test_get_query_utf8);
    RUN_TEST(test_get_query_multi);
    RUN_TEST(test_get_query_case);
    RUN_TEST(test_get_query_missing);

    RUN_TEST(test_query_iterate);
    RUN_TEST(test_query_iterate_early);
    RUN_TEST(test_query_iterate_empty);

    RUN_TEST(test_get_header);
    RUN_TEST(test_get_header_case);
    RUN_TEST(test_get_header_missing);

    RUN_TEST(test_get_cookie);
    RUN_TEST(test_get_cookie_alt);
    RUN_TEST(test_get_cookie_alt2);
    RUN_TEST(test_get_cookie_alt3);
    RUN_TEST(test_get_cookie_multi);
    RUN_TEST(test_get_cookie_multi_alt);
    RUN_TEST(test_get_cookie_multi_alt2);
    RUN_TEST(test_get_cookie_multi_alt3);
    RUN_TEST(test_get_cookie_not_exist);

    RUN_TEST(test_cookie_iterate);
    RUN_TEST(test_cookie_iterate_early);
    RUN_TEST(test_cookie_iterate_empty);

    RUN_TEST(test_get_body);
    RUN_TEST(test_get_body_empty);
    RUN_TEST(test_get_body_length);
    RUN_TEST(test_get_body_length_gt);
    RUN_TEST(test_get_body_length_repeat);

    RUN_TEST(test_get_body_chunk);
    RUN_TEST(test_get_body_chunk_gt);
    RUN_TEST(test_get_body_chunk_empty);

    RUN_TEST(test_getenv);

    RUN_TEST(test_env_iterate);
    RUN_TEST(test_env_iterate_early);

    RUN_TEST(test_middleware);

    return UNITY_END();
}