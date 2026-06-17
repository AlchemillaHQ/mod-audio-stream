# mod-audio-stream

Bidirectional WebSocket audio streaming for FreeSWITCH - forward caller audio to a WebSocket endpoint and receive audio responses back for playback to the caller.

Based on the original [mod_audio_stream](https://github.com/amigniter/mod_audio_stream) by amigniter (MIT license, see `OLD-LICENSE`). Reimplemented with a self-contained OpenSSL WebSocket client, replacing `libwsc`.

## Quick start

Load the module in FreeSWITCH:

```xml
<!-- conf/autoload_configs/modules.conf.xml -->
<load module="mod_audio_stream"/>
```

Then use from a dialplan:

```xml
<extension name="stream_to_ws">
  <condition field="destination_number" expression="^5555$">
    <action application="answer"/>
    <action application="set" data="STREAM_PLAYBACK=true"/>
    <action application="uuid_audio_stream" data="start ws://my-server:8080/audio mono 8k"/>
    <action application="park"/>
  </condition>
</extension>
```

Or from the CLI / event socket:

```
uuid_audio_stream <uuid> start ws://my-server:8080/audio mono 8k
uuid_audio_stream <uuid> pause
uuid_audio_stream <uuid> stop
```

## Features

- Forward streaming: L16 PCM audio from FreeSWITCH channel → WebSocket (resampling, buffer sizing)
- **Bidirectional playback**: Receive audio responses (base64 JSON or raw binary) from WebSocket → inject back into the call
- Audio formats: raw L16, PCMU, PCMA, Opus, WAV, MP3, OGG
- Playback tracking with `chunk_played` / `queue_completed` events
- `break` API to stop current playback immediately
- Full-duplex: forward streaming and playback run independently
- Thread-safe WebSocket client with OpenSSL TLS (ws:// and wss://)
- Test suite: 5 suites, 356 unit checks + 18 integration checks

## Dependencies

```bash
apt-get install cmake g++ make pkg-config libssl-dev libspeexdsp-dev libopus-dev
```

FreeSWITCH 1.10.x development headers must be available. If installed to `/usr/local/freeswitch`:

```bash
export PKG_CONFIG_PATH=/usr/local/freeswitch/lib/pkgconfig
```

## Building

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_LOCAL=ON ..
make
make install
```

`-DENABLE_LOCAL=ON` sets `PKG_CONFIG_PATH` to find FreeSWITCH at `/usr/local/freeswitch`. Omit if FS is on the system pkg-config path.

### Containerized build + test

```bash
cd test/integration
podman compose build
podman compose up -d
sleep 8
podman exec fs_test bash /usr/src/mod_audio_stream/test/integration/run.sh
podman compose down
```

## Channel Variables

| Variable | Description | Default |
|---|---|---|
| `STREAM_PLAYBACK` | Enable bidirectional playback mode | off |
| `STREAM_SAMPLE_RATE` | Default sample rate for raw binary stream | 8000 |
| `STREAM_HEART_BEAT` | Ping interval in seconds | off |
| `STREAM_SUPPRESS_LOG` | Suppress response logging | off |
| `STREAM_BUFFER_SIZE` | Buffer duration in ms (divisible by 20) | 20 |
| `STREAM_EXTRA_HEADERS` | JSON object for custom HTTP headers | none |
| `STREAM_TLS_CA_FILE` | CA cert or bundle (SYSTEM/NONE/path) | SYSTEM |
| `STREAM_TLS_KEY_FILE` | Client key for wss:// | none |
| `STREAM_TLS_CERT_FILE` | Client cert for wss:// | none |
| `STREAM_TLS_DISABLE_HOSTNAME_VALIDATION` | Disable hostname check | false |

## API Commands

```
uuid_audio_stream <uuid> start <ws-url> <mix-type> <sampling-rate> [metadata]
uuid_audio_stream <uuid> stop [text]
uuid_audio_stream <uuid> send_text <text>
uuid_audio_stream <uuid> pause
uuid_audio_stream <uuid> resume
uuid_audio_stream <uuid> break
```

- `start`: Attach media bug and begin streaming. `mix-type`: mono/mixed/stereo. `sampling-rate`: 8k/16k.
- `stop`: Stop streaming and close WebSocket. Optional text sent before close.
- `send_text`: Send text/JSON to WebSocket server.
- `pause`/`resume`: Pause/resume forward audio streaming.
- `break`: Clear playback queue immediately. Forward streaming continues.

## Bidirectional Playback

Enable with `STREAM_PLAYBACK=true`.

### JSON Base64 Audio

```json
{
  "type": "streamAudio",
  "data": {
    "audioDataType": "raw",
    "sampleRate": 8000,
    "audioData": "base64 encoded audio"
  }
}
```

Supported types: `raw` (L16, any 8kHz-multiple sample rate), `pcmu` (PCMU/μ-law, 8kHz), `pcma` (PCMA/A-law, 8kHz), `opus` (raw Opus frames, 48kHz, 20ms), `wav`, `mp3`, `ogg`.

### Raw Binary Stream

Send a metadata message first:
```json
{"type": "rawAudio", "data": {"sampleRate": 8000}}
```
Then stream raw L16 PCM as binary WebSocket frames. Sample rate from metadata overrides `STREAM_SAMPLE_RATE`. Best results: 20ms per frame (e.g., 320 bytes at 8kHz).

### Playback Events

**Event:** `mod_audio_stream::play`
Fired for each incoming JSON `streamAudio` response. Body contains the `data` object (without `audioData`).

**Event:** `mod_audio_stream::playback`
```json
{"event": "chunk_played", "seq": 12, "size": 320, "remaining": 5}
{"event": "queue_completed", "total_chunks": 12}
```

### Break

Call `uuid_audio_stream <uuid> break` to stop current playback. The queue is cleared, pending audio is discarded, and `queue_completed` fires. Playback resumes on the next incoming audio.

## Events

| Event | Description |
|---|---|
| `mod_audio_stream::connect` | WebSocket connected |
| `mod_audio_stream::disconnect` | WebSocket disconnected |
| `mod_audio_stream::error` | Connection error |
| `mod_audio_stream::json` | Non-playback message received |
| `mod_audio_stream::play` | Playback audio response received |
| `mod_audio_stream::playback` | Playback tracking (chunk_played/queue_completed) |

## Testing

### Unit tests (no FreeSWITCH required)

```bash
mkdir build && cd build && cmake .. && make && ctest --output-on-failure
```

Or directly:
```bash
g++ -std=c++11 -I. -Wall test/test_base64.cpp base64.cpp -o /tmp/t && /tmp/t
g++ -std=c++11 -I. -DUSE_TLS -Wall test/test_ws_client.cpp ws_client.cpp -lssl -lcrypto -o /tmp/t && /tmp/t
g++ -std=c++11 -I. -Wall test/test_ws_uri.cpp stream_utils.cpp -o /tmp/t && /tmp/t
g++ -std=c++11 -I. -Wall test/test_utf8.cpp stream_utils.cpp -o /tmp/t && /tmp/t
```

5 suites, 367 checks:
- `test_base64` - encode/decode, RFC 4648 vectors, error handling (13 checks)
- `test_base64_extra` - edge cases, URL-safe, unicode, large data (265 checks)
- `test_ws_client` - URL parsing, key generation, lifecycle (52 checks)
- `test_ws_uri` - WebSocket URL validation (17 checks)
- `test_utf8` - UTF-8 validation, overlong sequences, surrogates (20 checks)

### Integration tests (requires podman)

```bash
cd test/integration
podman compose build
podman compose up -d
sleep 8
podman exec fs_test bash /usr/src/mod_audio_stream/test/integration/run.sh
podman compose down
```

18 checks against a live FreeSWITCH 1.10.12 + Python WebSocket test server. Covers: module load, API/app registration, bidirectional streaming, pause/resume/break/stop, send_text, error handling, double-start prevention.

## Credits

This module is based on the original [mod_audio_stream](https://github.com/amigniter/mod_audio_stream) by amigniter, released under the MIT license (see `OLD-LICENSE`). The original work established the architecture for FreeSWITCH WebSocket audio streaming - media bug capture, resampling, and JSON-based streamAudio protocol.

Changes in this reimplementation:
- Replaced `libwsc` with a self-contained OpenSSL WebSocket client (`ws_client.cpp`)
- Added bidirectional playback (JSON base64 + raw binary, Opus decode, Speex resampling)
- Added `break` API, playback tracking events, buffer pool (no temp files)
- Removed Debian packaging, CPack, and submodules
- Standalone build: no libevent, just C++11 + OpenSSL + opus + speexdsp

## License

This fork is available under two licenses: the AGPL-3.0, or a proprietary license which is available upon request.