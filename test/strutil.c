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

#include <string.h>

#include <talloc.h>

#include "../src/strutil.h"

void setUp(void)
{

}

void tearDown(void)
{

}

void test_strchrnul_found()
{
    const char *str = "0123456789";
    const char *ch = su_strchrnul(str, '7');
    TEST_ASSERT_EQUAL_PTR(&str[7], ch);
    TEST_ASSERT_EQUAL_CHAR('7', *ch);
}

void test_strchrnul_not_found()
{
    const char *str = "0123456789";
    const char *ch = su_strchrnul(str, 'A');
    TEST_ASSERT_EQUAL_PTR(&str[10], ch);
    TEST_ASSERT_EQUAL_CHAR('\0', *ch);
}

void test_strchrnul_empty()
{
    const char *str = "";
    const char *ch = su_strchrnul(str, 'A');
    TEST_ASSERT_EQUAL_PTR(str, ch);
    TEST_ASSERT_EQUAL_CHAR('\0', *ch);
}

void test_url_encode()
{
    const char *str = "test";
    char *res = su_url_encode(str);
    TEST_ASSERT_NOT_NULL(res);
    TEST_ASSERT_EQUAL_STRING(str, res);
    talloc_free(res);
}

void test_url_encode_empty()
{
    const char *str = "";
    char *res = su_url_encode(str);
    TEST_ASSERT_NOT_NULL(res);
    TEST_ASSERT_EQUAL_STRING(str, res);
    talloc_free(res);
}

void test_url_encode_utf8()
{
    const char *str = "/テスト/";
    const char *enc = "%2F%E3%83%86%E3%82%B9%E3%83%88%2F";
    char *res = su_url_encode(str);
    TEST_ASSERT_NOT_NULL(res);
    TEST_ASSERT_EQUAL_STRING(enc, res);
    talloc_free(res);
}

void test_url_encode_capture_char()
{
    const char *str = "/test/:";
    const char *enc = "%2Ftest%2F%3A";
    char *res = su_url_encode(str);
    TEST_ASSERT_NOT_NULL(res);
    TEST_ASSERT_EQUAL_STRING(enc, res);
    talloc_free(res);
}

void test_url_encode_match_char()
{
    const char *str = "/test/*";
    const char *enc = "%2Ftest%2F%2A";
    char *res = su_url_encode(str);
    TEST_ASSERT_NOT_NULL(res);
    TEST_ASSERT_EQUAL_STRING(enc, res);
    talloc_free(res);
}

void test_url_encode_general()
{
    const char *str = "/a real ながい string/:";
    const char *enc = "%2Fa+real+%E3%81%AA%E3%81%8C%E3%81%84+string%2F%3A";
    char *res = su_url_encode(str);
    TEST_ASSERT_NOT_NULL(res);
    TEST_ASSERT_EQUAL_STRING(enc, res);
    talloc_free(res);
}

void test_url_encode_l()
{
    const char *str = "/test/tea and :biscuits/";
    const char *enc = "%2Ftest%2Ftea+and+";
    char *res = su_url_encode_l(str, strrchr(str, ':') - str);
    TEST_ASSERT_NOT_NULL(res);
    TEST_ASSERT_EQUAL_STRING(enc, res);
    talloc_free(res);
}

void test_url_decode()
{
    const char *enc = "%2F%E3%83%86%E3%82%B9%E3%83%88%2F";
    const char *dec = "/テスト/";
    char *res = su_url_decode(enc);
    TEST_ASSERT_NOT_NULL(res);
    TEST_ASSERT_EQUAL_STRING(dec, res);
    talloc_free(res);
}

void test_url_decode_empty()
{
    const char *enc = "";
    const char *dec = "";
    char *res = su_url_decode(enc);
    TEST_ASSERT_NOT_NULL(res);
    TEST_ASSERT_EQUAL_STRING(dec, res);
    talloc_free(res);
}

void test_url_decode_general()
{
    const char *enc = "%2Fa+real+%E3%81%AA%E3%81%8C%E3%81%84+string%2F%3A";
    const char *dec = "/a real ながい string/:";
    char *res = su_url_decode(enc);
    TEST_ASSERT_NOT_NULL(res);
    TEST_ASSERT_EQUAL_STRING(dec, res);
    talloc_free(res);
}

void test_url_decode_l()
{
    const char *enc = "%2Fa+real+%E3%81%AA%E3%81%8C%E3%81%84+string%2F%3A";
    size_t enc_l = sizeof("%2Fa+real+%E3%81%AA%E3%81%8C%E3%81%84+") - 1;
    const char *dec = "/a real ながい ";
    char *res = su_url_decode_l(enc, enc_l);
    TEST_ASSERT_NOT_NULL(res);
    TEST_ASSERT_EQUAL_STRING(dec, res);
    talloc_free(res);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_strchrnul_found);
    RUN_TEST(test_strchrnul_not_found);
    RUN_TEST(test_strchrnul_empty);

    RUN_TEST(test_url_encode);
    RUN_TEST(test_url_encode_empty);
    RUN_TEST(test_url_encode_utf8);
    RUN_TEST(test_url_encode_capture_char);
    RUN_TEST(test_url_encode_match_char);

    RUN_TEST(test_url_encode_l);

    RUN_TEST(test_url_decode);
    RUN_TEST(test_url_decode_empty);
    RUN_TEST(test_url_decode_general);

    RUN_TEST(test_url_decode_l);

    return UNITY_END();
}