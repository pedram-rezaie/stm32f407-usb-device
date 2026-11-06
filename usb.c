// Bare-metal USB OTG_FS device driver for the STM32F407
// one vendor-class interface (WinUSB, no driver install), EP1 bulk IN + OUT, 64-byte packets
#include "usb.h"
#include "usb_regs.h"

/* ======================================================================
   internal functions
   ====================================================================== */
static void usbrst_handler(void);
static void enumdne_handler(void);
static void rxflvl_handler(void);
static void data_out_handler(uint32_t epnum, uint32_t bcnt);
static void setup_handler(void);
static void ep0_out_arm(void);
static void ep0_stall(void);
static void ep0_in_arm(uint32_t size);
static void ep0_send(const uint8_t *pointer, uint32_t len);
static void ep0_send_data(const uint8_t *pointer, uint32_t size, uint16_t wLength);
static volatile uint32_t *ep_ctl(uint16_t wIndex);
static void ep1_in_arm(uint32_t packets, uint32_t bytes);
static void ep1_send(void);
static void ep1_abort(void);
static inline uint32_t min(uint32_t a, uint32_t b){ return a < b ? a : b; }

/* ======================================================================
   state
   ====================================================================== */
// EP0
static uint32_t setup[2];                               // last SETUP packet, 8 bytes
static uint32_t ep0_receive_buf[16];                    // EP0 OUT data, 64 bytes
static volatile uint8_t current_config = 0;             // 0 not configured, 1 configured (GET_CONFIGURATION)
static const uint8_t alt_setting = 0;                   // only alternate setting 0 exists (GET_INTERFACE)

// EP1 OUT
static uint32_t cmd_buf[16];                            // last EP1 OUT packet, 64 bytes
static volatile uint32_t message_received = 0;          // set to 1 in RXFLVL case 0x3
static volatile uint32_t ep1_bcnt = 0;                  // size of the last EP1 OUT packet, set in case 0x2

// EP1 IN
static volatile int ep1_message_length = 0;             // bytes of the message not written to the FIFO yet
static const uint32_t * volatile ep1_message_pointer;   // next word to write to the FIFO
static volatile int ep1_armed_length = 0;               // bytes of the armed part not written to the FIFO yet

/* ======================================================================
   descriptors
   ====================================================================== */
static const uint8_t dev_desc[20] = {
    0x12, 0x01,       // bLength 18, bDescriptorType DEVICE
    0x01, 0x02,       // bcdUSB 2.01: Windows asks for the BOS descriptor
    0x00, 0x00, 0x00, // class, subclass, protocol: defined per interface
    0x40,             // bMaxPacketSize0 64
    0x09, 0x12,       // idVendor 0x1209 (pid.codes)
    0x01, 0x00,       // idProduct 0x0001 (test PID)
    0x01, 0x01,       // bcdDevice 1.01
    0x00, 0x00, 0x00, // no string descriptors
    0x01,             // bNumConfigurations 1
    0x00, 0x00
};

static const uint8_t cfg_desc[32] = {
    // configuration
    0x09, 0x02,       // bLength 9, bDescriptorType CONFIGURATION
    0x20, 0x00,       // wTotalLength 32
    0x01,             // bNumInterfaces 1
    0x01,             // bConfigurationValue 1 (SET_CONFIGURATION sends this in wValue)
    0x00,             // iConfiguration, no string
    0xC0,             // bmAttributes: bit 7 reserved = 1, bit 6 self-powered = 1, bit 5 no remote wakeup
    0x00,             // bMaxPower 0 mA, nothing on the board runs from VBUS
    // interface 0
    0x09, 0x04,       // bLength 9, bDescriptorType INTERFACE
    0x00,             // bInterfaceNumber 0
    0x00,             // bAlternateSetting 0
    0x02,             // bNumEndpoints 2
    0xFF, 0x00, 0x00, // class vendor, subclass 0, protocol 0
    0x00,             // iInterface, no string
    // EP1 IN
    0x07, 0x05,       // bLength 7, bDescriptorType ENDPOINT
    0x81,             // bEndpointAddress: EP1, IN (bit 7)
    0x02,             // bmAttributes: bulk
    0x40, 0x00,       // wMaxPacketSize 64
    0x00,             // bInterval, ignored for bulk
    // EP1 OUT
    0x07, 0x05,       // bLength 7, bDescriptorType ENDPOINT
    0x01,             // bEndpointAddress: EP1, OUT
    0x02,             // bmAttributes: bulk
    0x40, 0x00,       // wMaxPacketSize 64
    0x00              // bInterval, ignored for bulk
};

static const uint8_t bos_desc[33] = {
    // BOS header
    0x05, 0x0F,             // bLength 5, bDescriptorType BOS (15)
    0x21, 0x00,             // wTotalLength 33
    0x01,                   // bNumDeviceCaps 1
    // platform capability: MS OS 2.0
    0x1C, 0x10, 0x05, 0x00, // bLength 28, DEVICE CAPABILITY (16), PLATFORM (5), reserved
    0xDF, 0x60, 0xDD, 0xD8, // MS OS 2.0 UUID {D8DD60DF-4589-4CC7-9CD2-659D9E648A9F},
    0x89, 0x45, 0xC7, 0x4C, //   first three fields little endian, last 8 bytes as written
    0x9C, 0xD2, 0x65, 0x9D,
    0x9E, 0x64, 0x8A, 0x9F,
    0x00, 0x00, 0x03, 0x06, // dwWindowsVersion 0x06030000 = Windows 8.1 and newer
    0xA2, 0x00,             // wMSOSDescriptorSetTotalLength 162 (msos20_desc below)
    0x01,                   // bMS_VendorCode 1: Windows asks for the set with bRequest = 1
    0x00                    // bAltEnumCode 0
};

// format: Microsoft OS 2.0 Descriptors Specification
// tells Windows to load WinUSB and register the interface GUID below
// GUID {96FB8FAD-018A-4EAC-94A0-87C43CD67A74}, generate your own for your product
// Windows caches these per VID/PID/bcdDevice: bump bcdDevice after changing them
static const uint8_t msos20_desc[162] = {
    // set header
    0x0A, 0x00,             // wLength 10
    0x00, 0x00,             // wDescriptorType 0, SET_HEADER_DESCRIPTOR
    0x00, 0x00, 0x03, 0x06, // dwWindowsVersion 0x06030000, Windows 8.1 and newer
    0xA2, 0x00,             // wTotalLength 162
    // compatible ID
    0x14, 0x00,             // wLength 20
    0x03, 0x00,             // wDescriptorType 3, FEATURE_COMPATIBLE_ID
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,           // CompatibleID
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // SubCompatibleID
    // registry property
    0x84, 0x00,             // wLength 132
    0x04, 0x00,             // wDescriptorType 4, FEATURE_REG_PROPERTY
    0x07, 0x00,             // wPropertyDataType 7, REG_MULTI_SZ
    0x2A, 0x00,             // wPropertyNameLength 42
    'D',0,'e',0,'v',0,'i',0,'c',0,'e',0,                // "DeviceInterfaceGUIDs", UTF-16LE
    'I',0,'n',0,'t',0,'e',0,'r',0,'f',0,'a',0,'c',0,'e',0,
    'G',0,'U',0,'I',0,'D',0,'s',0,
    0x00, 0x00,             // end of name
    0x50, 0x00,             // wPropertyDataLength 80
    '{',0,'9',0,'6',0,'F',0,'B',0,'8',0,'F',0,'A',0,'D',0,'-',0,
    '0',0,'1',0,'8',0,'A',0,'-',0,
    '4',0,'E',0,'A',0,'C',0,'-',0,
    '9',0,'4',0,'A',0,'0',0,'-',0,
    '8',0,'7',0,'C',0,'4',0,'3',0,'C',0,'D',0,'6',0,'7',0,'A',0,'7',0,'4',0,'}',0,
    0x00, 0x00,             // end of string
    0x00, 0x00              // end of list (MULTI_SZ)
};

/* ======================================================================
   public API
   ====================================================================== */

// HSE/M -> PLL input; *N -> VCO; VCO/P -> SYSCLK; VCO/Q -> USB
void clock_set(void){
    // flash at 168 MHz needs 5 wait states
    *FLASH_ACR = 0x00000005;

    // PLLM = 4 (VCO in = 8/4 MHz); PLLN = 168 (VCO out = 2*168 MHz); PLLP = 2 (SYSCLK = VCO/2 = 168 MHz);
    // PLLQ = 7 (USB = VCO/7 = 48 MHz); PLLSRC = 1 (HSE)
    (*RCC).PLLCFGR &= 0xF03C0000;
    (*RCC).PLLCFGR |= 0x07402A04;

    // AHB /1, APB1 /4, APB2 /2, SYSCLK source = PLL
    (*RCC).CFGR &= 0x0000030C;
    (*RCC).CFGR |= 0x00089402;

    // bit 16 HSE on; bit 24 PLL on, the PLL has to be configured before it's enabled
    (*RCC).CR |= 0x00010000;
    while (1){                              // bit 17: HSE ready
        uint32_t temp = (*RCC).CR;
        temp = temp & 0x00020000;
        if (temp) break;
    }
    (*RCC).CR |= 0x01000000;
    while (1){                              // bit 25: PLL ready
        uint32_t temp = (*RCC).CR;
        temp = temp & 0x02000000;
        if (temp) break;
    }
    while (1){                              // bits 3:2 system clock switch status, 10 = PLL
        uint32_t temp = (*RCC).CFGR;
        temp = temp & 0x00000008;
        if (temp) break;
    }
}

void usb_enable(void){
    *DEMCR    |= 0x01000000;          // TRCENA (bit 24): DWT block on
    *DWT_CTRL |= 0x00000001;          // CYCCNTENA (bit 0): cycle counter running
    (*RCC).AHB1ENR |= 0x00000001;     // GPIOA clock enable
    (void)(*RCC).AHB1ENR;
    (*RCC).AHB2ENR |= 0x00000080;     // OTG_FS clock enable
    (void)(*RCC).AHB2ENR;

    // PA11, PA12 are USB D-, D+ on alternate function 10
    (*GPIOA).AFRH |= 0x000AA000;
    uint32_t mask = 0x03C00000;
    (*GPIOA).MODER &= ~(mask);
    (*GPIOA).MODER |= 0x02800000;
    (*GPIOA).OSPEEDR |= 0x03C00000;

    // core soft reset, once the AHB master is idle (AHBIDL, bit 31)
    while (1){
        uint32_t temp = (*OTG_FS).GRSTCTL;
        uint32_t mask = 0x80000000;
        temp &= mask;
        if (temp){
            (*OTG_FS).GRSTCTL |= 0x00000030;     // FIFO flush
            (*OTG_FS).GRSTCTL |= 0x00000001;     // CSRST
            while (1){                           // wait until the reset is done
                uint32_t temp2 = (*OTG_FS).GRSTCTL;
                uint32_t mask2 = 0x00000001;
                temp2 &= mask2;
                if (!temp2) break;
            }
            break;
        }
    }

    // force device mode, then USB turnaround time 6
    (*OTG_FS).GUSBCFG  = 0x00000000;
    (*OTG_FS).GUSBCFG |= 0x40000000;
    uint32_t t0 = *DWT_CYCCNT;
    while ((*DWT_CYCCNT - t0) < 4200000){}  // 25 ms at 168 MHz for the mode change
    (*OTG_FS).GUSBCFG |= 0x00001800;

    // PHY on, VBUS sensing off (usb_vbus_sense_activate turns it on)
    (*OTG_FS).GCCFG = 0x00210000;
    // full speed
    (*OTG_FS).DCFG |= 0x00000003;
    // interrupts: USBRST | ENUMDNE
    (*OTG_FS).GINTMSK = 0x00003000;
    // global interrupt enable; TXFE is only turned on while sending
    (*OTG_FS).GAHBCFG = 0x00000001;

    // FIFOs, in words: RX 128, EP0 TX 64 at 128, EP1 TX 128 at 192 (320 total)
    (*OTG_FS).GRXFSIZ = 0x80;
    (*OTG_FS).DIEPTXF0 = 0x00400080;
    (*OTG_FS).DIEPTXF[0] = 0x008000C0;

    // NVIC: IRQ 67 (OTG_FS), the last gate before the interrupt can fire
    (*NVIC).ISER[2] = 0x00000008;
}

void usb_vbus_sense_activate(void){
    (*OTG_FS).GCCFG = 0x00090000;    // VBUSBSEN (bit 19) | PWRDWN (bit 16), NOVBUSSENS (bit 21) cleared
}

uint32_t usb_is_configured(void){
    return current_config;
}

// GINTSTS IEPINT (bit 18)
//   └─ DAINT bit 1 (EP1 IN)
//        └─ DIEPINT1 bit 7 (TXFE)
int usb_data_transfer(const uint32_t *pointer, uint32_t length){
    if (!current_config) return -1;                         // EP1 IN isn't active before SET_CONFIGURATION
    ep1_message_length = length;
    ep1_message_pointer = pointer;
    // one arm can hold at most 1023 packets, so bigger messages go out in parts
    while (ep1_message_length > 0){
        ep1_armed_length = min(ep1_message_length, 65472);
        ep1_in_arm(min((ep1_message_length + 63) / 64, 1023), ep1_armed_length);
        // XFRC is only polled, not used as an interrupt; clear it before using it
        (*OTG_FS).IEP[1].DIEPINT = 0x00000001;
        // TXFE for EP1 (DIEPEMPMSK bit 1) and IEPINT (GINTMSK bit 18): from here otg_fs_handler fills the FIFO
        (*OTG_FS).DIEPEMPMSK |= 0x00000002;
        (*OTG_FS).GINTMSK |= 0x00040000;
        uint32_t t0 = *DWT_CYCCNT;
        while (!((*OTG_FS).IEP[1].DIEPINT & 0x00000001)){   // XFRC = bit 0
            if ((*DWT_CYCCNT - t0) > 168000000){            // 1 s per part: host stopped reading
                ep1_abort();
                return -1;
            }
        }
        (*OTG_FS).GINTMSK &= 0xFFFBFFFF;
        (*OTG_FS).DIEPEMPMSK &= 0xFFFFFFFD;
    }
    // message ended on a full packet (64, 128, ...): host needs a ZLP to see the end
    if ((length & 0x3F) == 0){
        (*OTG_FS).IEP[1].DIEPINT = 0x00000001;              // clear XFRC
        ep1_in_arm(1, 0);
        uint32_t t0 = *DWT_CYCCNT;
        while (!((*OTG_FS).IEP[1].DIEPINT & 0x00000001)){
            if ((*DWT_CYCCNT - t0) > 168000000){ ep1_abort(); return -1; }
        }
    }
    return 0;
}

int usb_receive(uint32_t *buffer, uint32_t length){
    while (1){
        while (!current_config){}                    // wait until the host configured us
        message_received = 0;                        // clear before arming, not after
        (*OTG_FS).OEP[1].DOEPTSIZ = 0x00080040;      // PKTCNT 1 (bit 19), XFRSIZ 64
        (*OTG_FS).OEP[1].DOEPCTL |= 0x84000000;      // EPENA (31) | CNAK (26)
        while (!message_received && current_config){}
        if (message_received) break;
        // reset or unconfigure while waiting: wait for the next SET_CONFIGURATION, arm again
    }
    // EP1 is already NAKing here, nothing to disable
    uint8_t *src = (uint8_t *)cmd_buf;
    uint8_t *dst = (uint8_t *)buffer;
    uint32_t n = min(ep1_bcnt, length);
    for (uint32_t i = 0; i < n; ++i) dst[i] = src[i];   // byte copy, so length doesn't have to be a multiple of 4
    return (int)ep1_bcnt;
}

/* ======================================================================
   interrupt handling
   ====================================================================== */

// an interrupt reaches the CPU only if all of these let it through:
//     DOEPMSK/DIEPMSK  (which events)
//   AND DAINTMSK       (which endpoints)
//   AND GINTMSK 18/19
//   AND GAHBCFG bit 0
//   AND NVIC ISER2 bit 3
//
//     GINTSTS
//  ├─ OEPINT (bit 19): "some OUT endpoint has an event"
//  │    └─ DAINT bits 31:16: which OUT endpoint (bit 16 = EP0, bit 17 = EP1...)
//  │         └─ DOEPINTx: which event (STUP, XFRC, EPDISD...)
//  ├─ IEPINT (bit 18): "some IN endpoint has an event"
//  │    └─ DAINT bits 15:0: which IN endpoint (bit 0 = EP0...)
//  │         └─ DIEPINTx: which event (XFRC, TOC, TXFE...)
//  └─ RXFLVL (bit 4): "a packet is waiting in the RX FIFO"
//       └─ GRXSTSP: which endpoint (EPNUM), what kind (PKTSTS), how big (BCNT)
void otg_fs_handler(void){
    // interrupts that are pending and also enabled
    uint32_t status = (*OTG_FS).GINTMSK & (*OTG_FS).GINTSTS;
    // USBRST, bit 12
    if (status & 0x00001000){
        usbrst_handler();
    }
    // ENUMDNE, bit 13
    if (status & 0x00002000){
        enumdne_handler();
    }
    // RXFLVL, bit 4: a packet has arrived
    if (status & 0x00000010){
        rxflvl_handler();
    }
    // IEPINT, bit 18
    if (status & 0x00040000){
        // DAINT bit 1 (Device All endpoints INTerrupt, EP1 IN): the only IN interrupt enabled is TXFE
        uint32_t daint = (*OTG_FS).DAINT;
        if (daint & 0x00000002){
            if (!(ep1_armed_length > 0)){
                (*OTG_FS).GINTMSK &= 0xFFFBFFFF;
                (*OTG_FS).DIEPEMPMSK &= 0xFFFFFFFD;
            }
            ep1_send();
        }
    }
}

static void usbrst_handler(void){
    current_config = 0;                      // unconfigured until the next SET_CONFIGURATION
    (*OTG_FS).DIEPMSK = 0x00000000;          // no IN endpoint events
    (*OTG_FS).DAINTMSK = 0x00000000;         // no endpoint may interrupt
    // all OUT endpoints NAK
    (*OTG_FS).OEP[1].DOEPCTL |= 0x08000000;
    (*OTG_FS).OEP[2].DOEPCTL |= 0x08000000;
    (*OTG_FS).OEP[3].DOEPCTL |= 0x08000000;
    // DOEPMSK: which OUT events are allowed at all: XFRCM (transfer complete), STUPM (SETUP phase done)
    (*OTG_FS).DOEPMSK = 0x00000009;
    // RX FIFO size in words (4 bytes each), 320 words of FIFO RAM in total
    (*OTG_FS).GRXFSIZ = 128;
    // EP0 TX FIFO start and size
    (*OTG_FS).DIEPTXF0 = 0x00400080;
    // DOEPTSIZ0: what EP0 expects from the host. STUPCNT bits 30:29, PKTCNT always 1 on EP0
    (*OTG_FS).OEP[0].DOEPTSIZ = 0x60000000;
    // address 0, full speed
    (*OTG_FS).DCFG = 0x02200003;
    (*OTG_FS).GRSTCTL = 0x00000420;                 // TXFFLSH (bit 5), TXFNUM = 0x10 (bits 10:6) = all TX FIFOs
    while ((*OTG_FS).GRSTCTL & 0x00000020){}        // wait until the flush is done
    (*OTG_FS).DIEPEMPMSK = 0x00000000;              // no TXFE requests
    (*OTG_FS).GINTSTS = 0x00001000;                 // clear USBRST
}

static void enumdne_handler(void){
    // DIEPCTL0 (Device IN EndPoint ConTroL 0): MPSIZ bits 1:0 = 0 -> 64 bytes, for EP0 IN and OUT
    (*OTG_FS).IEP[0].DIEPCTL = 0x00000000;
    // GINTMSK bit 4 RXFLVL: the host sent something and it's waiting in the RX FIFO
    (*OTG_FS).GINTMSK |= 0x00000010;
    (*OTG_FS).GINTSTS = 0x00002000;                 // clear ENUMDNE (write 1)
}

// GRXSTSP: BCNT bits 14:4, DPID bits 16:15, PKTSTS bits 20:17, FRMNUM bits 24:21
static void rxflvl_handler(void){
    uint32_t status = (*OTG_FS).GRXSTSP;
    uint32_t epnum  = (status & 0x0000000F) >> 0;    // endpoint number
    uint32_t bcnt   = (status & 0x00007FF0) >> 4;    // size of the packet
    uint32_t pktsts = (status & 0x001E0000) >> 17;   // packet status
    switch (pktsts){
    // OUT data arrived
    case 0x2: data_out_handler(epnum, bcnt); break;
    // OUT transfer complete: the packet is read out of the FIFO
    case 0x3:
        if (epnum == 0) ep0_out_arm();               // EP0: ready for the next packet / status stage
        if (epnum == 1) message_received = 1;        // EP1: message done, EP1 NAKs until usb_receive arms it again
        break;
    // SETUP data arrived
    case 0x6: setup[0] = *(OTG_FS_FIFO); setup[1] = *(OTG_FS_FIFO); break;
    // SETUP stage done
    case 0x4: setup_handler(); ep0_out_arm(); break;
    default: break;
    }
}

static void data_out_handler(uint32_t epnum, uint32_t bcnt){
    int length = (int)bcnt;
    if (epnum == 0){
        int i = 0;
        while (length > 0){
            ep0_receive_buf[i] = (*OTG_FS_FIFO1);
            ++i;
            length -= 4;
        }
        return;
    }
    if (epnum == 1){
        ep1_bcnt = bcnt;
        int i = 0;
        while (length > 0){
            uint32_t word = *OTG_FS_FIFO1;
            if (i < 16) cmd_buf[i] = word;       // never write past cmd_buf
            ++i;
            length -= 4;
        }
        return;
    }
}

/* ======================================================================
   EP0: control requests
   ====================================================================== */

// FIFO words are little endian, so byte 0 is bits 7:0 of setup[0]
// bmRequestType  setup[0] bits 7:0
// bRequest       setup[0] bits 15:8
// wValue         setup[0] bits 31:16
// wIndex         setup[1] bits 15:0
// wLength        setup[1] bits 31:16
static void setup_handler(void){
    uint8_t  bmRequestType = setup[0] & 0xFF;
    uint8_t  bRequest      = (setup[0] >> 8) & 0xFF;
    uint16_t wValue        = (setup[0] >> 16) & 0xFFFF;
    uint16_t wIndex        = setup[1] & 0xFFFF;
    uint16_t wLength       = (setup[1] >> 16) & 0xFFFF;
    uint8_t  recipient     = bmRequestType & 0x1F;   // 0 device, 1 interface, 2 endpoint

    switch ((bmRequestType >> 5) & 0x03){
        // standard requests (type 0)
        case 0:
        {
            switch (bRequest)
            {
            // GET_STATUS: 2 bytes back (Windows' power manager asks this)
            case 0:
            {
                uint8_t status[2] = {0x00, 0x00};
                if (recipient == 0){                                 // device
                    status[0] = 0x01;                                // bit 0 self-powered (matches cfg_desc 0xC0), bit 1 remote wakeup off
                } else if (recipient == 1){                          // interface
                    if (wIndex != 0){ ep0_stall(); break; }          // only interface 0 exists
                } else if (recipient == 2){                          // endpoint
                    if ((wIndex & 0x7F) != 0){                       // EP0 can't be halted, answers 0
                        volatile uint32_t *ctl = ep_ctl(wIndex);
                        if (!ctl){ ep0_stall(); break; }
                        if (*ctl & 0x00200000) status[0] = 0x01;     // bit 0 halted = STALL (bit 21) set
                    }
                } else { ep0_stall(); break; }
                ep0_send(status, min(2, wLength));
                break;
            }
            // CLEAR_FEATURE (1) / SET_FEATURE (3): only ENDPOINT_HALT (wValue 0) on EP1
            case 1:
            case 3:
            {
                volatile uint32_t *ctl = ep_ctl(wIndex);
                // recipient 0 would be device features (remote wakeup, test mode), not supported
                if (recipient != 2 || wValue != 0 || !ctl){ ep0_stall(); break; }
                if (bRequest == 3) *ctl |= 0x00200000;                             // SET: STALL (bit 21)
                else *ctl = (*ctl & ~0x00200000) | 0x10000000;                     // CLEAR: STALL off, SD0PID (bit 28): toggle back to DATA0
                ep0_send(0, 0);                                                    // status ZLP, after the STALL change
                break;
            }
            // SET_ADDRESS
            case 5:
            {
                (*OTG_FS).DCFG &= 0xFFFFF80F;            // clear DAD (bits 10:4)
                (*OTG_FS).DCFG |= (wValue) << 4;         // new address
                ep0_send(0, 0);                          // status ZLP
                break;
            }
            // GET_DESCRIPTOR
            // wValue high byte = descriptor type (1 device, 2 configuration, 3 string, 6 device qualifier, 15 BOS)
            // wValue low byte = index (which string, which configuration)
            case 6:
            {
                switch (wValue >> 8){
                    case 1:  ep0_send_data(dev_desc, 18, wLength); break;
                    case 2:  ep0_send_data(cfg_desc, 32, wLength); break;
                    case 15: ep0_send_data(bos_desc, 33, wLength); break;
                    default: ep0_stall(); break;            // 3 string, 6 device qualifier
                }
                break;
            }
            // GET_CONFIGURATION: 1 byte, current configuration value
            case 8:
            {
                uint8_t config = current_config;            // ep0_send wants a plain pointer
                ep0_send(&config, min(1, wLength));
                break;
            }
            // SET_CONFIGURATION: only configuration 1 exists
            case 9:
            {
                if (wValue == 1){
                    (*OTG_FS).IEP[1].DIEPCTL  = 0x18488040;   // EP1 IN: active, bulk, 64, TX FIFO 1, DATA0, NAK
                    (*OTG_FS).OEP[1].DOEPCTL  = 0x10088040;   // EP1 OUT: active, bulk, 64, DATA0, not armed
                    // only EP1 IN may interrupt (for TXFE); EP0 IN is polled, OUT is all handled through RXFLVL
                    (*OTG_FS).DAINTMSK        = 0x00000002;
                    current_config = 1;
                    ep0_send(0, 0);                           // status ZLP
                } else if (wValue == 0){
                    current_config = 0;                       // unconfigure
                    ep0_send(0, 0);
                } else {
                    ep0_stall();
                }
                break;
            }
            // GET_INTERFACE: 1 byte, current alternate setting
            case 10:
            {
                // interfaces only exist once configured, and only interface 0 exists
                if (!current_config || wIndex != 0){ ep0_stall(); break; }
                ep0_send(&alt_setting, min(1, wLength));
                break;
            }
            // SET_INTERFACE: only interface 0, alternate setting 0
            case 11:
            {
                if (!current_config || wIndex != 0 || wValue != 0){ ep0_stall(); break; }
                // the host resets its toggle on SET_INTERFACE; if we don't match,
                // our first packet looks like a duplicate and gets dropped
                (*OTG_FS).IEP[1].DIEPCTL |= 0x10000000;   // SD0PID: EP1 IN back to DATA0
                (*OTG_FS).OEP[1].DOEPCTL |= 0x10000000;   // SD0PID: EP1 OUT back to DATA0
                ep0_send(0, 0);
                break;
            }
            default: ep0_stall(); break;
            }
            break;
        }
        // vendor requests (type 2): Windows asks for the MS OS 2.0 set
        case 2:
        {
            if (bRequest == 1 && wIndex == 7) ep0_send_data(msos20_desc, 162, wLength);
            else ep0_stall();
            break;
        }
        // class requests and anything else: not supported
        default: ep0_stall(); break;
    }
}

static void ep0_out_arm(void){
    (*OTG_FS).OEP[0].DOEPTSIZ = 0x60080040;  // STUPCNT 3 (30:29), PKTCNT 1 (19), XFRSIZ 64
    (*OTG_FS).OEP[0].DOEPCTL |= 0x84000000;  // EPENA (31) | CNAK (26)
}

static void ep0_stall(void){
    // DIEPCTL0 STALL bit 21, the core clears it on the next SETUP
    (*OTG_FS).IEP[0].DIEPCTL |= 0x00200000;
}

static void ep0_in_arm(uint32_t size){
    (*OTG_FS).IEP[0].DIEPTSIZ = 0x00080000 | size;   // PKTCNT 1 (19), XFRSIZ size (6:0)
    (*OTG_FS).IEP[0].DIEPCTL |= 0x84000000;          // EPENA (31) | CNAK (26)
}

// one 64-byte packet per transfer, recurses for the rest, blocks on XFRC after each packet
static void ep0_send(const uint8_t *pointer, uint32_t len){
    (*OTG_FS).IEP[0].DIEPINT = 0x00000001;   // clear XFRC (write 1 to clear)
    ep0_in_arm(len > 64 ? 64 : len);
    int i = 64;
    if (len < 64) i = len;
    len = len - i;
    while (i > 0){
        uint32_t word = 0x00;
        word |= (uint32_t)*pointer;       ++pointer; --i; if (i <= 0) goto done;
        word |= (uint32_t)*pointer << 8;  ++pointer; --i; if (i <= 0) goto done;
        word |= (uint32_t)*pointer << 16; ++pointer; --i; if (i <= 0) goto done;
        word |= (uint32_t)*pointer << 24; ++pointer; --i; if (i <= 0) goto done;
        done: (*OTG_FS_FIFO) = word;
    }
    // wait for XFRC (bit 0), 50 ms timeout
    uint32_t t0 = *DWT_CYCCNT;
    while (((*OTG_FS).IEP[0].DIEPINT & 0x00000001) == 0){
        if ((*DWT_CYCCNT - t0) > 8400000){
            // the core turns the endpoint off after a transfer, so still enabled = stuck
            if ((*OTG_FS).IEP[0].DIEPCTL & 0x80000000){
                (*OTG_FS).IEP[0].DIEPCTL |= 0x40200000;              // EPDIS | STALL
                while (!((*OTG_FS).IEP[0].DIEPINT & 0x00000002)){}   // wait for EPDISD
                (*OTG_FS).IEP[0].DIEPINT = 0x00000002;               // clear EPDISD (write 1)
            } else (*OTG_FS).IEP[0].DIEPCTL |= 0x00200000;
            // flush TX FIFO 0
            (*OTG_FS).GRSTCTL = 0x00000020;
            while ((*OTG_FS).GRSTCTL & 0x00000020){}
            return;
        }
    }
    if (len > 0){
        ep0_send(pointer, len);
    }
    (*OTG_FS).IEP[0].DIEPINT = 0x00000001;   // clear XFRC (write 1 to clear)
}

// data stage of an IN request: never more than wLength,
// plus a ZLP if the data ended on a full packet and the host asked for more
static void ep0_send_data(const uint8_t *pointer, uint32_t size, uint16_t wLength){
    uint32_t len = min(size, wLength);
    ep0_send(pointer, len);
    if (len != 0 && len < wLength && (len & 0x3F) == 0) ep0_send(0, 0);
}

// wIndex of an endpoint request is the endpoint address: 0x81 EP1 IN, 0x01 EP1 OUT
// returns that endpoint's control register, 0 if it's not one we support halt on
static volatile uint32_t *ep_ctl(uint16_t wIndex){
    if (wIndex == 0x81) return &(*OTG_FS).IEP[1].DIEPCTL;
    if (wIndex == 0x01) return &(*OTG_FS).OEP[1].DOEPCTL;
    return 0;
}

/* ======================================================================
   EP1 IN
   ====================================================================== */

static void ep1_in_arm(uint32_t packets, uint32_t bytes){
    (*OTG_FS).IEP[1].DIEPTSIZ = ((packets & 0x3FF) << 19)    // PKTCNT, bits 28:19, max 1023
                              |  (bytes & 0x7FFFF);          // XFRSIZ, bits 18:0
    (*OTG_FS).IEP[1].DIEPCTL |= 0x84000000;                  // EPENA (31) | CNAK (26)
}

// called from the TXFE interrupt: one 64-byte packet at a time,
// as long as DTXFSTS1 (bits 15:0, free space in words) has room for 16 words
static void ep1_send(void){
    while (ep1_armed_length > 0 && ((*OTG_FS).IEP[1].DTXFSTS & 0x0000FFFF) >= 16){
        int i = 64; if (ep1_message_length < 64) i = ep1_message_length;
        ep1_message_length -= i;
        ep1_armed_length -= i;
        while (i > 0){
            uint32_t word = *ep1_message_pointer; ep1_message_pointer++;
            (*OTG_FS_FIFO1) = word;
            i -= 4;
        }
    }
}

// IN endpoint disable sequence from RM0090
static void ep1_abort(void){
    (*OTG_FS).GINTMSK    &= 0xFFFBFFFF;                      // IEPINT off
    (*OTG_FS).DIEPEMPMSK &= 0xFFFFFFFD;                      // TXFE off
    ep1_message_length = 0; ep1_armed_length = 0;
    if ((*OTG_FS).IEP[1].DIEPCTL & 0x80000000){              // endpoint still enabled
        (*OTG_FS).IEP[1].DIEPCTL |= 0x08000000;              // SNAK
        while (!((*OTG_FS).IEP[1].DIEPINT & 0x00000040)){}   // wait for INEPNE (bit 6): NAK effective
        (*OTG_FS).IEP[1].DIEPCTL |= 0x48000000;              // EPDIS | SNAK
        while (!((*OTG_FS).IEP[1].DIEPINT & 0x00000002)){}   // wait for EPDISD
        (*OTG_FS).IEP[1].DIEPINT = 0x00000002;               // clear EPDISD (write 1)
    }
    (*OTG_FS).GRSTCTL = 0x00000060;                          // flush TX FIFO 1: TXFFLSH (bit 5), TXFNUM 1 (bits 10:6)
    while ((*OTG_FS).GRSTCTL & 0x00000020){}
}
// USB setup packet: 8 bytes, little endian (low byte first)
// byte 0    bmRequestType  bit 7    direction (0 host->dev, 1 dev->host)
//                          bit 6:5  type (0 standard, 1 class, 2 vendor)
//                          bit 4:0  recipient (0 device, 1 interface, 2 endpoint)
// byte 1    bRequest       which request
// byte 3:2  wValue         parameter, depends on request
// byte 5:4  wIndex         parameter, often interface/endpoint number
// byte 7:6  wLength        max bytes in data stage (0 = no data stage)
//
// standard requests handled:
// 0 GET_STATUS, 1 CLEAR_FEATURE, 3 SET_FEATURE, 5 SET_ADDRESS, 6 GET_DESCRIPTOR,
// 8 GET_CONFIGURATION, 9 SET_CONFIGURATION, 10 GET_INTERFACE, 11 SET_INTERFACE
//
// stages: setup (8 bytes, always DATA0)
//      -> data (direction from bit 7, skipped if wLength = 0)
//      -> status (opposite direction, zero length packet)
// TX FIFO: write the bytes you want to send; RX FIFO: read what arrived
