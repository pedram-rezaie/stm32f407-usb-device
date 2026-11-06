# STM32F407 bare-metal USB device driver

A USB full-speed device driver for the OTG_FS peripheral of a self-powered STM32F407, written directly against the registers in RM0090. No HAL, no CMSIS, nothing but `<stdint.h>`.

The device shows up as a vendor-class device with one bulk IN and one bulk OUT endpoint. It answers Microsoft OS 2.0 descriptor requests, so Windows 8.1 and newer load WinUSB automatically: plug it in and talk to it from libusb or pyusb, no driver install and no Zadig.

## Features

- Full enumeration: device, configuration and BOS descriptors, SET_ADDRESS, SET_CONFIGURATION
- MS OS 2.0 descriptors: WinUSB + DeviceInterfaceGUID registered automatically on Windows
- Standard requests: GET_STATUS, CLEAR_FEATURE / SET_FEATURE (endpoint halt), GET_CONFIGURATION, GET_INTERFACE, SET_INTERFACE
- EP1 bulk IN: messages of any length, sent as 64-byte packets from the TX FIFO empty interrupt, ZLP added when a message ends on a full packet
- EP1 bulk OUT: messages up to 64 bytes
- Optional VBUS sensing on PA9
- Unsupported requests are answered with a STALL, as the USB spec requires

## Files

- `usb.h`: the public API, the only header you include
- `usb.c`: the driver
- `usb_regs.h`: register map, used only by `usb.c`, so it never clashes with CMSIS or your own headers

## Requirements

- STM32F407
- HCLK at 168 MHz and a 48 MHz USB clock from PLLQ. `clock_set()` sets this up from an 8 MHz HSE crystal; skip it if your firmware already configures the clocks this way. All timeouts are counted in CPU cycles at 168 MHz.
- PA11 = D-, PA12 = D+
- Optional: VBUS wired to PA9 (5 V tolerant pin) if you use `usb_vbus_sense_activate()`
- The configuration descriptor reports the device as self-powered with 0 mA from VBUS. Change `cfg_desc` in `usb.c` if your board runs from USB power.

## Integration

1. Add `usb.c`, `usb.h` and `usb_regs.h` to your project.
2. Put `otg_fs_handler` in your vector table at IRQ 67 (vector table entry 83, offset 0x14C).
3. Set up the clocks, then call `usb_enable()`.

## API

- `void clock_set(void)`: optional helper, 8 MHz HSE to 168 MHz SYSCLK/HCLK, 48 MHz USB clock
- `void usb_enable(void)`: starts the USB core and the interrupt. VBUS sensing is off by default
- `void usb_vbus_sense_activate(void)`: turns on VBUS sensing on PA9; the core then connects and disconnects D+ by itself. Call after `usb_enable()`
- `uint32_t usb_is_configured(void)`: 1 once the host has configured the device, 0 before that or after a USB reset
- `int usb_data_transfer(const uint32_t *pointer, uint32_t length)`: sends `length` bytes, buffer must be word aligned. Blocks until sent. Returns 0 when sent, -1 if not configured or the host stopped reading (1 s timeout)
- `int usb_receive(uint32_t *buffer, uint32_t length)`: blocks until the host sends a message and copies at most `length` bytes into `buffer`. Returns the number of bytes the host sent; more than `length` means the message was cut
- `void otg_fs_handler(void)`: the OTG_FS interrupt handler, see Integration

## Host side

On Windows WinUSB is loaded automatically. On Linux no driver is needed, but a normal user needs a udev rule for access to the device.

`example_echo_once` receives one message and sends it back, the simplest possible use.
`example_echo` echoes forever, and its `test.py` checks edge cases and 500 random messages.
The scripts need `pip install pyusb libusb-package`.

## Limitations

- Received messages are at most 64 bytes. A longer message from the host arrives split: the first 64 bytes come back from one `usb_receive()` call, the rest from the next.
- `usb_receive()` blocks until a message arrives, with no timeout.
- `usb_data_transfer()` blocks until the whole message is sent.
- Full speed only (12 Mbit/s), one configuration, one interface, EP1 IN + EP1 OUT.
- No string descriptors yet, so the device has no product name or serial number.
- No suspend/resume handling. Without VBUS sensing the driver doesn't notice an unplug.

## Using it in your own product

- VID 0x1209 / PID 0x0001 is the [pid.codes](https://pid.codes) test PID. Get your own PID before shipping anything.
- Generate your own interface GUID and put it in `msos20_desc` in `usb.c`.
- Windows caches the MS OS 2.0 descriptors per VID/PID/bcdDevice. After changing them, bump `bcdDevice` in `dev_desc`, or Windows keeps using the old ones.

## License

MIT, see [LICENSE](LICENSE).
