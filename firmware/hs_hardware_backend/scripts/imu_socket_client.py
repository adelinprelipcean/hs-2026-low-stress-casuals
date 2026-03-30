#!/usr/bin/env python3
import argparse
import socket
import struct
import time

PACKET_STRUCT = struct.Struct("<BIIhhhhhh")
PACKET_SIZE = PACKET_STRUCT.size
HEADER_RAW_IMU = 0xA1
HEADER_STATUS = 0xE1


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("Socket closed by peer")
        data.extend(chunk)
    return bytes(data)


def format_packet(seq: int, sample_us: int, gx: int, gy: int, gz: int, ax: int, ay: int, az: int) -> str:
    gx_dps = gx / 16.4
    gy_dps = gy / 16.4
    gz_dps = gz / 16.4
    ax_g = ax / 16384.0
    ay_g = ay / 16384.0
    az_g = az / 16384.0

    return (
        f"seq={seq:10d} "
        f"t_us={sample_us:10d} "
        f"gyro[dps]=({gx_dps:8.2f},{gy_dps:8.2f},{gz_dps:8.2f}) "
        f"acc[g]=({ax_g:7.3f},{ay_g:7.3f},{az_g:7.3f})"
    )


def format_status(seq: int, sample_us: int) -> str:
    return f"seq={seq:10d} t_us={sample_us:10d} status=IMU_UNAVAILABLE_OR_READ_FAIL"


def run_client(host: str, port: int, reconnect_s: float) -> None:
    print(f"Connecting to {host}:{port} (expecting {PACKET_SIZE}-byte packets)...")

    while True:
        sock = None
        try:
            sock = socket.create_connection((host, port), timeout=5.0)
            sock.settimeout(5.0)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            print("Connected. Streaming IMU data:\n")

            while True:
                packet = recv_exact(sock, PACKET_SIZE)
                header, seq, sample_us, gx, gy, gz, ax, ay, az = PACKET_STRUCT.unpack(packet)

                if header == HEADER_RAW_IMU:
                    print(format_packet(seq, sample_us, gx, gy, gz, ax, ay, az), flush=True)
                elif header == HEADER_STATUS:
                    print(format_status(seq, sample_us), flush=True)
                else:
                    print(
                        f"seq={seq:10d} t_us={sample_us:10d} unknown_header=0x{header:02X}",
                        flush=True,
                    )

        except KeyboardInterrupt:
            print("\nStopped by user.")
            return
        except Exception as exc:
            print(f"Connection error: {exc}")
            print(f"Reconnecting in {reconnect_s:.1f}s...\n")
            time.sleep(reconnect_s)
        finally:
            if sock is not None:
                try:
                    sock.close()
                except Exception:
                    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Connect to ESP32 IMU TCP stream and print decoded data in real time."
    )
    parser.add_argument("--host", default="192.168.4.1", help="ESP32 AP IP (default: 192.168.4.1)")
    parser.add_argument("--port", type=int, default=3333, help="ESP32 IMU socket port (default: 3333)")
    parser.add_argument(
        "--reconnect",
        type=float,
        default=1.0,
        help="Reconnect delay in seconds after disconnect (default: 1.0)",
    )
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    run_client(args.host, args.port, args.reconnect)
