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

typedef struct netscape_cookie_t
{
    char route[1024];
    int include_subdomains;
    char path[1024];
    int secure;
    long expiry;
    char name[1024];
    char value[1024];
} netscape_cookie_t;

void helper_convert_cookie(const char *raw, netscape_cookie_t *cookie)
{
    const char *term = strchr(raw, '\t');
    size_t l = term - raw;
    strncpy(cookie->route, raw, l);
    cookie->route[l] = '\0';
    raw = term + 1;

    cookie->include_subdomains = *raw == 'T';
    raw = strchr(raw, '\t') + 1;

    term = strchr(raw, '\t');
    l = term - raw;
    strncpy(cookie->path, raw, l);
    cookie->path[l] = '\0';
    raw = term + 1;

    cookie->secure = *raw == 'T';
    raw = strchr(raw, '\t') + 1;

    term = strchr(raw, '\t');
    l = term - raw;
    char buf[128];
    strncpy(buf, raw, l);
    buf[l] = '\0';
    cookie->expiry = strtol(buf, NULL, 10);
    raw = term + 1;

    term = strchr(raw, '\t');
    l = term - raw;
    strncpy(cookie->name, raw, l);
    cookie->name[l] = '\0';
    raw = term + 1;

    strcpy(cookie->value, raw);
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

    curl_easy_setopt(c, CURLOPT_COOKIEFILE, "");

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
        if (strncasecmp(res_hdrs[i], hdr, hdr_l))
        {
            continue;
        }
        const char *actual_val = &res_hdrs[i][hdr_l];
        if (strncmp(actual_val, val, val_l))
        {
            continue;
        }
        return;
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(NULL, "Header didn't exist");
}

/**
 * Verifies a header (not case sensative) and value (case sensative) combination
 * doesn't exist.
 */
void helper_header_value_not_exist(const char *hdr, const char *val)
{
    size_t hdr_l = strlen(hdr);
    size_t val_l = strlen(val);

    const char *actual_hdr = NULL;
    for (size_t i = 0; i < res_hdrs_size; ++i)
    {
        if (strncasecmp(res_hdrs[i], hdr, hdr_l))
        {
            continue;
        }
        const char *actual_val = &res_hdrs[i][hdr_l];
        if (strncmp(actual_val, val, val_l))
        {
            continue;
        }
        TEST_ASSERT_NOT_NULL_MESSAGE(NULL, "Header did exist");
    }
}

/**
 * Verifies that a header does not exist with any value.
 */
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

enum vla_handle_code handler_header_add(const vla_request *req, void *nul)
{
    int ret = vla_response_header_add(req, "X-Test-Header", "Cheese", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_add()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_add, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "Cheese");
}

enum vla_handle_code handler_header_add_duplicate(
    const vla_request *req,
    void *nul)
{
    int ret = vla_response_header_add(req, "X-Test-Header", "Cheese", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_add(req, "X-Test-Header", "Tacos", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_add_duplicate()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_add_duplicate, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "Cheese");
    helper_header_value_exists("x-test-header: ", "Tacos");
}

enum vla_handle_code handler_header_add_index(const vla_request *req, void *nul)
{
    size_t i = -1;
    int ret = vla_response_header_add(req, "X-Test-Header", "Cheese", &i);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_size_t(0, i);
    ret = vla_response_header_add(req, "X-Test-Header", "Tacos", &i);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_size_t(1, i);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_add_index()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_add_index, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "Cheese");
    helper_header_value_exists("x-test-header: ", "Tacos");
}

enum vla_handle_code handler_header_add_multi(const vla_request *req, void *nul)
{
    int ret = vla_response_header_add(req, "X-Test-Header", "Cheese", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_add(req, "X-Best-Header", "Tacos", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_add_multi()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_add_multi, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "Cheese");
    helper_header_value_exists("x-best-header: ", "Tacos");
}

enum vla_handle_code handler_header_replace(const vla_request *req, void *nul)
{
    size_t i;
    int ret = vla_response_header_add(req, "X-Test-Header", "Cheese", &i);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_replace(req, "X-Test-Header", "Tacos", i);
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

    helper_header_value_exists("x-test-header: ", "Tacos");
}

enum vla_handle_code handler_header_replace_not_exist(
    const vla_request *req,
    void *nul)
{
    int ret = vla_response_header_replace(req, "X-Test-Header", "Tacos", 0);
    TEST_ASSERT_EQUAL_INT(-1, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_replace_not_exist()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_replace_not_exist, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_not_exist("x-test-header: ");
}

enum vla_handle_code handler_header_replace_wrong_index(
    const vla_request *req,
    void *nul)
{
    size_t i = -1;
    int ret = vla_response_header_add(req, "X-Test-Header", "Cheese", &i);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_replace(req, "X-Test-Header", "Tacos", i + 1);
    TEST_ASSERT_EQUAL_INT(-1, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_replace_wrong_index()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_replace_wrong_index, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "Cheese");
}

enum vla_handle_code handler_header_replace_all(
    const vla_request *req,
    void *nul)
{
    int ret = vla_response_header_add(req, "X-Test-Header", "Cheese", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_add(req, "X-Test-Header", "Tacos", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_replace_all(req, "X-Test-Header", "Chicken");
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_replace_all()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_replace_all, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_not_exist("x-test-header: ", "Cheese");
    helper_header_value_not_exist("x-test-header: ", "Tacos");
    helper_header_value_exists("x-test-header: ", "Chicken");
}

enum vla_handle_code handler_header_replace_all_none(
    const vla_request *req,
    void *nul)
{
    int ret = vla_response_header_replace_all(req, "X-Test-Header", "Chicken");
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_replace_all_none()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_replace_all_none, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "Chicken");
}

enum vla_handle_code handler_header_replace_all_multi(
    const vla_request *req,
    void *nul)
{
    int ret = vla_response_header_add(req, "X-Test-Header", "Cheese", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_add(req, "X-Test-Header", "Tacos", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_replace_all(req, "X-Test-Header", "Chicken");
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_replace_all(req, "X-Best-Header", "Sandwich");
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_replace_all_multi()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_replace_all_multi, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_not_exist("x-test-header: ", "Cheese");
    helper_header_value_not_exist("x-test-header: ", "Tacos");
    helper_header_value_exists("x-test-header: ", "Chicken");
    helper_header_value_exists("x-best-header: ", "Sandwich");
}

enum vla_handle_code handler_header_remove(
    const vla_request *req,
    void *nul)
{
    size_t i = -1;
    int ret = vla_response_header_add(req, "X-Test-Header", "Cheese", &i);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_remove(req, "X-Test-Header", i);
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_remove()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_remove, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_not_exist("x-test-header: ");
}

enum vla_handle_code handler_header_remove_middle(
    const vla_request *req,
    void *nul)
{
    int ret = vla_response_header_add(req, "X-Test-Header", "Cheese", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    size_t i = -1;
    ret = vla_response_header_add(req, "X-Test-Header", "Tacos", &i);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_NOT_EQUAL_size_t(-1, i);
    ret = vla_response_header_add(req, "X-Test-Header", "Chicken", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_remove(req, "X-Test-Header", i);
    TEST_ASSERT_EQUAL_INT(0, ret);
    const char *val = vla_response_header_get(req, "X-Test-Header", i);
    TEST_ASSERT_EQUAL_STRING("Chicken", val);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_remove_middle()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_remove_middle, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "Cheese");
    helper_header_value_not_exist("x-test-header: ", "Tacos");
    helper_header_value_exists("x-test-header: ", "Chicken");
}

enum vla_handle_code handler_header_remove_doesnt_exist(
    const vla_request *req,
    void *nul)
{
    int ret = vla_response_header_remove(req, "X-Test-Header", 0);
    TEST_ASSERT_EQUAL_INT(-1, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_remove_doesnt_exist()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_remove_doesnt_exist, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_not_exist("x-test-header: ");
}

enum vla_handle_code handler_header_remove_wrong_index(
    const vla_request *req,
    void *nul)
{
    size_t i = -1;
    int ret = vla_response_header_add(req, "X-Test-Header", "Cheese", &i);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_remove(req, "X-Test-Header", i + 1);
    TEST_ASSERT_EQUAL_INT(-1, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_remove_wrong_index()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_remove_wrong_index, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "Cheese");
}

enum vla_handle_code handler_header_remove_then_add(
    const vla_request *req,
    void *nul)
{
    size_t i = -1;
    int ret = vla_response_header_add(req, "X-Test-Header", "Cheese", &i);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_remove(req, "X-Test-Header", i);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_add(req, "X-Test-Header", "Tacos", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_remove_then_add()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_remove_then_add, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_not_exist("x-test-header: ", "Cheese");
    helper_header_value_exists("x-test-header: ", "Tacos");
}

enum vla_handle_code handler_header_remove_all(
    const vla_request *req,
    void *nul)
{
    int ret = vla_response_header_add(req, "X-Test-Header", "Cheese", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_remove_all(req, "X-Test-Header");
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_remove_all()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_remove_all, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_not_exist("x-test-header: ");
}

enum vla_handle_code handler_header_remove_all_multi(
    const vla_request *req,
    void *nul)
{
    int ret = vla_response_header_add(req, "X-Test-Header", "Cheese", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_add(req, "X-Test-Header", "Tacos", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_remove_all(req, "X-Test-Header");
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_remove_all_multi()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_remove_all_multi, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_not_exist("x-test-header: ");
}

enum vla_handle_code handler_header_remove_all_not_exist(
    const vla_request *req,
    void *nul)
{
    int ret = vla_response_header_remove_all(req, "X-Test-Header");
    TEST_ASSERT_EQUAL_INT(-1, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_remove_all_not_exist()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_remove_all_not_exist, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_not_exist("x-test-header: ");
}

enum vla_handle_code handler_header_remove_all_then_add(
    const vla_request *req,
    void *nul)
{
    int ret = vla_response_header_add(req, "X-Test-Header", "Cheese", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_add(req, "X-Test-Header", "Tacos", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_remove_all(req, "X-Test-Header");
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_add(req, "X-Test-Header", "Beans", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_remove_all_then_add()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_remove_all_then_add, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_not_exist("x-test-header: ", "Cheese");
    helper_header_value_not_exist("x-test-header: ", "Tacos");
    helper_header_value_exists("x-test-header: ", "Beans");
}

enum vla_handle_code handler_header_get(const vla_request *req, void *nul)
{
    size_t i = -1;
    int ret = vla_response_header_add(req, "X-Test-Header", "Cheese", &i);
    TEST_ASSERT_EQUAL_INT(0, ret);
    const char *val = vla_response_header_get(req, "x-test-header", i);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQUAL_STRING("Cheese", val);
    ret = vla_free((char *)val);
    TEST_ASSERT_EQUAL_INT(0, ret);
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

    helper_header_value_exists("x-test-header: ", "Cheese");
}

enum vla_handle_code handler_header_get_multi(const vla_request *req, void *nul)
{
    int ret = vla_response_header_add(req, "X-Test-Header", "Cheese", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);

    ret = vla_response_header_add(req, "X-Test-Header", "Tacos", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);

    const char *val = vla_response_header_get(req, "x-test-header", 0);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQUAL_STRING("Cheese", val);
    ret = vla_free((char *)val);
    TEST_ASSERT_EQUAL_INT(0, ret);

    val = vla_response_header_get(req, "x-test-header", 1);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQUAL_STRING("Tacos", val);
    ret = vla_free((char *)val);
    TEST_ASSERT_EQUAL_INT(0, ret);

    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_get_multi()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_get_multi, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "Cheese");
    helper_header_value_exists("x-test-header: ", "Tacos");
}

enum vla_handle_code handler_header_get_not_exist(
    const vla_request *req,
    void *nul)
{
    const char *val = vla_response_header_get(req, "x-test-header", 0);
    TEST_ASSERT_NULL(val);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_get_not_exist()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_get_not_exist, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_not_exist("x-test-header: ");
}

enum vla_handle_code handler_header_get_after_remove(
    const vla_request *req,
    void *nul)
{
    size_t i = -1;
    int ret = vla_response_header_add(req, "x-test-header", "value", &i);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_NOT_EQUAL_size_t(-1, i);

    ret = vla_response_header_remove(req, "x-test-header", i);
    TEST_ASSERT_EQUAL_INT(0, ret);

    const char *val = vla_response_header_get(req, "x-test-header", 0);
    TEST_ASSERT_NULL(val);

    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_get_after_remove()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_get_after_remove, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_not_exist("x-test-header: ");
}

enum vla_handle_code handler_header_count(const vla_request *req, void *nul)
{
    int ret = vla_response_header_add(req, "x-test-header", "value", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);

    size_t size = -1;
    size = vla_response_header_count(req, "x-test-header");
    TEST_ASSERT_EQUAL_size_t(1, size);

    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_count()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_count, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "value");
}

enum vla_handle_code handler_header_count_multi(
    const vla_request *req,
    void *nul)
{
    int ret = vla_response_header_add(req, "x-test-header", "Cheese", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_add(req, "x-test-header", "Tacos", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_add(req, "x-test-header", "Chicken", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);

    size_t size = -1;
    size = vla_response_header_count(req, "x-test-header");
    TEST_ASSERT_EQUAL_size_t(3, size);

    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_count_multi()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_count_multi, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "Cheese");
    helper_header_value_exists("x-test-header: ", "Tacos");
    helper_header_value_exists("x-test-header: ", "Chicken");
}

enum vla_handle_code handler_header_count_not_exist(
    const vla_request *req,
    void *nul)
{
    size_t size = -1;
    size = vla_response_header_count(req, "x-test-header");
    TEST_ASSERT_EQUAL_size_t(0, size);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_count_not_exist()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_count_not_exist, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_not_exist("x-test-header: ");
}

enum vla_handle_code handler_header_count_after_remove(
    const vla_request *req,
    void *nul)
{
    int ret = vla_response_header_add(req, "x-test-header", "Cheese", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_add(req, "x-test-header", "Tacos", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_add(req, "x-test-header", "Chicken", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);

    size_t size = -1;
    size = vla_response_header_count(req, "x-test-header");
    TEST_ASSERT_EQUAL_size_t(3, size);

    ret = vla_response_header_remove(req, "x-test-header", 1);
    TEST_ASSERT_EQUAL_INT(0, ret);

    size = -1;
    size = vla_response_header_count(req, "x-test-header");
    TEST_ASSERT_EQUAL_size_t(2, size);

    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_count_after_remove()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_count_after_remove, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_exists("x-test-header: ", "Cheese");
    helper_header_value_not_exist("x-test-header: ", "Tacos");
    helper_header_value_exists("x-test-header: ", "Chicken");
}

enum vla_handle_code handler_header_count_after_remove_all(
    const vla_request *req,
    void *nul)
{
    int ret = vla_response_header_add(req, "x-test-header", "Cheese", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_add(req, "x-test-header", "Tacos", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = vla_response_header_add(req, "x-test-header", "Chicken", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);

    ret = vla_response_header_remove_all(req, "x-test-header");
    TEST_ASSERT_EQUAL_INT(0, ret);

    size_t size = -1;
    size = vla_response_header_count(req, "x-test-header");
    TEST_ASSERT_EQUAL_size_t(0, size);

    return VLA_HANDLE_RESPOND_TERM;
}

void test_header_count_after_remove_all()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_header_count_after_remove_all, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    helper_header_value_not_exist("x-test-header: ", "Cheese");
    helper_header_value_not_exist("x-test-header: ", "Tacos");
    helper_header_value_not_exist("x-test-header: ", "Chicken");
}

enum vla_handle_code handler_status(const vla_request *req, void *nul)
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

enum vla_handle_code handler_set_status(const vla_request *req, void *nul)
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

enum vla_handle_code handler_set_status_twice(const vla_request *req, void *nul)
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

enum vla_handle_code handler_get_status(const vla_request *req, void *nul)
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

enum vla_handle_code handler_set_content_type(const vla_request *req, void *nul)
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

enum vla_handle_code handler_get_content_type(const vla_request *req, void *nul)
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

enum vla_handle_code handler_get_content_type_null(const vla_request *req, void *nul)
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

enum vla_handle_code handler_set_cookie(const vla_request *req, void *nul)
{
    vla_cookie_t cookie;
    vla_init_cookie(&cookie);
    cookie.name = "TestCookie";
    cookie.value = "Value";
    int ret = vla_response_set_cookie(req, &cookie);
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_set_cookie()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_set_cookie, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    struct curl_slist *cookies = NULL;
    curl_easy_getinfo(c, CURLINFO_COOKIELIST, &cookies);

    TEST_ASSERT_NOT_NULL_MESSAGE(cookies, "Cookie list is NULL");
    TEST_ASSERT_NOT_NULL_MESSAGE(cookies->data, "Cookie data is NULL");

    netscape_cookie_t cookie;
    helper_convert_cookie(cookies->data, &cookie);
    TEST_ASSERT_EQUAL_STRING("localhost", cookie.route);
    TEST_ASSERT_EQUAL_INT(0, cookie.include_subdomains);
    TEST_ASSERT_EQUAL_STRING("/", cookie.path);
    TEST_ASSERT_EQUAL_INT(0, cookie.secure);
    TEST_ASSERT_EQUAL(0, cookie.expiry);
    TEST_ASSERT_EQUAL_STRING("TestCookie", cookie.name);
    TEST_ASSERT_EQUAL_STRING("Value", cookie.value);

    TEST_ASSERT_NULL_MESSAGE(cookies->next, "Cookie next is not NULL");

    curl_slist_free_all(cookies);
}

enum vla_handle_code handler_set_cookie_params(
    const vla_request *req,
    void *nul)
{
    vla_cookie_t cookie;
    vla_init_cookie(&cookie);
    cookie.name = "TestCookie";
    cookie.value = "Value";
    cookie.path = "/response";
    cookie.expires = 0x7FFFFFFF;
    cookie.httponly = 1;
    int ret = vla_response_set_cookie(req, &cookie);
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_set_cookie_params()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_set_cookie_params, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    struct curl_slist *cookies = NULL;
    curl_easy_getinfo(c, CURLINFO_COOKIELIST, &cookies);

    TEST_ASSERT_NOT_NULL_MESSAGE(cookies, "Cookie list is NULL");
    TEST_ASSERT_NOT_NULL_MESSAGE(cookies->data, "Cookie data is NULL");

    netscape_cookie_t cookie;
    helper_convert_cookie(cookies->data, &cookie);
    TEST_ASSERT_EQUAL_STRING("#HttpOnly_localhost", cookie.route);
    TEST_ASSERT_EQUAL_INT(0, cookie.include_subdomains);
    TEST_ASSERT_EQUAL_STRING("/response", cookie.path);
    TEST_ASSERT_EQUAL_INT(0, cookie.secure);
    TEST_ASSERT_EQUAL(0x7FFFFFFF, cookie.expiry);
    TEST_ASSERT_EQUAL_STRING("TestCookie", cookie.name);
    TEST_ASSERT_EQUAL_STRING("Value", cookie.value);

    TEST_ASSERT_NULL_MESSAGE(cookies->next, "Cookie next is not NULL");

    curl_slist_free_all(cookies);
}

enum vla_handle_code handler_set_cookie_params2(
    const vla_request *req,
    void *nul)
{
    vla_cookie_t cookie;
    vla_init_cookie(&cookie);
    cookie.name = "TestCookie";
    cookie.value = "Value";
    cookie.path = "/";
    cookie.httponly = 0;
    cookie.domain = "localhost";
    int ret = vla_response_set_cookie(req, &cookie);
    TEST_ASSERT_EQUAL_INT(0, ret);
    return VLA_HANDLE_RESPOND_TERM;
}

void test_set_cookie_params2()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_set_cookie_params2, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    struct curl_slist *cookies = NULL;
    curl_easy_getinfo(c, CURLINFO_COOKIELIST, &cookies);

    TEST_ASSERT_NOT_NULL_MESSAGE(cookies, "Cookie list is NULL");
    TEST_ASSERT_NOT_NULL_MESSAGE(cookies->data, "Cookie data is NULL");

    netscape_cookie_t cookie;
    helper_convert_cookie(cookies->data, &cookie);
    TEST_ASSERT_EQUAL_STRING(".localhost", cookie.route);
    TEST_ASSERT_EQUAL_INT(1, cookie.include_subdomains);
    TEST_ASSERT_EQUAL_STRING("/", cookie.path);
    TEST_ASSERT_EQUAL_INT(0, cookie.secure);
    TEST_ASSERT_EQUAL(0, cookie.expiry);
    TEST_ASSERT_EQUAL_STRING("TestCookie", cookie.name);
    TEST_ASSERT_EQUAL_STRING("Value", cookie.value);

    TEST_ASSERT_NULL_MESSAGE(cookies->next, "Cookie next is not NULL");

    curl_slist_free_all(cookies);
}

enum vla_handle_code handler_set_cookie_multi(const vla_request *req, void *nul)
{
    vla_cookie_t cookie;
    vla_init_cookie(&cookie);
    cookie.name = "Cookie1";
    cookie.value = "Value1";
    cookie.path = "/";
    cookie.domain = "localhost";
    int ret = vla_response_set_cookie(req, &cookie);
    TEST_ASSERT_EQUAL_INT(0, ret);

    vla_init_cookie(&cookie);
    cookie.name = "Cookie2";
    cookie.value = "Value2";
    cookie.path = "/request";
    cookie.httponly = 1;
    cookie.expires = 0x5FFFFFFF;
    ret = vla_response_set_cookie(req, &cookie);
    TEST_ASSERT_EQUAL_INT(0, ret);

    return VLA_HANDLE_RESPOND_TERM;
}

void test_set_cookie_multi()
{
    int ret = vla_add_route(
        ctx,
        VLA_HTTP_GET, "/response",
        handler_set_cookie_multi, NULL,
        NULL
    );
    TEST_ASSERT_EQUAL_INT(0, ret);

    start_request();

    struct curl_slist *cookies = NULL;
    curl_easy_getinfo(c, CURLINFO_COOKIELIST, &cookies);

    TEST_ASSERT_NOT_NULL_MESSAGE(cookies, "Cookie list is NULL");
    TEST_ASSERT_NOT_NULL_MESSAGE(cookies->data, "Cookie data is NULL");
    TEST_ASSERT_NOT_NULL_MESSAGE(cookies->next, "Next cookie doesn't exist");
    TEST_ASSERT_NOT_NULL_MESSAGE(
        cookies->next->data, "Next cookie data is NULL"
    );
    TEST_ASSERT_NULL_MESSAGE(
        cookies->next->next, "Cookie next next is not NULL"
    );

    netscape_cookie_t cookie;
    helper_convert_cookie(cookies->data, &cookie);
    TEST_ASSERT_EQUAL_STRING(".localhost", cookie.route);
    TEST_ASSERT_EQUAL_INT(1, cookie.include_subdomains);
    TEST_ASSERT_EQUAL_STRING("/", cookie.path);
    TEST_ASSERT_EQUAL_INT(0, cookie.secure);
    TEST_ASSERT_EQUAL(0, cookie.expiry);
    TEST_ASSERT_EQUAL_STRING("Cookie1", cookie.name);
    TEST_ASSERT_EQUAL_STRING("Value1", cookie.value);

    helper_convert_cookie(cookies->next->data, &cookie);
    TEST_ASSERT_EQUAL_STRING("#HttpOnly_localhost", cookie.route);
    TEST_ASSERT_EQUAL_INT(0, cookie.include_subdomains);
    TEST_ASSERT_EQUAL_STRING("/request", cookie.path);
    TEST_ASSERT_EQUAL_INT(0, cookie.secure);
    TEST_ASSERT_EQUAL(0x5FFFFFFF, cookie.expiry);
    TEST_ASSERT_EQUAL_STRING("Cookie2", cookie.name);
    TEST_ASSERT_EQUAL_STRING("Value2", cookie.value);

    curl_slist_free_all(cookies);
}

enum vla_handle_code handler_printf(const vla_request *req, void *nul)
{
    int ret = vla_printf(req, "%s\n%d", "Test", -3);
    TEST_ASSERT_EQUAL_INT(0, ret);
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

enum vla_handle_code handler_puts(const vla_request *req, void *nul)
{
    int ret = vla_puts(req, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    TEST_ASSERT_EQUAL_INT(0, ret);
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

enum vla_handle_code handler_write(const vla_request *req, void *nul)
{
    int ret = vla_write(req, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ", 10);
    TEST_ASSERT_EQUAL_INT(0, ret);
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

enum vla_handle_code handler_write_binary(const vla_request *req, void *nul)
{
    const char write[] = "\x00\x00\x00\x90\x90";
    int ret = vla_write(req, write, sizeof(write) - 1);
    TEST_ASSERT_EQUAL_INT(0, ret);
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

enum vla_handle_code handler_multi_print(const vla_request *req, void *nul)
{
    int ret = vla_puts(req, "1: puts\n");
    TEST_ASSERT_EQUAL_INT(0, ret);

    ret = vla_printf(req, "%u: %s%c", 2, "printf", '\n');
    TEST_ASSERT_EQUAL_INT(0, ret);

    char bin[] = "3: \x90\x90\x00\x27\xf7\x22";
    ret = vla_write(req, bin, sizeof(bin) - 1);
    TEST_ASSERT_EQUAL_INT(0, ret);

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

enum vla_handle_code handler_header_and_print(const vla_request *req, void *nul)
{
    vla_response_set_status_code(req, 301);
    vla_response_header_add(req, "x-test-header", "test", NULL);
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

enum vla_handle_code handler_header_and_print_order(
    const vla_request *req,
    void *nul)
{
    vla_puts(req, "Rock ");
    vla_response_set_status_code(req, 504);
    vla_response_header_add(req, "x-best-hdr", "best", NULL);
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

    RUN_TEST(test_header_add);
    RUN_TEST(test_header_add_duplicate);
    RUN_TEST(test_header_add_index);
    RUN_TEST(test_header_add_multi);

    RUN_TEST(test_header_replace);
    RUN_TEST(test_header_replace_not_exist);
    RUN_TEST(test_header_replace_wrong_index);

    RUN_TEST(test_header_replace_all);
    RUN_TEST(test_header_replace_all_none);
    RUN_TEST(test_header_replace_all_multi);

    RUN_TEST(test_header_remove);
    RUN_TEST(test_header_remove_middle);
    RUN_TEST(test_header_remove_doesnt_exist);
    RUN_TEST(test_header_remove_wrong_index);
    RUN_TEST(test_header_remove_then_add);

    RUN_TEST(test_header_remove_all);
    RUN_TEST(test_header_remove_all_multi);
    RUN_TEST(test_header_remove_all_not_exist);
    RUN_TEST(test_header_remove_all_then_add);

    RUN_TEST(test_header_get);
    RUN_TEST(test_header_get_multi);
    RUN_TEST(test_header_get_not_exist);
    RUN_TEST(test_header_get_after_remove);

    RUN_TEST(test_header_count);
    RUN_TEST(test_header_count_multi);
    RUN_TEST(test_header_count_not_exist);
    RUN_TEST(test_header_count_after_remove);
    RUN_TEST(test_header_count_after_remove_all);

    RUN_TEST(test_status);
    RUN_TEST(test_set_status);
    RUN_TEST(test_set_status_twice);
    RUN_TEST(test_get_status);

    RUN_TEST(test_set_content_type);
    RUN_TEST(test_get_content_type);
    RUN_TEST(test_get_content_type_null);

    RUN_TEST(test_set_cookie);
    RUN_TEST(test_set_cookie_params);
    RUN_TEST(test_set_cookie_params2);
    RUN_TEST(test_set_cookie_multi);

    RUN_TEST(test_printf);
    RUN_TEST(test_puts);
    RUN_TEST(test_write);
    RUN_TEST(test_write_binary);
    RUN_TEST(test_multi_print);

    RUN_TEST(test_header_and_print);
    RUN_TEST(test_header_and_print_order);

    UNITY_END();
}