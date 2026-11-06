#ifndef USB_H
#define USB_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bare-metal USB OTG_FS device driver for the STM32F407.
// One vendor-class interface (WinUSB on Windows, no driver install), EP1 bulk IN + OUT, 64-byte packets.
// Needs 168 MHz HCLK and a 48 MHz USB clock from PLLQ before usb_enable.

// optional helper: 8 MHz HSE crystal -> 168 MHz SYSCLK/HCLK, 48 MHz USB clock (PLLQ)
// skip it if your firmware already sets the clocks up like this
void     clock_set(void);

// starts the USB core, PA11 = D-, PA12 = D+. VBUS sensing off by default
void     usb_enable(void);

// optional: VBUS wired to PA9, the core then connects/disconnects D+ by itself. call after usb_enable
void     usb_vbus_sense_activate(void);

// 1 once the host has configured the device, 0 before that or after a USB reset
uint32_t usb_is_configured(void);

// sends length bytes from pointer (word aligned), blocking, 1 s timeout per part
// returns 0 sent, -1 not configured or the host stopped reading
int      usb_data_transfer(const uint32_t *pointer, uint32_t length);

// blocks until the host sends a message (max 64 bytes), copies at most length bytes into buffer
// returns the bytes the host sent, more than length means the message was cut
int      usb_receive(uint32_t *buffer, uint32_t length);

// OTG_FS interrupt handler, put it in your vector table at IRQ 67
void     otg_fs_handler(void);

#ifdef __cplusplus
}
#endif
#endif
