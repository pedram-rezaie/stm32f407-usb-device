# sends one 70-byte message and checks that it comes back unchanged
# the echo_once firmware only answers one message: reset the board before every run
# needs: pip install pyusb libusb-package
import libusb_package

dev = libusb_package.find(idVendor=0x1209, idProduct=0x0001)
if dev is None:
    raise SystemExit("device not found")
dev.set_configuration()

data = bytes(range(70))
dev.write(0x01, data)
if len(data) % 64 == 0:
    dev.write(0x01, b"")                       # ZLP: message ended
echo = bytes(dev.read(0x81, 100, timeout=1000))
print(len(echo), echo == data)
