#!/usr/bin/env python3
import argparse
import asyncio
import struct
import websockets

# Binary Packet Definitions
IMU_STRUCT = struct.Struct("<BIIhhhhhh") # 21 bytes
TELEMETRY_STRUCT = struct.Struct("<BIf f f BBBBB") # 22 bytes? No, wait.
# Let's check the C++ struct again.
# struct TelemetryPacket {
#   uint8_t header; // 1
#   uint32_t sampleMs; // 4
#   float temp; // 4
#   float volt; // 4
#   float curr; // 4
#   uint8_t bat; // 1
#   uint8_t cpu; // 1
#   uint8_t rtcHour; // 1
#   uint8_t rtcMin; // 1
#   uint8_t rtcSec; // 1
# };
# Total: 1+4+4+4+4+1+1+1+1+1 = 22 bytes.
TELEMETRY_STRUCT = struct.Struct("<BI f f f BBBBB")

HEADER_IMU = 0xA1
HEADER_STATUS = 0xE1
HEADER_TELEMETRY = 0xD4

def format_imu(data):
    header, seq, sample_us, gx, gy, gz, ax, ay, az = IMU_STRUCT.unpack(data)
    # Reverting to the user's preferred mapping: Gyro at 0,1,2 and Accel at 3,4,5
    gx_dps, gy_dps, gz_dps = gx / 16.4, gy / 16.4, gz / 16.4
    ax_g, ay_g, az_g = ax / 16384.0, ay / 16384.0, az / 16384.0
    return (f"IMU [#{seq:6d}] t={sample_us/1000:8.2f}ms "
            f"G=({gx_dps:7.1f},{gy_dps:7.1f},{gz_dps:7.1f}) "
            f"A=({ax_g:6.2f},{ay_g:6.2f},{az_g:6.2f})")

def format_telemetry(data):
    header, ms, temp, volt, curr, bat, cpu, h, m, s = TELEMETRY_STRUCT.unpack(data)
    return (f"TEL [t={ms:8d}ms] {h:02d}:{m:02d}:{s:02d} | "
            f"Temp: {temp:4.1f}C | Bat: {bat:3d}% ({volt:4.2f}V) | "
            f"Load: {cpu:2d}% | I: {curr:5.1f}mA")

async def listen(uri):
    async with websockets.connect(uri) as websocket:
        print(f"Connected to {uri}")
        print("-" * 80)
        while True:
            try:
                message = await websocket.recv()
                if isinstance(message, bytes):
                    header = message[0]
                    if header in (HEADER_IMU, HEADER_STATUS) and len(message) == IMU_STRUCT.size:
                        print(format_imu(message), flush=True)
                    elif header == HEADER_TELEMETRY and len(message) == TELEMETRY_STRUCT.size:
                        print("\033[92m" + format_telemetry(message) + "\033[0m", flush=True)
                    else:
                        print(f"Unknown Packet: Header=0x{header:02X} Size={len(message)}")
                else:
                    print(f"Text Message: {message}")
            except websockets.ConnectionClosed:
                print("Connection closed")
                break

async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="192.168.4.1")
    parser.add_argument("--port", type=int, default=3333)
    args = parser.parse_args()

    uri = f"ws://{args.host}:{args.port}"
    while True:
        try:
            await listen(uri)
        except Exception as e:
            print(f"Connection failed: {e}. Retrying in 2s...")
            await asyncio.sleep(2)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
