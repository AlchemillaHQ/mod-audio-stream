#include "test.h"
#include "base64.h"
#include <string>
#include <cstring>

using std::string;

TEST(encode_empty) {
    string encoded = base64_encode(string(""), false);
    CHECK(encoded.empty());
}

TEST(encode_decode_roundtrip) {
    const string original = "Hello, World! This is a test of base64 encoding/decoding.";
    string encoded = base64_encode(original, false);
    string decoded = base64_decode(encoded, false);
    CHECK(original == decoded);
}

TEST(encode_decode_binary) {
    unsigned char data[] = {0x00, 0x01, 0x02, 0x7F, 0x80, 0xFE, 0xFF};
    string encoded = base64_encode(data, sizeof(data), false);
    string decoded = base64_decode(encoded, false);
    CHECK(decoded.size() == sizeof(data));
    CHECK(memcmp(decoded.data(), data, sizeof(data)) == 0);
}

TEST(encode_known_vector) {
    /* "Man" → "TWFu" (RFC 4648 test vector) */
    string encoded = base64_encode(string("Man"), false);
    CHECK(encoded == "TWFu");
}

TEST(encode_known_vector_2) {
    /* "Ma" → "TWE=" */
    string encoded = base64_encode(string("Ma"), false);
    CHECK(encoded == "TWE=");
}

TEST(encode_known_vector_3) {
    /* "M" → "TQ==" */
    string encoded = base64_encode(string("M"), false);
    CHECK(encoded == "TQ==");
}

TEST(url_safe_chars) {
    string encoded = base64_encode(string("\xFF\xFE"), true);
    /* URL-safe should use - and _ instead of + and / and . instead of = */
    CHECK(encoded.find('-') != string::npos || encoded.find('_') != string::npos || encoded.find('.') != string::npos);
}

TEST(decode_invalid_throws) {
    bool threw = false;
    try {
        base64_decode("!!!invalid!!!", false);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(decode_padded) {
    /* "TQ==" → "M" */
    string decoded = base64_decode("TQ==", false);
    CHECK(decoded == "M");
}

TEST(large_encode_decode) {
    /* 10KB of random-ish data */
    string original;
    original.reserve(10240);
    for (int i = 0; i < 10240; i++) {
        original.push_back((char)((i * 17 + 31) & 0xFF));
    }
    string encoded = base64_encode(original, false);
    string decoded = base64_decode(encoded, false);
    CHECK(original == decoded);
    CHECK(encoded.size() >= original.size()); /* base64 is larger */
}

TEST(decode_with_linebreaks) {
    string encoded = "TQ==";
    string encoded_with_nl = "TQ==\n";
    string decoded1 = base64_decode(encoded, false);
    string decoded2 = base64_decode(encoded_with_nl, true);
    CHECK(decoded1 == decoded2);
}

TEST_SUITE_END() {
    RUN_TEST(encode_empty);
    RUN_TEST(encode_decode_roundtrip);
    RUN_TEST(encode_decode_binary);
    RUN_TEST(encode_known_vector);
    RUN_TEST(encode_known_vector_2);
    RUN_TEST(encode_known_vector_3);
    RUN_TEST(url_safe_chars);
    RUN_TEST(decode_invalid_throws);
    RUN_TEST(decode_padded);
    RUN_TEST(large_encode_decode);
    RUN_TEST(decode_with_linebreaks);
}

TEST_MAIN()
