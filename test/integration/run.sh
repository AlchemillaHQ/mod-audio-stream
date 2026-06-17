#!/bin/bash
# Integration tests for mod_audio_stream
# Run inside the container after FS and test server are up.
set -e

FS_CLI="fs_cli -x"
PASS=0
FAIL=0

check() {
    local desc="$1" cmd="$2" expected="$3"
    echo -n "  $desc ... "
    local result
    result=$($FS_CLI "$cmd" 2>&1) || true
    if echo "$result" | grep -q "$expected"; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL (got: $result)"
        FAIL=$((FAIL + 1))
    fi
}

STATS_URL="http://ws_test_server:8081/stats"
stats_field() {
    python3 -c "import urllib.request,json;r=urllib.request.urlopen('$STATS_URL');print(json.loads(r.read()).get('$1',0))" 2>/dev/null || echo 0
}

echo "=== Integration Tests ==="
echo ""

# ---- basic module checks ----
echo "--- Basic Module ---"
check "module loaded" \
    "module_exists mod_audio_stream" "true"
check "app registered" \
    "uuid_audio_stream" "USAGE"
check "show api lists module" \
    "show api uuid_audio_stream" "mod_audio_stream"

# ---- bi-directional streaming test ----
echo ""
echo "--- Bi-directional Streaming ---"

ORIG_OUT=$($FS_CLI 'bgapi originate {origination_caller_id_number=1000}loopback/test_001 9196' 2>&1) || true
echo "  Originate: $ORIG_OUT"
sleep 5

CH_UUID=$(fs_cli -x 'show channels' 2>&1 | grep ',inbound,' | grep -oP '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}' | tail -1) || true
echo "  Channel UUID: $CH_UUID"

if [ -z "$CH_UUID" ]; then
    echo "  No active channels found."
    echo "=== Results: $PASS passed, $FAIL failed ==="
    exit $FAIL
fi

check "call active"           "uuid_exists $CH_UUID"   "true"

# ---- end-to-end audio flow ----
echo ""
echo "--- End-to-End Audio Flow ---"

# Send rawAudio and streamAudio immediately before binary frames fill the queue
check "activate rawAudio mode" \
    "uuid_audio_stream $CH_UUID send_text {\"type\":\"rawAudio\",\"data\":{\"sampleRate\":8000}}" "+OK"
sleep 1
PRE_ECHO=$(stats_field streamaudio_echoed)
check "send streamAudio (reverse path)" \
    "uuid_audio_stream $CH_UUID send_text {\"type\":\"streamAudio\",\"data\":{\"audioDataType\":\"raw\",\"sampleRate\":8000,\"audioData\":\"AAAA\",\"e2e_test\":1}}" "+OK"
sleep 4
POST_ECHO=$(stats_field streamaudio_echoed)
echo -n "  reverse playback path ... "
if [ "$POST_ECHO" -gt "$PRE_ECHO" ] 2>/dev/null; then
    echo "PASS (server echoed streamAudio: $PRE_ECHO -> $POST_ECHO)"
    PASS=$((PASS + 1))
else
    echo "FAIL (no echo: $PRE_ECHO -> $POST_ECHO)"
    FAIL=$((FAIL + 1))
fi

# Verify forward path: binary audio frames reach the server
sleep 2
BIN=$(stats_field binary_frames)
BYTES=$(stats_field bytes_received)
CONN=$(stats_field connections)
echo -n "  forward audio streaming ... "
if [ "$BIN" -gt 0 ] 2>/dev/null && [ "$CONN" -gt 0 ] 2>/dev/null; then
    echo "PASS ($BIN frames / $BYTES bytes, $CONN WS connections)"
    PASS=$((PASS + 1))
else
    echo "FAIL (frames=$BIN connections=$CONN)"
    FAIL=$((FAIL + 1))
fi

# ---- pause / resume ----
echo "--- Pause / Resume ---"
check "pause"                 "uuid_audio_stream $CH_UUID pause"       "+OK"
check "resume"                "uuid_audio_stream $CH_UUID resume"      "+OK"
check "pause again"           "uuid_audio_stream $CH_UUID pause"       "+OK"
check "resume again"          "uuid_audio_stream $CH_UUID resume"      "+OK"

# ---- text & metadata ----
echo ""
echo "--- Text & Metadata ---"
check "send_text"             "uuid_audio_stream $CH_UUID send_text '{\"command\":\"ping\"}'"  "+OK"
check "send_text special chars" "uuid_audio_stream $CH_UUID send_text 'hello world'"            "+OK"

# ---- break ----
echo ""
echo "--- Break ---"
check "break"                 "uuid_audio_stream $CH_UUID break"       "+OK"

# ---- stop ----
echo ""
echo "--- Stop ---"
check "stop"                  "uuid_audio_stream $CH_UUID stop"        "+OK"
sleep 1

# ---- error cases ----
echo ""
echo "--- Error Handling ---"
check "invalid cmd"           "uuid_audio_stream $CH_UUID invalid_cmd" "Operation Failed"
check "stop on stopped"       "uuid_audio_stream $CH_UUID stop"        "Operation Failed"
check "pause on stopped"      "uuid_audio_stream $CH_UUID pause"       "Operation Failed"
check "bad uuid"              "uuid_audio_stream deadbeef-dead-beef-dead-beefdeadbeef stop" "Operation Failed"

# ---- double start (should fail because bug already attached) ----
echo ""
echo "--- Double Start ---"
ORIG_OUT2=$($FS_CLI 'bgapi originate {origination_caller_id_number=1000}loopback/test_001 9196' 2>&1) || true
sleep 5
CH2=$(fs_cli -x 'show channels' 2>&1 | grep ',inbound,' | grep -oP '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}' | tail -1) || true
if [ -n "$CH2" ] && [ "$CH2" != "$CH_UUID" ]; then
    check "restart blocked"   "uuid_audio_stream $CH2 start ws://ws_test_server:8080/audio mono 8k" "Operation Failed"
    check "stop second call"  "uuid_audio_stream $CH2 stop" "+OK"
fi

# ---- 24000 Hz Streaming ----
echo ""
echo "--- 24000 Hz Streaming ---"
ORIG_24K=$($FS_CLI 'bgapi originate {origination_caller_id_number=1000}loopback/test_24k_001 9196' 2>&1) || true
echo "  Originate 24k: $ORIG_24K"
sleep 5

CH_24K=$(fs_cli -x 'show channels' 2>&1 | grep ',inbound,' | grep -oP '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}' | tail -1) || true
echo "  Channel UUID 24k: $CH_24K"

if [ -n "$CH_24K" ]; then
    check "24k call active"      "uuid_exists $CH_24K"   "true"
    check "24k pause"            "uuid_audio_stream $CH_24K pause"  "+OK"
    check "24k resume"           "uuid_audio_stream $CH_24K resume" "+OK"

    # 24k forward audio: verify binary frames flow at 24000 Hz
    sleep 2
    BIN_24K=$(stats_field binary_frames)
    echo -n "  24k forward audio streaming ... "
    if [ "$BIN_24K" -gt "$BIN" ] 2>/dev/null; then
        echo "PASS ($BIN_24K frames — audio flowing at 24k)"
        PASS=$((PASS + 1))
    else
        echo "FAIL (before=$BIN after=$BIN_24K)"
        FAIL=$((FAIL + 1))
    fi

    check "24k break"            "uuid_audio_stream $CH_24K break"  "+OK"
    check "24k stop"             "uuid_audio_stream $CH_24K stop"   "+OK"
fi

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
exit $FAIL
