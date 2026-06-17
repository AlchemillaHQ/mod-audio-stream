#ifndef MOD_AUDIO_STREAM_H
#define MOD_AUDIO_STREAM_H

#include <switch.h>
#include <speex/speex_resampler.h>

#define MOD_AUDIO_STREAM_VERSION "0.0.1"
#define MY_BUG_NAME "audio_stream"
#define MAX_SESSION_ID (256)
#define MAX_WS_URI (4096)
#define MAX_METADATA_LEN (8192)

#define EVENT_CONNECT           "mod_audio_stream::connect"
#define EVENT_DISCONNECT        "mod_audio_stream::disconnect"
#define EVENT_ERROR             "mod_audio_stream::error"
#define EVENT_JSON              "mod_audio_stream::json"
#define EVENT_PLAY              "mod_audio_stream::play"
#define EVENT_PLAYBACK          "mod_audio_stream::playback"

#define PLAYBACK_BUFFER_SECONDS 30
#define PLAYBACK_FRAME_MS       20

typedef void (*responseHandler_t)(switch_core_session_t* session, const char* eventName, const char* json);

struct private_data {
    switch_mutex_t *mutex;
    char sessionId[MAX_SESSION_ID];
    SpeexResamplerState *resampler;
    responseHandler_t responseHandler;
    void *pAudioStreamer;
    char ws_uri[MAX_WS_URI];
    int sampling;
    int native_sampling;
    int channels;
    unsigned int audio_paused:1;
    unsigned int close_requested:1;
    unsigned int cleanup_started:1;
    unsigned int playback_enabled:1;
    unsigned int playback_paused:1;
    unsigned int playback_in_response:1;
    char initialMetadata[8192];
    switch_buffer_t *sbuffer;
    switch_buffer_t *playback_buffer;
    SpeexResamplerState *playback_resampler;
    int playback_sample_rate;
    int playback_seq;
    int playback_chunks_remaining;
    int rtp_packets;
};

typedef struct private_data private_t;

enum notifyEvent_t {
    CONNECT_SUCCESS,
    CONNECT_ERROR,
    CONNECTION_DROPPED,
    MESSAGE
};

#endif //MOD_AUDIO_STREAM_H
