#include "stream_utils.h"
#include <cstring>
#include <cctype>

extern "C" {

int validate_ws_uri(const char* url, char* wsUri) {
    const char* hostStart = nullptr;
    const char* hostEnd = nullptr;
    const char* portStart = nullptr;

    if (strncmp(url, "ws://", 5) == 0) {
        hostStart = url + 5;
    } else if (strncmp(url, "wss://", 6) == 0) {
        hostStart = url + 6;
    } else {
        return 0;
    }

    hostEnd = hostStart;
    while (*hostEnd && *hostEnd != ':' && *hostEnd != '/') {
        if (!std::isalnum((unsigned char)*hostEnd) && *hostEnd != '-' && *hostEnd != '.' && *hostEnd != '_') {
            return 0;
        }
        ++hostEnd;
    }

    if (hostStart == hostEnd) {
        return 0;
    }

    if (*hostEnd == ':') {
        portStart = hostEnd + 1;
        while (*portStart && *portStart != '/') {
            if (!std::isdigit((unsigned char)*portStart)) {
                return 0;
            }
            ++portStart;
        }
    }

    std::strncpy(wsUri, url, MAX_WS_URI - 1);
    wsUri[MAX_WS_URI - 1] = '\0';
    return 1;
}

int is_valid_utf8(const char* str) {
    while (*str) {
        if ((*str & 0x80) == 0x00) {
            str++;
        } else if ((*str & 0xE0) == 0xC0) {
            if ((str[1] & 0xC0) != 0x80) return 0;
            // reject overlong: codepoint must be >= U+0080
            unsigned cp = ((unsigned)(str[0] & 0x1F) << 6) | (str[1] & 0x3F);
            if (cp < 0x80) return 0;
            str += 2;
        } else if ((*str & 0xF0) == 0xE0) {
            if ((str[1] & 0xC0) != 0x80 || (str[2] & 0xC0) != 0x80) return 0;
            unsigned cp = ((unsigned)(str[0] & 0x0F) << 12)
                        | ((unsigned)(str[1] & 0x3F) << 6)
                        | (str[2] & 0x3F);
            // reject overlong (cp < 0x800) and surrogates (U+D800-U+DFFF)
            if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
            str += 3;
        } else if ((*str & 0xF8) == 0xF0) {
            if ((str[1] & 0xC0) != 0x80 || (str[2] & 0xC0) != 0x80
                || (str[3] & 0xC0) != 0x80) return 0;
            unsigned cp = ((unsigned)(str[0] & 0x07) << 18)
                        | ((unsigned)(str[1] & 0x3F) << 12)
                        | ((unsigned)(str[2] & 0x3F) << 6)
                        | (str[3] & 0x3F);
            // reject overlong (cp < 0x10000) and above U+10FFFF
            if (cp < 0x10000 || cp > 0x10FFFF) return 0;
            str += 4;
        } else {
            return 0;
        }
    }
    return 1;
}

} // extern "C"
