#include "test.h"
#define WS_CLIENT_TEST
#include "ws_client.h"
#include <string>
#include <cstring>

using std::string;

/* ---- URL parsing ---- */

TEST(ws_plain_default_port) {
    WebSocketClient c;
    c.setUrl("ws://example.com/audio");
    CHECK(c.test_isTls() == false);
    CHECK(c.test_getHost() == "example.com");
    CHECK(c.test_getPort() == 80);
    CHECK(c.test_getPath() == "/audio");
}

TEST(ws_explicit_port) {
    WebSocketClient c;
    c.setUrl("ws://10.0.0.1:8080/path/to/stream");
    CHECK(c.test_isTls() == false);
    CHECK(c.test_getHost() == "10.0.0.1");
    CHECK(c.test_getPort() == 8080);
    CHECK(c.test_getPath() == "/path/to/stream");
}

TEST(ws_no_path) {
    WebSocketClient c;
    c.setUrl("ws://foo.bar:9090");
    CHECK(c.test_getHost() == "foo.bar");
    CHECK(c.test_getPort() == 9090);
    CHECK(c.test_getPath() == "/"); // empty path → "/"
}

TEST(wss_default_port) {
    WebSocketClient c;
    c.setUrl("wss://secure.example.com/stream");
    CHECK(c.test_isTls() == true);
    CHECK(c.test_getHost() == "secure.example.com");
    CHECK(c.test_getPort() == 443);
    CHECK(c.test_getPath() == "/stream");
}

TEST(wss_explicit_port) {
    WebSocketClient c;
    c.setUrl("wss://secure.example.com:9443/stream");
    CHECK(c.test_isTls() == true);
    CHECK(c.test_getPort() == 9443);
}

TEST(ws_root_path) {
    WebSocketClient c;
    c.setUrl("ws://example.com/");
    CHECK(c.test_getPath() == "/");
}

TEST(ws_host_only) {
    WebSocketClient c;
    c.setUrl("ws://example.com");
    CHECK(c.test_getHost() == "example.com");
    CHECK(c.test_getPath() == "/");
}

/* ---- key generation ---- */

TEST(key_length) {
    string key = WebSocketClient::test_generateKey();
    // 16 random bytes → 24 base64 chars
    CHECK(key.size() == 24);
}

TEST(key_is_base64) {
    string key = WebSocketClient::test_generateKey();
    for (size_t i = 0; i < key.size(); i++) {
        char c = key[i];
        bool valid = (c >= 'A' && c <= 'Z') ||
                     (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') ||
                     c == '+' || c == '/' || c == '=';
        CHECK(valid);
    }
}

TEST(key_is_random) {
    string k1 = WebSocketClient::test_generateKey();
    string k2 = WebSocketClient::test_generateKey();
    CHECK(k1 != k2);
}

/* ---- lifecycle ---- */

TEST(initial_state) {
    WebSocketClient c;
    CHECK(c.isConnected() == false);
}

TEST(disconnect_when_not_connected) {
    WebSocketClient c;
    c.disconnect(); // should not crash
    CHECK(c.isConnected() == false);
}

/* ---- TLSOptions defaults ---- */

TEST(tls_options_defaults) {
    TLSOptions opts;
    CHECK(opts.caFile.empty());
    CHECK(opts.keyFile.empty());
    CHECK(opts.certFile.empty());
    CHECK(opts.disableHostnameValidation == false);
}

TEST_SUITE_END() {
    RUN_TEST(ws_plain_default_port);
    RUN_TEST(ws_explicit_port);
    RUN_TEST(ws_no_path);
    RUN_TEST(wss_default_port);
    RUN_TEST(wss_explicit_port);
    RUN_TEST(ws_root_path);
    RUN_TEST(ws_host_only);
    RUN_TEST(key_length);
    RUN_TEST(key_is_base64);
    RUN_TEST(key_is_random);
    RUN_TEST(initial_state);
    RUN_TEST(disconnect_when_not_connected);
    RUN_TEST(tls_options_defaults);
}

TEST_MAIN()
