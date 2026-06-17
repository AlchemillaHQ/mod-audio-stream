#include "test.h"
#include "stream_utils.h"
#include <cstring>
#include <string>

static char wsUri[MAX_WS_URI];

TEST(valid_ws) {
    CHECK(validate_ws_uri("ws://example.com/audio", wsUri) == 1);
    CHECK(strcmp(wsUri, "ws://example.com/audio") == 0);
}

TEST(valid_wss) {
    CHECK(validate_ws_uri("wss://secure.example.com:8443/stream", wsUri) == 1);
    CHECK(strcmp(wsUri, "wss://secure.example.com:8443/stream") == 0);
}

TEST(valid_ws_no_path) {
    CHECK(validate_ws_uri("ws://10.0.0.1:8080", wsUri) == 1);
}

TEST(valid_ws_root_path) {
    CHECK(validate_ws_uri("ws://example.com/", wsUri) == 1);
}

TEST(invalid_no_scheme) {
    CHECK(validate_ws_uri("example.com/audio", wsUri) == 0);
}

TEST(invalid_http) {
    CHECK(validate_ws_uri("http://example.com", wsUri) == 0);
}

TEST(invalid_empty) {
    CHECK(validate_ws_uri("", wsUri) == 0);
}

TEST(invalid_no_host) {
    CHECK(validate_ws_uri("ws:///path", wsUri) == 0);
}

TEST(invalid_bad_port) {
    CHECK(validate_ws_uri("ws://example.com:abc/path", wsUri) == 0);
}

TEST(invalid_special_char_in_host) {
    CHECK(validate_ws_uri("ws://example!.com/path", wsUri) == 0);
}

TEST(valid_ipv4) {
    CHECK(validate_ws_uri("ws://10.0.0.1:9999/stream", wsUri) == 1);
}

TEST(valid_underscore_host) {
    CHECK(validate_ws_uri("ws://test_server:8080/path", wsUri) == 1);
}

TEST(invalid_wss_bad_port) {
    CHECK(validate_ws_uri("wss://host:abc/path", wsUri) == 0);
}

TEST(valid_long_uri) {
    std::string longUri = "ws://example.com/";
    longUri.append(4000, 'x');
    int result = validate_ws_uri(longUri.c_str(), wsUri);
    CHECK(result == 1);
    CHECK(strncmp(wsUri, "ws://example.com/", 16) == 0);
}

TEST_SUITE_END() {
    RUN_TEST(valid_ws);
    RUN_TEST(valid_wss);
    RUN_TEST(valid_ws_no_path);
    RUN_TEST(valid_ws_root_path);
    RUN_TEST(invalid_no_scheme);
    RUN_TEST(invalid_http);
    RUN_TEST(invalid_empty);
    RUN_TEST(invalid_no_host);
    RUN_TEST(invalid_bad_port);
    RUN_TEST(invalid_special_char_in_host);
    RUN_TEST(valid_ipv4);
    RUN_TEST(valid_underscore_host);
    RUN_TEST(invalid_wss_bad_port);
    RUN_TEST(valid_long_uri);
}

TEST_MAIN()
