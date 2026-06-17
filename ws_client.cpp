/*
 * ws_client.cpp - Lightweight WebSocket client implementation
 *
 * WebSocket framing based on easywsclient (MIT license, dhbaird/easywsclient)
 * https://github.com/dhbaird/easywsclient
 */

#include "ws_client.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <algorithm>
#include <random>
#include <chrono>

#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>

#ifdef USE_TLS
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#endif

// ---- internal helpers ----

#ifdef USE_TLS
static std::string sha1_and_base64(const std::string& input) {
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), digest);

    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(29);
    for (int i = 0; i < 18; i += 3) {
        unsigned n  = (digest[i]   << 16) |
                      (digest[i+1] << 8)  |
                      (digest[i+2]);
        out.push_back(b64[(n >> 18) & 0x3F]);
        out.push_back(b64[(n >> 12) & 0x3F]);
        out.push_back(b64[(n >> 6)  & 0x3F]);
        out.push_back(b64[ n        & 0x3F]);
    }
    // last 2 bytes -> 3 chars + padding
    unsigned n = (digest[18] << 16) | (digest[19] << 8);
    out.push_back(b64[(n >> 18) & 0x3F]);
    out.push_back(b64[(n >> 12) & 0x3F]);
    out.push_back(b64[(n >> 6)  & 0x3F]);
    out.push_back('=');
    return out;
}
#endif

static std::string randomBase64(int bytes) {
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<int> dist(0, 255);
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::vector<unsigned char> buf(bytes);
    for (int i = 0; i < bytes; i++) buf[i] = static_cast<unsigned char>(dist(rng));

    std::string out;
    out.reserve(((bytes + 2) / 3) * 4);
    for (size_t i = 0; i < buf.size(); i += 3) {
        unsigned n = (buf[i] << 16);
        if (i + 1 < buf.size()) n |= (buf[i+1] << 8);
        if (i + 2 < buf.size()) n |= buf[i+2];
        out.push_back(b64[(n >> 18) & 0x3F]);
        out.push_back(b64[(n >> 12) & 0x3F]);
        if (i + 1 < buf.size()) {
            out.push_back(b64[(n >> 6) & 0x3F]);
            if (i + 2 < buf.size())
                out.push_back(b64[n & 0x3F]);
            else
                out.push_back('=');
        } else {
            out.push_back('=');
            out.push_back('=');
        }
    }
    return out;
}

static bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

// ---- WebSocketClient ----

WebSocketClient::WebSocketClient()
    : port_(0), useTls_(false), pingInterval_(0),
      sockfd_(-1), ssl_(nullptr), sslCtx_(nullptr),
      state_(DISCONNECTED), running_(false), threadStarted_(false) {}

WebSocketClient::~WebSocketClient() {
    disconnect();
}

void WebSocketClient::setUrl(const std::string& url) {
    url_ = url;

    // parse ws://host:port/path or wss://host:port/path
    if (url.compare(0, 6, "wss://") == 0) {
        useTls_ = true;
        port_ = 443;
    } else if (url.compare(0, 5, "ws://") == 0) {
        useTls_ = false;
        port_ = 80;
    } else {
        return;
    }

    const char* start = url.c_str() + (useTls_ ? 6 : 5);
    const char* slash = strchr(start, '/');
    const char* colon = strchr(start, ':');

    if (colon && (!slash || colon < slash)) {
        // has explicit port
        host_.assign(start, colon - start);
        port_ = atoi(colon + 1);
        if (slash) path_.assign(slash, strlen(slash));
        else path_ = "";
    } else {
        if (slash) {
            host_.assign(start, slash - start);
            path_.assign(slash, strlen(slash));
        } else {
            host_ = start;
            path_ = "";
        }
    }
    if (path_.empty()) path_ = "/";
}

void WebSocketClient::setTLSOptions(const TLSOptions& opts) {
    tlsOpts_ = opts;
}

void WebSocketClient::setPingInterval(int seconds) {
    pingInterval_ = seconds;
}

void WebSocketClient::setHeaders(const std::map<std::string, std::string>& headers) {
    extraHeaders_ = headers;
}

// ---- lifecycle ----

void WebSocketClient::connect() {
    State expected = DISCONNECTED;
    if (!state_.compare_exchange_strong(expected, CONNECTING))
        return;

    running_ = true;
    thread_ = std::thread(&WebSocketClient::threadFunc, this);
    threadStarted_ = true;
}

void WebSocketClient::disconnect() {
    State s = state_.load();
    if (s != DISCONNECTED && s != CLOSED) {
        state_ = CLOSING;
        running_ = false;

        {
            std::lock_guard<std::mutex> lk(sendMutex_);
            sendCv_.notify_all();
        }
    }

    if (threadStarted_ && thread_.joinable()) {
        if (std::this_thread::get_id() == thread_.get_id()) {
            thread_.detach();
        } else {
            thread_.join();
        }
        threadStarted_ = false;
    }
}

bool WebSocketClient::isConnected() const {
    return state_.load() == OPEN;
}

// ---- send (thread-safe) ----

void WebSocketClient::sendBinary(const uint8_t* data, size_t len) {
    if (!data || len == 0) return;
    if (state_.load() != OPEN) return;

    SendItem item;
    item.data.assign(data, data + len);
    item.isBinary = true;

    {
        std::lock_guard<std::mutex> lk(sendMutex_);
        if (sendQueue_.size() >= MAX_SEND_QUEUE) {
            static int overflow_warn = 0;
            if (overflow_warn++ < 5 || overflow_warn % 1000 == 0) {
                fprintf(stderr, "[ws_client] send queue overflow (%zu/%zu) - dropping frame\n",
                        sendQueue_.size(), MAX_SEND_QUEUE);
            }
            return;
        }
        sendQueue_.push_back(std::move(item));
    }
    sendCv_.notify_one();
}

void WebSocketClient::sendMessage(const char* text, size_t len) {
    if (!text || len == 0) return;

    SendItem item;
    item.data.assign(text, text + len);
    item.isBinary = false;

    {
        std::lock_guard<std::mutex> lk(sendMutex_);
        if (sendQueue_.size() >= MAX_SEND_QUEUE) {
            static int overflow_warn = 0;
            if (overflow_warn++ < 5 || overflow_warn % 1000 == 0) {
                fprintf(stderr, "[ws_client] send queue overflow (%zu/%zu) - dropping text\n",
                        sendQueue_.size(), MAX_SEND_QUEUE);
            }
            return;
        }
        sendQueue_.push_back(std::move(item));
    }
    sendCv_.notify_one();
}

// ---- random key generation ----

std::string WebSocketClient::generateKey() {
    return randomBase64(16);
}

// ---- error reporting ----

void WebSocketClient::fail(const std::string& msg, int code) {
    if (onError_) onError_(code, msg);
}

// ---- the worker thread ----

void WebSocketClient::threadFunc() {
    // --- DNS resolve ---
    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%d", port_);

    if (getaddrinfo(host_.c_str(), portStr, &hints, &res) != 0 || !res) {
        state_ = DISCONNECTED;
        fail("DNS lookup failed for " + host_, 6);
        if (onClose_) onClose_(1006, "DNS lookup failed");
        return;
    }

    // --- TCP connect ---
    sockfd_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd_ < 0) {
        state_ = DISCONNECTED;
        freeaddrinfo(res);
        fail("Socket creation failed", 6);
        if (onClose_) onClose_(1006, "Socket creation failed");
        return;
    }

    // set send buffer size for audio
    int sndbuf = 256 * 1024;
    setsockopt(sockfd_, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    int ret = ::connect(sockfd_, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    if (ret < 0 && errno != EINPROGRESS) {
        ::close(sockfd_); sockfd_ = -1;
        state_ = DISCONNECTED;
        fail("TCP connect failed", 6);
        if (onClose_) onClose_(1006, "TCP connect failed");
        return;
    }

    // wait for connect to complete
    if (ret < 0) {
        struct pollfd pfd;
        pfd.fd = sockfd_;
        pfd.events = POLLOUT;
        if (poll(&pfd, 1, 10000) <= 0) {
            ::close(sockfd_); sockfd_ = -1;
            state_ = DISCONNECTED;
            fail("TCP connect timeout", 6);
            if (onClose_) onClose_(1006, "TCP connect timeout");
            return;
        }
        // check socket error
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(sockfd_, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err != 0) {
            ::close(sockfd_); sockfd_ = -1;
            state_ = DISCONNECTED;
            fail("TCP connect error", 6);
            if (onClose_) onClose_(1006, "TCP connect error");
            return;
        }
    }

    setNonBlocking(sockfd_);
    int flag = 1;
    setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

#ifdef USE_TLS
    // --- TLS handshake (wss:// only) ---
    if (useTls_) {
        sslCtx_ = SSL_CTX_new(TLS_client_method());
        if (!sslCtx_) {
            ::close(sockfd_); sockfd_ = -1;
            state_ = DISCONNECTED;
            fail("SSL_CTX_new failed", 7);
            if (onClose_) onClose_(1006, "TLS context creation failed");
            return;
        }

        SSL_CTX_set_options((SSL_CTX*)sslCtx_, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);

        // certificate verification
        if (tlsOpts_.caFile == "NONE" || tlsOpts_.disableHostnameValidation) {
            SSL_CTX_set_verify((SSL_CTX*)sslCtx_, SSL_VERIFY_NONE, nullptr);
        } else if (tlsOpts_.caFile == "SYSTEM" || tlsOpts_.caFile.empty()) {
            SSL_CTX_set_default_verify_paths((SSL_CTX*)sslCtx_);
            SSL_CTX_set_verify((SSL_CTX*)sslCtx_, SSL_VERIFY_PEER, nullptr);
        } else {
            SSL_CTX_load_verify_locations((SSL_CTX*)sslCtx_, tlsOpts_.caFile.c_str(), nullptr);
            SSL_CTX_set_verify((SSL_CTX*)sslCtx_, SSL_VERIFY_PEER, nullptr);
        }

        // client certificate
        if (!tlsOpts_.certFile.empty() && !tlsOpts_.keyFile.empty()) {
            SSL_CTX_use_certificate_file((SSL_CTX*)sslCtx_, tlsOpts_.certFile.c_str(), SSL_FILETYPE_PEM);
            SSL_CTX_use_PrivateKey_file((SSL_CTX*)sslCtx_, tlsOpts_.keyFile.c_str(), SSL_FILETYPE_PEM);
            SSL_CTX_check_private_key((SSL_CTX*)sslCtx_);
        }

        ssl_ = SSL_new((SSL_CTX*)sslCtx_);
        SSL_set_fd((SSL*)ssl_, sockfd_);
        SSL_set_tlsext_host_name((SSL*)ssl_, host_.c_str());

        // hostname verification
        if (!tlsOpts_.disableHostnameValidation) {
            X509_VERIFY_PARAM* param = SSL_get0_param((SSL*)ssl_);
            X509_VERIFY_PARAM_set1_host(param, host_.c_str(), 0);
        }

        if (SSL_connect((SSL*)ssl_) != 1) {
            unsigned long err = ERR_get_error();
            char errBuf[256];
            ERR_error_string_n(err, errBuf, sizeof(errBuf));
            SSL_free((SSL*)ssl_); ssl_ = nullptr;
            SSL_CTX_free((SSL_CTX*)sslCtx_); sslCtx_ = nullptr;
            ::close(sockfd_); sockfd_ = -1;
            state_ = DISCONNECTED;
            fail(std::string("TLS handshake failed: ") + errBuf, 8);
            if (onClose_) onClose_(1006, "TLS handshake failed");
            return;
        }
    }
#endif

    // --- WebSocket HTTP upgrade ---
    if (!doHandshake()) {
        state_ = DISCONNECTED;
#ifdef USE_TLS
        if (ssl_) { SSL_free((SSL*)ssl_); ssl_ = nullptr; }
        if (sslCtx_) { SSL_CTX_free((SSL_CTX*)sslCtx_); sslCtx_ = nullptr; }
#endif
        ::close(sockfd_); sockfd_ = -1;
        // doHandshake already calls fail()
        if (onClose_) onClose_(1002, "WebSocket handshake failed");
        return;
    }

    state_ = OPEN;
    lastActivity_ = std::chrono::steady_clock::now();

    if (onOpen_) onOpen_();

    // --- main poll loop ---
    rxbuf_.reserve(65536);

    while (running_) {
        bool hasQueue;
        {
            std::lock_guard<std::mutex> lk(sendMutex_);
            hasQueue = !sendQueue_.empty();
        }
        struct pollfd pfd;
        pfd.fd = sockfd_;
        pfd.events = POLLIN;
        if (hasQueue) pfd.events |= POLLOUT;
        int timeout = pingInterval_ > 0 ? 1000 : 5000; // 1s if pinging, 5s otherwise

        int n = poll(&pfd, 1, timeout);
        auto now = std::chrono::steady_clock::now();

        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // --- read ---
        if (pfd.revents & POLLIN) {
            uint8_t buf[8192];
            int nread;
#ifdef USE_TLS
            if (ssl_)
                nread = SSL_read((SSL*)ssl_, buf, sizeof(buf));
            else
#endif
                nread = (int)recv(sockfd_, buf, sizeof(buf), 0);

            if (nread > 0) {
                rxbuf_.insert(rxbuf_.end(), buf, buf + nread);
                lastActivity_ = now;
                processFrame();
            } else if (nread == 0) {
                break; // connection closed
            } else {
#ifdef USE_TLS
                if (ssl_) {
                    int err = SSL_get_error((SSL*)ssl_, nread);
                    if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE)
                        break; // real error
                } else
#endif
                {
                    if (errno != EAGAIN && errno != EWOULDBLOCK)
                        break;
                }
            }
        }

        // --- write (drain send queue) ---
        if (pfd.revents & POLLOUT) {
            drainSendQueue();
        }

        // --- ping ---
        if (pingInterval_ > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - lastActivity_).count();
            if (elapsed >= pingInterval_) {
                sendFrame(0x9, nullptr, 0);
                lastActivity_ = now;
            }
        }
    }

    // --- cleanup ---
    if (sockfd_ >= 0) {
        sendFrame(0x8, nullptr, 0);
#ifdef USE_TLS
        if (ssl_) {
            SSL_shutdown((SSL*)ssl_);
            SSL_free((SSL*)ssl_); ssl_ = nullptr;
        }
        if (sslCtx_) {
            SSL_CTX_free((SSL_CTX*)sslCtx_); sslCtx_ = nullptr;
        }
#endif
        ::close(sockfd_); sockfd_ = -1;
    }

    State oldState = state_.exchange(CLOSED);
    if (onClose_) {
        int code = 1000;
        std::string reason = "Connection closed";
        if (oldState != CLOSING) {
            code = 1006;
            reason = "Connection closed unexpectedly";
        }
        onClose_(code, reason);
    }
}

// ---- WebSocket HTTP upgrade ----

bool WebSocketClient::doHandshake() {
    std::string wsKey = generateKey();
    std::string expectedAccept;

#ifdef USE_TLS
    expectedAccept = sha1_and_base64(wsKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
#else
    // basic base64-only fallback (you should compile with USE_TLS)
    expectedAccept = "unused";
#endif

    std::string req;
    req += "GET " + path_ + " HTTP/1.1\r\n";
    req += "Host: " + host_;
    if ((useTls_ && port_ != 443) || (!useTls_ && port_ != 80)) {
        req += ":" + std::to_string(port_);
    }
    req += "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + wsKey + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";

    for (const auto& h : extraHeaders_) {
        req += h.first + ": " + h.second + "\r\n";
    }

    req += "\r\n";

    // send the request
    int sent = 0;
    while (sent < (int)req.size()) {
        int n;
#ifdef USE_TLS
        if (ssl_)
            n = SSL_write((SSL*)ssl_, req.data() + sent, (int)req.size() - sent);
        else
#endif
            n = (int)::send(sockfd_, req.data() + sent, req.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            fail("Failed to send HTTP upgrade request", 6);
            return false;
        }
        sent += n;
    }

    // temporarily set blocking for handshake response read
    int oldFlags = fcntl(sockfd_, F_GETFL, 0);
    fcntl(sockfd_, F_SETFL, oldFlags & ~O_NONBLOCK);

    // read response line
    std::string response;
    char c;
    while (response.size() < 1024) {
        int n;
#ifdef USE_TLS
        if (ssl_)
            n = SSL_read((SSL*)ssl_, &c, 1);
        else
#endif
            n = (int)recv(sockfd_, &c, 1, 0);
        if (n <= 0) {
            fail("Connection closed during WebSocket handshake", 6);
            return false;
        }
        response += c;
        if (response.size() >= 2 && response[response.size()-2] == '\r' &&
            response[response.size()-1] == '\n') break;
    }

    int status = 0;
    if (sscanf(response.c_str(), "HTTP/1.1 %d", &status) != 1 || status != 101) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Bad HTTP status: %d", status);
        fail(msg, 6);
        return false;
    }

    // read headers until blank line
    std::string headerLine;
    std::string serverAccept;
    for (;;) {
        headerLine.clear();
        while (headerLine.size() < 4096) {
            int n;
#ifdef USE_TLS
            if (ssl_)
                n = SSL_read((SSL*)ssl_, &c, 1);
            else
#endif
                n = (int)recv(sockfd_, &c, 1, 0);
            if (n <= 0) {
                fail("Connection closed during WebSocket handshake headers", 6);
                return false;
            }
            headerLine += c;
            if (headerLine.size() >= 2 && headerLine[headerLine.size()-2] == '\r' &&
                headerLine[headerLine.size()-1] == '\n') break;
        }
        if (headerLine == "\r\n") break; // blank line = end of headers

        // parse "Sec-WebSocket-Accept: ..."
        if (headerLine.size() > 22 &&
            strncasecmp(headerLine.c_str(), "Sec-WebSocket-Accept:", 21) == 0) {
            size_t start = headerLine.find_first_not_of(" \t", 21);
            size_t end   = headerLine.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos && end > start) {
                serverAccept = headerLine.substr(start, end - start + 1);
            }
        }
    }

#ifdef USE_TLS
    // verify accept key
    if (serverAccept != expectedAccept) {
        fail("WebSocket accept key mismatch", 6);
        return false;
    }
#endif

    fcntl(sockfd_, F_SETFL, oldFlags); // restore original flags (re-enable non-blocking)
    return true;
}

// ---- WebSocket framing ----

bool WebSocketClient::sendFrame(int opcode, const uint8_t* data, size_t len) {
    if (sockfd_ < 0) return false;

    static thread_local std::mt19937 rng(std::random_device{}());
    uint8_t mask[4];
    for (int i = 0; i < 4; i++) mask[i] = (uint8_t)(rng() & 0xFF);

    std::vector<uint8_t> frame;
    frame.reserve(14 + len);

    frame.push_back(0x80 | opcode);

    if (len < 126) {
        frame.push_back(0x80 | (uint8_t)len);
    } else if (len < 65536) {
        frame.push_back(0x80 | 126);
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    } else {
        frame.push_back(0x80 | 127);
        for (int i = 7; i >= 0; i--)
            frame.push_back((len >> (i * 8)) & 0xFF);
    }

    frame.push_back(mask[0]);
    frame.push_back(mask[1]);
    frame.push_back(mask[2]);
    frame.push_back(mask[3]);

    size_t headerSize = frame.size();
    frame.resize(headerSize + len);
    for (size_t i = 0; i < len; i++) {
        frame[headerSize + i] = data[i] ^ mask[i & 0x3];
    }

    size_t offset = 0;
    while (offset < frame.size()) {
        int n;
#ifdef USE_TLS
        if (ssl_)
            n = SSL_write((SSL*)ssl_, frame.data() + offset, (int)(frame.size() - offset));
        else
#endif
            n = (int)::send(sockfd_, frame.data() + offset, frame.size() - offset, MSG_NOSIGNAL);
        if (n > 0) {
            offset += n;
        } else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            break;
        } else {
            break;
        }
    }
    return (offset == frame.size());
}

void WebSocketClient::processFrame() {
    while (!rxbuf_.empty()) {
        if (rxbuf_.size() < 2) return;

        uint8_t* data = rxbuf_.data();
        bool fin    = (data[0] & 0x80) != 0;
        int  opcode = data[0] & 0x0F;
        bool masked = (data[1] & 0x80) != 0;
        uint64_t N  = data[1] & 0x7F;
        size_t headerSize = 2;

        if (N == 126) { headerSize += 2; if (rxbuf_.size() < headerSize) return; N = ((uint64_t)data[2] << 8) | data[3]; }
        else if (N == 127) { headerSize += 8; if (rxbuf_.size() < headerSize) return;
            N = 0; for (int i = 0; i < 8; i++) N = (N << 8) | data[2+i]; }

        uint8_t maskKey[4] = {0,0,0,0};
        if (masked) {
            headerSize += 4;
            if (rxbuf_.size() < headerSize) return;
            for (int i = 0; i < 4; i++) maskKey[i] = data[headerSize - 4 + i];
        }

        if (rxbuf_.size() < headerSize + (size_t)N) return;

        // unmask
        uint8_t* payload = data + headerSize;
        if (masked) {
            for (size_t i = 0; i < (size_t)N; i++) payload[i] ^= maskKey[i & 0x3];
        }

        // handle frame
        if (opcode == 0x1 && fin) { // text
            std::string msg(payload, payload + N);
            if (onMessage_) onMessage_(msg);
        } else if (opcode == 0x2 && fin) { // binary
            if (onBinary_) {
                std::vector<uint8_t> copy(payload, payload + (size_t)N);
                onBinary_(copy.data(), copy.size());
            }
        } else if (opcode == 0x8) { // close
            state_ = CLOSING;
            running_ = false;
            return;
        } else if (opcode == 0x9) { // ping → pong
            sendFrame(0xA, payload, (size_t)N);
        } else if (opcode == 0xA) { // pong
            // ignore
        }
        // else: continuation frames, control frames - ignore for simplicity

        rxbuf_.erase(rxbuf_.begin(), rxbuf_.begin() + headerSize + (size_t)N);
    }
}

void WebSocketClient::drainSendQueue() {
    std::lock_guard<std::mutex> lk(sendMutex_);
    size_t retries = 0;
    while (!sendQueue_.empty() && retries < sendQueue_.size()) {
        SendItem item = std::move(sendQueue_.front());
        sendQueue_.pop_front();
        if (item.data.empty()) continue;
        if (!sendFrame(item.isBinary ? 0x2 : 0x1, item.data.data(), item.data.size())) {
            sendQueue_.push_back(std::move(item));
            retries++;
        } else {
            retries = 0;
        }
    }
}
