#!/usr/bin/env python3
"""Test WebSocket server for mod_audio_stream integration tests.

Handles:
- Raw binary audio stream (after rawAudio metadata)
- JSON base64 audio stream (streamAudio messages)
- getStats query (responds with traffic stats)
- HTTP /stats endpoint on port 8081 for external verification
"""

import asyncio
import base64
import json
import struct
import websockets
from websockets.asyncio.server import serve

STATS = {"binary_frames": 0, "json_frames": 0, "bytes_received": 0,
         "streamaudio_echoed": 0, "connections": 0}


def make_silence_pcm(samples=160):
    """Generate silent L16 PCM (160 samples = 20ms at 8kHz)."""
    return struct.pack("<" + "h" * samples, *([0] * samples))


def make_tone_pcm(freq=440, sample_rate=8000, duration_ms=20):
    """Generate a simple sine tone as L16 PCM."""
    import math
    samples = int(sample_rate * duration_ms / 1000)
    result = []
    for i in range(samples):
        t = i / sample_rate
        val = int(16000 * math.sin(2 * math.pi * freq * t))
        result.append(max(-32768, min(32767, val)))
    return struct.pack("<" + "h" * samples, *result)


async def handler(websocket):
    STATS["connections"] += 1
    print(f"[server] connected (conn #{STATS['connections']})", flush=True)
    raw_audio_active = False
    raw_sample_rate = 8000

    try:
        async for message in websocket:
            if isinstance(message, bytes):
                STATS["binary_frames"] += 1
                STATS["bytes_received"] += len(message)
                print(f"[server] binary frame {STATS['binary_frames']}: {len(message)} bytes", flush=True)
                if raw_audio_active:
                    await websocket.send(make_silence_pcm(160))
                continue

            print(f"[server] text message: {message[:200]}", flush=True)
            try:
                data = json.loads(message)
            except json.JSONDecodeError:
                print(f"[server] non-JSON text: {message[:80]}", flush=True)
                continue

            msg_type = data.get("type", "")

            if msg_type == "rawAudio":
                raw_audio_active = True
                raw_sample_rate = data.get("data", {}).get("sampleRate", 8000)
                print(f"[server] rawAudio mode: {raw_sample_rate}Hz")

            elif msg_type == "streamAudio":
                STATS["json_frames"] += 1
                STATS["streamaudio_echoed"] += 1
                tone = make_tone_pcm(440, 8000, 20)
                response = {
                    "type": "streamAudio",
                    "data": {
                        "audioDataType": "raw",
                        "sampleRate": 8000,
                        "audioData": base64.b64encode(tone).decode("ascii"),
                        "test_seq": STATS["json_frames"],
                    },
                }
                await websocket.send(json.dumps(response))

            elif msg_type == "getStats":
                await websocket.send(json.dumps({
                    "type": "stats",
                    "data": dict(STATS),
                }))

            else:
                pass

    except websockets.exceptions.ConnectionClosed:
        print(f"[server] disconnected")


async def http_handler(reader, writer):
    try:
        data = await reader.read(4096)
        body = json.dumps(STATS).encode()
        response = (
            b"HTTP/1.1 200 OK\r\n"
            b"Content-Type: application/json\r\n"
            b"Content-Length: " + str(len(body)).encode() + b"\r\n"
            b"Connection: close\r\n"
            b"\r\n"
            + body
        )
        writer.write(response)
        await writer.drain()
    finally:
        writer.close()


async def main():
    print("[server] WS on :8080, stats HTTP on :8081")
    http_server = await asyncio.start_server(http_handler, "0.0.0.0", 8081)
    async with serve(handler, "0.0.0.0", 8080):
        await asyncio.get_running_loop().create_future()


if __name__ == "__main__":
    asyncio.run(main())
