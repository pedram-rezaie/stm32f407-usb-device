# builds the example together with the driver from the repo root
# Cortex-M4, Thumb only

set -e
CFLAGS="-mcpu=cortex-m4 -mthumb -Wall -Wextra -I.."
echo "Using CFLAGS: $CFLAGS"

echo "Compiling startup..."
arm-none-eabi-gcc $CFLAGS -c startup.c -o startup.o

echo "Compiling main..."
arm-none-eabi-gcc $CFLAGS -c main.c -o main.o

echo "Compiling usb..."
arm-none-eabi-gcc $CFLAGS -c ../usb.c -o usb.o

echo "Linking..."
arm-none-eabi-gcc $CFLAGS -nostdlib -T linker.ld usb.o startup.o main.o -o firmware.elf

echo "Build successful!"
