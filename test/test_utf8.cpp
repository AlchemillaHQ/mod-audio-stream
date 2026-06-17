#include "test.h"
#include "stream_utils.h"

TEST(valid_ascii) {
    CHECK(is_valid_utf8("hello world") == 1);
}

TEST(valid_2byte) {
    /* U+00A9 = © */
    CHECK(is_valid_utf8("\xC2\xA9") == 1);
}

TEST(valid_3byte) {
    /* U+20AC = € */
    CHECK(is_valid_utf8("\xE2\x82\xAC") == 1);
}

TEST(valid_4byte) {
    /* U+1F600 = 😀 */
    CHECK(is_valid_utf8("\xF0\x9F\x98\x80") == 1);
}

TEST(valid_mixed) {
    CHECK(is_valid_utf8("Hello \xC2\xA9 World \xE2\x82\xAC!") == 1);
}

TEST(invalid_truncated_2byte) {
    CHECK(is_valid_utf8("\xC2") == 0);
}

TEST(invalid_bad_continuation) {
    CHECK(is_valid_utf8("\xC2\x20") == 0);
}

TEST(invalid_truncated_3byte) {
    CHECK(is_valid_utf8("\xE2\x82") == 0);
}

TEST(invalid_truncated_4byte) {
    CHECK(is_valid_utf8("\xF0\x9F\x98") == 0);
}

TEST(invalid_0xFE) {
    CHECK(is_valid_utf8("\xFE") == 0);
}

TEST(invalid_0xFF) {
    CHECK(is_valid_utf8("\xFF") == 0);
}

TEST(invalid_overlong) {
    CHECK(is_valid_utf8("\xC0\xAF") == 0);
}

TEST(invalid_surrogate_high) {
    /* U+D800 = \xED\xA0\x80 */
    CHECK(is_valid_utf8("\xED\xA0\x80") == 0);
}

TEST(invalid_surrogate_low) {
    /* U+DFFF = \xED\xBF\xBF */
    CHECK(is_valid_utf8("\xED\xBF\xBF") == 0);
}

TEST(invalid_overlong_3byte) {
    /* overlong encoding of U+007F */
    CHECK(is_valid_utf8("\xE0\x81\xBF") == 0);
}

TEST(invalid_overlong_4byte) {
    /* overlong encoding of U+07FF */
    CHECK(is_valid_utf8("\xF0\x80\x9F\xBF") == 0);
}

TEST(invalid_above_10FFFF) {
    /* \xF4\x90\x80\x80 encodes > U+10FFFF */
    CHECK(is_valid_utf8("\xF4\x90\x80\x80") == 0);
}

TEST(invalid_continuation_at_start) {
    CHECK(is_valid_utf8("\x80\x80") == 0);
}

TEST(invalid_mixed_truncated) {
    CHECK(is_valid_utf8("hello\xC2") == 0);
}

TEST(valid_empty) {
    CHECK(is_valid_utf8("") == 1);
}

TEST_SUITE_END() {
    RUN_TEST(valid_ascii);
    RUN_TEST(valid_2byte);
    RUN_TEST(valid_3byte);
    RUN_TEST(valid_4byte);
    RUN_TEST(valid_mixed);
    RUN_TEST(invalid_truncated_2byte);
    RUN_TEST(invalid_bad_continuation);
    RUN_TEST(invalid_truncated_3byte);
    RUN_TEST(invalid_truncated_4byte);
    RUN_TEST(invalid_0xFE);
    RUN_TEST(invalid_0xFF);
    RUN_TEST(invalid_overlong);
    RUN_TEST(invalid_surrogate_high);
    RUN_TEST(invalid_surrogate_low);
    RUN_TEST(invalid_overlong_3byte);
    RUN_TEST(invalid_overlong_4byte);
    RUN_TEST(invalid_above_10FFFF);
    RUN_TEST(invalid_continuation_at_start);
    RUN_TEST(invalid_mixed_truncated);
    RUN_TEST(valid_empty);
}

TEST_MAIN()
