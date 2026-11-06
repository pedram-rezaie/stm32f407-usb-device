#include <stdint.h>
#include "usb.h"

// from linker.ld
extern uint32_t _estack;                      // top of the stack
extern uint32_t _sidata;                      // .data initial values in flash (LMA)
extern uint32_t _sdata, _edata;               // .data in RAM (VMA)
extern uint32_t _sbss, _ebss;                 // .bss in RAM

int main(void);
void reset_handler(void);
void default_handler(void);

// vector table: 16 core exceptions + IRQs up to OTG_FS (IRQ 67 = entry 16 + 67 = 83)
__attribute__((section(".isr_vector"), used))
void (* const vector_table[84])(void) = {
    [0]         = (void (*)(void))&_estack,   // initial stack pointer, not a function
    [1]         = reset_handler,
    [2 ... 82]  = default_handler,            // NMI, faults, SysTick, unused IRQs
    [83]        = otg_fs_handler,             // IRQ 67, OTG_FS
};

void reset_handler(void){
    // copy .data initial values from flash to RAM
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    // zero .bss
    dst = &_sbss;
    while (dst < &_ebss) *dst++ = 0;
    main();
    while (1){}
}

// any interrupt or fault without its own handler ends up here and stops,
// so a debugger shows where it went wrong
void default_handler(void){
    while (1){}
}
