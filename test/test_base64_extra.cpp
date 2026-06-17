/* Additional base64 tests */
#include "test.h"
#include "base64.h"
#include <string>
#include <cstring>

using std::string;

TEST(decode_url_safe) {
    /* URL-safe base64 uses - instead of + and _ instead of / and . instead of = */
    string encoded = "a-_9";  // represents specific bytes
    bool threw = false;
    try {
        base64_decode(encoded, false);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(!threw);  /* should accept URL-safe variants */
}

TEST(encode_empty_bytes) {
    unsigned char data[] = {};
    string encoded = base64_encode(data, 0, false);
    CHECK(encoded.empty());
}

TEST(decode_empty) {
    string decoded = base64_decode("", false);
    CHECK(decoded.empty());
}

TEST(decode_single_char) {
    /* Single base64 character is invalid (needs at least 2 for 1 output byte) */
    bool threw = false;
    try {
        base64_decode("A", false);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(encode_binary_zeroes) {
    unsigned char data[256] = {};
    string encoded = base64_encode(data, sizeof(data), false);
    string decoded = base64_decode(encoded, false);
    CHECK(decoded.size() == sizeof(data));
    for (size_t i = 0; i < decoded.size(); i++) {
        CHECK((unsigned char)decoded[i] == 0);
    }
}

TEST(encode_binary_all_bytes) {
    unsigned char data[256];
    for (int i = 0; i < 256; i++) data[i] = (unsigned char)i;
    string encoded = base64_encode(data, sizeof(data), false);
    string decoded = base64_decode(encoded, false);
    CHECK(decoded.size() == sizeof(data));
    CHECK(memcmp(decoded.data(), data, sizeof(data)) == 0);
}

TEST(encode_decode_unicode_string) {
    /* String with various byte values */
    string original = "Hello\x00World\xFF\xFE\x7F\x80!";
    string encoded = base64_encode(original, false);
    string decoded = base64_decode(encoded, false);
    CHECK(decoded == original);
}

TEST(url_safe_roundtrip) {
    string original = "test";
    string encoded = base64_encode(original, true);
    string decoded = base64_decode(encoded, false);
    CHECK(decoded == original);
}

TEST_SUITE_END() {
    RUN_TEST(decode_url_safe);
    RUN_TEST(encode_empty_bytes);
    RUN_TEST(decode_empty);
    RUN_TEST(decode_single_char);
    RUN_TEST(encode_binary_zeroes);
    RUN_TEST(encode_binary_all_bytes);
    RUN_TEST(encode_decode_unicode_string);
    RUN_TEST(url_safe_roundtrip);
}

TEST_MAIN()
