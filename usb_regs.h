#ifndef USB_REGS_H
#define USB_REGS_H
#include <stdint.h>

// Register map used by usb.c only. Users never include this file,
// so RCC, GPIOA, NVIC... can't clash with CMSIS or their own headers.

/* ---------- Flash ---------- */
#define FLASH_ACR  ((volatile uint32_t *)0x40023C00)   // Flash Access Control Register (wait states), used by clock_set

/* ---------- Cortex-M4 cycle counter, used for every delay and timeout ---------- */
#define DEMCR      ((volatile uint32_t *)0xE000EDFC)   // Debug Exception and Monitor Control Register
#define DWT_CTRL   ((volatile uint32_t *)0xE0001000)   // Data Watchpoint and Trace ConTRoL
#define DWT_CYCCNT ((volatile uint32_t *)0xE0001004)   // CYCle CouNT, +1 per CPU clock

/* ---------- GPIOA ---------- */
typedef struct GPIO_PORTA
{
    volatile uint32_t MODER;   // reset 0xA8000000
    volatile uint32_t OTYPER;  // reset 0x00000000
    volatile uint32_t OSPEEDR; // reset 0x0C000000
    volatile uint32_t PUPDR;   // reset 0x64000000
    volatile uint32_t IDR;
    volatile uint32_t ODR;     // reset 0x00000000
    volatile uint32_t BSRR;    // reset 0x00000000
    volatile uint32_t LCKR;    // reset 0x00000000
    volatile uint32_t AFRL;    // reset 0x00000000
    volatile uint32_t AFRH;    // reset 0x00000000
} GPIOA_PORTA;
#define GPIOA ((GPIOA_PORTA*)0x40020000)

/* ---------- RCC ---------- */
typedef struct rcc_registers
{
    volatile uint32_t CR;
    volatile uint32_t PLLCFGR;
    volatile uint32_t CFGR;
    volatile uint32_t CIR;
    volatile uint32_t AHB1RSTR;
    volatile uint32_t AHB2RSTR;
    volatile uint32_t AHB3RSTR;
    volatile uint32_t skip0[1];
    volatile uint32_t APB1RSTR;
    volatile uint32_t APB2RSTR;
    volatile uint32_t skip[2];
    volatile uint32_t AHB1ENR;
    volatile uint32_t AHB2ENR;
    volatile uint32_t AHB3ENR;
    volatile uint32_t skip2[1];
    volatile uint32_t APB1ENR;
    volatile uint32_t APB2ENR;
    volatile uint32_t skip3[2];
    volatile uint32_t AHB1LPENR;
    volatile uint32_t AHB2LPENR;
    volatile uint32_t AHB3LPENR;
    volatile uint32_t skip4[1];
    volatile uint32_t APB1LPENR;
    volatile uint32_t APB2LPENR;
    volatile uint32_t skip5[2];
    volatile uint32_t BDCR;
    volatile uint32_t CSR;
    volatile uint32_t skip6[2];
    volatile uint32_t SSCGR;
    volatile uint32_t PLLI2SCFGR;
    volatile uint32_t skip7[1];
    volatile uint32_t DCKCFGR;
} rcc_registers;
#define RCC ((rcc_registers*)0x40023800)

/* ---------- NVIC ---------- */
typedef struct nvic_registers
{
    volatile uint32_t ISER[8];        // 0x000 set-enable
    uint32_t RESERVED0[24];
    volatile uint32_t ICER[8];        // 0x080 clear-enable
    uint32_t RESERVED1[24];
    volatile uint32_t ISPR[8];        // 0x100 set-pending
    uint32_t RESERVED2[24];
    volatile uint32_t ICPR[8];        // 0x180 clear-pending
    uint32_t RESERVED3[24];
    volatile uint32_t IABR[8];        // 0x200 active bit
    uint32_t RESERVED4[56];
    volatile uint8_t  IPR[240];       // 0x300 priority, one byte per IRQ
    uint32_t RESERVED5[644];
    volatile uint32_t STIR;           // 0xE00 software trigger
} nvic_registers;
#define NVIC ((nvic_registers*)0xE000E100)

/* ---------- USB OTG_FS (RM0090 register map) ---------- */
typedef struct
{
    volatile uint32_t HCCHAR;     // +0x00
    volatile uint32_t RES0;       // +0x04
    volatile uint32_t HCINT;      // +0x08
    volatile uint32_t HCINTMSK;   // +0x0C
    volatile uint32_t HCTSIZ;     // +0x10
    volatile uint32_t RES1[3];    // +0x14
} OTG_FS_HC;                      // 0x20 per host channel

typedef struct
{
    volatile uint32_t DIEPCTL;    // +0x00
    volatile uint32_t RES0;       // +0x04
    volatile uint32_t DIEPINT;    // +0x08
    volatile uint32_t RES1;       // +0x0C
    volatile uint32_t DIEPTSIZ;   // +0x10
    volatile uint32_t RES2;       // +0x14
    volatile uint32_t DTXFSTS;    // +0x18
    volatile uint32_t RES3;       // +0x1C
} OTG_FS_INEP;                    // 0x20 per IN endpoint

typedef struct
{
    volatile uint32_t DOEPCTL;    // +0x00
    volatile uint32_t RES0;       // +0x04
    volatile uint32_t DOEPINT;    // +0x08
    volatile uint32_t RES1;       // +0x0C
    volatile uint32_t DOEPTSIZ;   // +0x10
    volatile uint32_t RES2[3];    // +0x14
} OTG_FS_OUTEP;                   // 0x20 per OUT endpoint

typedef struct OTG_FS_REGS
{
    /* Core global, 0x000 */
    volatile uint32_t GOTGCTL;     // 0x000
    volatile uint32_t GOTGINT;     // 0x004
    volatile uint32_t GAHBCFG;     // 0x008
    volatile uint32_t GUSBCFG;     // 0x00C
    volatile uint32_t GRSTCTL;     // 0x010
    volatile uint32_t GINTSTS;     // 0x014
    volatile uint32_t GINTMSK;     // 0x018
    volatile uint32_t GRXSTSR;     // 0x01C  read only
    volatile uint32_t GRXSTSP;     // 0x020  read + pop
    volatile uint32_t GRXFSIZ;     // 0x024
    volatile uint32_t DIEPTXF0;    // 0x028  HNPTXFSIZ in host mode
    volatile uint32_t HNPTXSTS;    // 0x02C
    volatile uint32_t RES0[2];     // 0x030
    volatile uint32_t GCCFG;       // 0x038
    volatile uint32_t CID;         // 0x03C
    volatile uint32_t RES1[48];    // 0x040
    volatile uint32_t HPTXFSIZ;    // 0x100
    volatile uint32_t DIEPTXF[3];  // 0x104  DIEPTXF1..3
    volatile uint32_t RES2[188];   // 0x110

    /* Host, 0x400 */
    volatile uint32_t HCFG;        // 0x400
    volatile uint32_t HFIR;        // 0x404
    volatile uint32_t HFNUM;       // 0x408
    volatile uint32_t RES3;        // 0x40C
    volatile uint32_t HPTXSTS;     // 0x410
    volatile uint32_t HAINT;       // 0x414
    volatile uint32_t HAINTMSK;    // 0x418
    volatile uint32_t RES4[9];     // 0x41C
    volatile uint32_t HPRT;        // 0x440
    volatile uint32_t RES5[47];    // 0x444
    OTG_FS_HC         HC[8];       // 0x500  ch n at 0x500 + 0x20*n
    volatile uint32_t RES6[128];   // 0x600

    /* Device, 0x800 */
    volatile uint32_t DCFG;        // 0x800
    volatile uint32_t DCTL;        // 0x804
    volatile uint32_t DSTS;        // 0x808
    volatile uint32_t RES7;        // 0x80C
    volatile uint32_t DIEPMSK;     // 0x810
    volatile uint32_t DOEPMSK;     // 0x814
    volatile uint32_t DAINT;       // 0x818
    volatile uint32_t DAINTMSK;    // 0x81C
    volatile uint32_t RES8[2];     // 0x820
    volatile uint32_t DVBUSDIS;    // 0x828
    volatile uint32_t DVBUSPULSE;  // 0x82C
    volatile uint32_t RES9;        // 0x830
    volatile uint32_t DIEPEMPMSK;  // 0x834
    volatile uint32_t RES10[50];   // 0x838
    OTG_FS_INEP       IEP[4];      // 0x900  IN ep n at 0x900 + 0x20*n
    volatile uint32_t RES11[96];   // 0x980
    OTG_FS_OUTEP      OEP[4];      // 0xB00  OUT ep n at 0xB00 + 0x20*n
    volatile uint32_t RES12[160];  // 0xB80

    /* Power and clock gating, 0xE00 */
    volatile uint32_t PCGCCTL;     // 0xE00
} OTG_FS_REGS;

#define OTG_FS       ((OTG_FS_REGS*)0x50000000)
#define OTG_FS_FIFO  ((volatile uint32_t *)0x50001000)   // EP0 FIFO
#define OTG_FS_FIFO1 ((volatile uint32_t *)0x50002000)   // EP1 FIFO
// reading any FIFO address pops the one shared RX FIFO

#endif
