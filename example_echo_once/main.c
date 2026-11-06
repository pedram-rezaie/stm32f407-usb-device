#include <stdint.h>
#include "usb.h"
static int min(int a, int b){ return a < b ? a : b; }

int main(){
uint32_t recieve_buf[25];
int max_lenght = 100;
uint32_t* pointerToBuffer = recieve_buf;
clock_set();
usb_enable();
int lenghtOfTheMessaeg=0;
while (max_lenght>0)
{
    //message is over either if we get a zlp or a <64 byte message
    int n = usb_receive(pointerToBuffer,min(64,max_lenght));
    lenghtOfTheMessaeg += n;
    max_lenght -=n; if(max_lenght <= 0) break;
    if(n ==0 || n<64) break;
    pointerToBuffer+=16;
}
if(lenghtOfTheMessaeg>100) lenghtOfTheMessaeg=100; // pls dont buffer overflow
usb_data_transfer(recieve_buf,lenghtOfTheMessaeg);

   while(1){}
}