# echo test for example/main.c: sends messages of different sizes and checks
# that each one comes back unchanged
# needs: pip install pyusb libusb-package, and the example firmware running
import random
import sys

import libusb_package
import usb.core

VID, PID = 0x1209, 0x0001
EP_OUT, EP_IN = 0x01, 0x81
MAX = 100           # BUFFER_SIZE in example/main.c
PACKET = 64         # full-speed bulk max packet size


def echo(dev, data):
    dev.write(EP_OUT, data)
    # a message ending on a full packet needs a ZLP so the device knows it's over
    # (an empty message already is a single ZLP)
    if len(data) > 0 and len(data) % PACKET == 0:
        dev.write(EP_OUT, b"")
    return bytes(dev.read(EP_IN, MAX, timeout=1000))


def check(dev, data):
    """None if the echo matches, otherwise what went wrong."""
    try:
        back = echo(dev, data)
    except usb.core.USBTimeoutError:
        return "timeout"
    if back != data:
        return f"got {len(back)} bytes back, expected {len(data)}"
    return None


def main():
    dev = libusb_package.find(idVendor=VID, idProduct=PID)
    if dev is None:
        sys.exit("device not found: is the board plugged in and running the example?")
    dev.set_configuration()

    # edge cases: 0 = only a ZLP, 1 and 30 = one short packet, 63 / 65 = around the packet size,
    # 64 = ZLP in both directions, 100 = full buffer (64 + 36)
    print("edge cases")
    for size in (0, 1, 30, 63, 64, 65, MAX):
        data = bytes((i + size) % 256 for i in range(size))
        error = check(dev, data)
        print(f"  {size:4d} bytes: {error or 'ok'}")
        if error:
            sys.exit("stopped: host and device may be out of sync now, reset the board")

    # random sizes and data, fixed seed so a failure can be reproduced
    runs = 500
    rng = random.Random(1)
    print(f"{runs} random messages")
    for run in range(runs):
        size = rng.randint(0, MAX)
        data = rng.randbytes(size)
        error = check(dev, data)
        if error:
            sys.exit(f"  run {run}, {size} bytes: {error}")
    print("  ok")

    print("all passed")


if __name__ == "__main__":
    main()
