/*
 * ws_client.h - Lightweight WebSocket client for FreeSWITCH mod_audio_stream
 *
 * Based on easywsclient framing (MIT, https://github.com/dhbaird/easywsclient)
 * with OpenSSL TLS, proper random keys, custom headers, and thread-safe operation.
 */

#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <functional>
#include <string>
#include <map>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <cstdint>

struct TLSOptions {
    std::string caFile;
    std::string keyFile;
    std::string certFile;
    bool        disableHostnameValidation = false;
};

class WebSocketClient {
public:
    using OpenCallback    = std::function<void()>;
    using CloseCallback   = std::function<void(int code, const std::string& reason)>;
    using ErrorCallback   = std::function<void(int code, const std::string& msg)>;
    using MessageCallback = std::function<void(const std::string&)>;
    using BinaryCallback  = std::function<void(const void* data, size_t len)>;

    WebSocketClient();
    ~WebSocketClient();

    // ---- configuration (call BEFORE connect) ----
    void setUrl(const std::string& url);
    void setTLSOptions(const TLSOptions& opts);
    void setPingInterval(int seconds);   // 0 = disable
    void setHeaders(const std::map<std::string, std::string>& headers);
    void enableCompression(bool) {}      // no-op: per-message deflate not supported

    // ---- lifecycle ----
    void connect();                      // async: starts WS thread
    void disconnect();                   // graceful close + join WS thread
    bool isConnected() const;

    // ---- send (thread-safe, may be called from any thread) ----
    void sendBinary(const uint8_t* data, size_t len);
    void sendMessage(const char* text, size_t len);

    // ---- callbacks (set BEFORE connect) ----
    void setOpenCallback(OpenCallback cb)       { onOpen_    = std::move(cb); }
    void setCloseCallback(CloseCallback cb)     { onClose_   = std::move(cb); }
    void setErrorCallback(ErrorCallback cb)     { onError_   = std::move(cb); }
    void setMessageCallback(MessageCallback cb) { onMessage_ = std::move(cb); }
    void setBinaryCallback(BinaryCallback cb)   { onBinary_  = std::move(cb); }

#ifdef WS_CLIENT_TEST
    // exposed for testing
    std::string test_getHost() const { return host_; }
    int         test_getPort() const { return port_; }
    std::string test_getPath() const { return path_; }
    bool        test_isTls()    const { return useTls_; }
    static std::string test_generateKey() { return generateKey(); }
#endif

private:
    void threadFunc();
    bool doHandshake();
    void processFrame();
    void drainSendQueue();
    bool sendFrame(int opcode, const uint8_t* data, size_t len);
    void fail(const std::string& msg, int code = 6);
    static std::string generateKey();

    // ---- configuration ----
    std::string                          url_;
    std::string                          host_;
    std::string                          path_;
    int                                  port_;
    bool                                 useTls_;
    TLSOptions                           tlsOpts_;
    int                                  pingInterval_;
    std::map<std::string, std::string>   extraHeaders_;

    // ---- callbacks ----
    OpenCallback    onOpen_;
    CloseCallback   onClose_;
    ErrorCallback   onError_;
    MessageCallback onMessage_;
    BinaryCallback  onBinary_;

    // ---- socket / TLS ----
    int          sockfd_;
    void*        ssl_;      // SSL* (opaque to avoid openssl header in public interface)
    void*        sslCtx_;   // SSL_CTX*

    // ---- state ----
    enum State { DISCONNECTED, CONNECTING, OPEN, CLOSING, CLOSED };
    std::atomic<State> state_;
    std::atomic<bool>  running_;
    std::thread        thread_;
    std::atomic<bool>  threadStarted_;

    // ---- send queue (producer: media thread, consumer: WS thread) ----
    struct SendItem {
        std::vector<uint8_t> data;
        bool isBinary;
    };
    std::mutex                      sendMutex_;
    std::condition_variable         sendCv_;
    std::deque<SendItem>            sendQueue_;
    static constexpr size_t         MAX_SEND_QUEUE = 64;

    // ---- receive buffer ----
    std::vector<uint8_t> rxbuf_;

    // ---- ping timer ----
    std::chrono::steady_clock::time_point lastActivity_;
};

#endif
