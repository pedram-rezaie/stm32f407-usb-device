#this is a script to compile things fast
# we are using a cortex m4 that runs only tumb

set -e
CFLAGS="-mcpu=cortex-m4 -mthumb -Wall -Wextra"
echo "Using CFLAGS: $CFLAGS"

echo "Compiling startup..."
arm-none-eabi-gcc $CFLAGS -c startup.c -o startup.o

echo "Compiling main..."
arm-none-eabi-gcc $CFLAGS -c main.c -o main.o

echo "Compiling usb..."
arm-none-eabi-gcc $CFLAGS -c usb.c -o usb.o

echo "Linking..."
arm-none-eabi-gcc $CFLAGS -nostdlib -T linker.ld usb.o startup.o main.o -o firmware.elf

echo "Build successful!"