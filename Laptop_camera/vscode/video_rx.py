#!/usr/bin/env python3
"""
video_rx.py - receive UDP video frames from the Zynq board and show them.

    pip install numpy opencv-python
    python video_rx.py

Press q in the window to quit.
"""

import socket
import struct
import sys
import time

import numpy as np
import cv2

# ---- Must match video_udp.c ------------------------------------------------
WIDTH  = 640
HEIGHT = 360
BPP    = 3
FRAME_SIZE = WIDTH * HEIGHT * BPP

LISTEN_IP   = "0.0.0.0"
LISTEN_PORT = 5001

HEADER = struct.Struct("<II")      # frame_id, offset  (little endian)

# Xilinx video IP and OpenCV don't always agree on channel order.
# If faces look blue, flip this.
SWAP_RB = True


def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    # A big receive buffer matters a lot here. The default is often 64 KB,
    # which one frame overruns immediately, and the kernel silently drops
    # packets - you'd see horizontal bands of stale image.
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 16 * 1024 * 1024)
    except OSError:
        print("warning: could not enlarge receive buffer")

    sock.bind((LISTEN_IP, LISTEN_PORT))
    sock.settimeout(2.0)

    print(f"listening on {LISTEN_IP}:{LISTEN_PORT}, expecting {WIDTH}x{HEIGHT}")

    frame = bytearray(FRAME_SIZE)
    current_id = None
    received = 0

    frames_shown = 0
    last_report = time.time()
    fps = 0.0

    cv2.namedWindow("FPGA", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("FPGA", WIDTH, HEIGHT)

    while True:
        try:
            packet = sock.recv(65535)
        except socket.timeout:
            print("no packets - is the board running and on the same subnet?")
            continue

        if len(packet) < HEADER.size:
            continue

        frame_id, offset = HEADER.unpack_from(packet, 0)
        payload = packet[HEADER.size:]

        # A new frame_id means the previous frame is over. Whatever we have
        # is what we got - render it and start fresh. Incomplete frames just
        # show stale data in the missing region, which is the right trade for
        # a live view.
        if current_id is None:
            current_id = frame_id

        elif frame_id != current_id:
            if received > FRAME_SIZE * 0.5:      # ignore badly torn frames
                img = np.frombuffer(bytes(frame), dtype=np.uint8)
                img = img.reshape((HEIGHT, WIDTH, BPP))

                if SWAP_RB:
                    img = img[:, :, ::-1]

                display = img.copy()
                cv2.putText(display, f"{fps:.1f} fps", (10, 25),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
                cv2.imshow("FPGA", display)

                frames_shown += 1
                now = time.time()
                if now - last_report >= 1.0:
                    fps = frames_shown / (now - last_report)
                    frames_shown = 0
                    last_report = now

                if cv2.waitKey(1) & 0xFF == ord("q"):
                    break

            current_id = frame_id
            received = 0

        end = offset + len(payload)
        if end <= FRAME_SIZE:
            frame[offset:end] = payload
            received += len(payload)

    sock.close()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nstopped")
        sys.exit(0)
