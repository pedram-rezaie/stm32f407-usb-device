#include <stdint.h>
#include "usb.h"

// echo example: receives a message of up to BUFFER_SIZE bytes and sends it back, forever
// a message is over when a packet shorter than 64 bytes arrives (a ZLP counts too),
// the same rule the host uses on the bus

#define BUFFER_SIZE 100

static int min(int a, int b){ return a < b ? a : b; }

int main(void){
    uint32_t receive_buf[BUFFER_SIZE / 4];          // word aligned, usb_data_transfer needs that
    clock_set();
    usb_enable();
    while (1){
        uint32_t *pointer = receive_buf;
        int space = BUFFER_SIZE;                    // bytes still free in the buffer
        int message_length = 0;
        while (space > 0){
            int asked = min(64, space);
            int n = usb_receive(pointer, asked);    // bytes the host sent in this packet
            message_length += min(n, asked);        // bytes that actually landed in the buffer
            space -= min(n, asked);
            if (n < 64) break;                      // short packet or ZLP: message over
            pointer += 16;                          // next 64 bytes (16 words)
        }
        // a message longer than the buffer is cut here, the rest arrives as the next message
        usb_data_transfer(receive_buf, message_length);
    }
}
