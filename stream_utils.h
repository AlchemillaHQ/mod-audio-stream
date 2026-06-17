#ifndef STREAM_UTILS_H
#define STREAM_UTILS_H

#define MAX_WS_URI 4096

#ifdef __cplusplus
extern "C" {
#endif

int validate_ws_uri(const char* url, char* wsUri);
int is_valid_utf8(const char* str);

#ifdef __cplusplus
}
#endif

#endif
