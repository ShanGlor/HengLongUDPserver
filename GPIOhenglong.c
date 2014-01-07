

// I have stolen this from ianrenton, ThanX!

//
// Raspberry Tank SSH Remote Control script
// Ian Renton, June 2012
// http://ianrenton.com
//
// Based on the GPIO example by Dom and Gert
// (http://elinux.org/Rpi_Low-level_peripherals#GPIO_Driving_Example_.28C.29)
// Using Heng Long op codes discovered by ancvi_pIII
// (http://www.rctanksaustralia.com/forum/viewtopic.php?p=1314#p1314)
//

#define BCM2708_PERI_BASE        0x20000000
#define GPIO_BASE                (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "GPIOhenglong.h"

#define PAGE_SIZE (4*1024)
#define BLOCK_SIZE (4*1024)

int  mem_fd;
char *gpio_mem, *gpio_map;
char *spi0_mem, *spi0_map;

// I/O access
volatile unsigned *gpio;

// GPIO setup macros. Always use INP_GPIO(x) before using OUT_GPIO(x) or SET_GPIO_ALT(x,y)
#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpio+(((g)/10))) |= (((a)<=3?(a)+4:(a)==4?3:2)<<(((g)%10)*3))

// N.B. These have been reversed compared to Gert & Dom's original code!
// This is because the transistor board I use for talking to the Heng
// Long RX18 inverts the signal.  So the GPIO_SET pointer here actually
// sets the GPIO pin low - but that ends up as a high at the tank.
#define GPIO_SET *(gpio+7)  // sets   bits which are 1, ignores bits which are 0
#define GPIO_CLR *(gpio+10) // clears bits which are 1, ignores bits which are 0

// GPIO pin that connects to the Heng Long main board
// (Pin 7 is the top right pin on the Pi's GPIO, next to the yellow video-out)
#define PIN 8






// Sends one individual code to the main tank controller
void sendCode(int code) {
  // Send header "bit" (not a valid Manchester code)
  GPIO_SET = 1<<PIN;
  usleep(500);

  // Send the code itself, bit by bit using Manchester coding
  int i;
  for (i=0; i<32; i++) {
    int bit = (code>>(31-i)) & 0x1;
    sendBit(bit);
  }

  // Force a 4ms gap between messages
  GPIO_CLR = 1<<PIN;
  usleep(3333);
} // sendCode


// Sends one individual bit using Manchester coding
// 1 = high-low, 0 = low-high
void sendBit(int bit) {
  //printf("%d", bit);

  if (bit == 1) {
    GPIO_SET = 1<<PIN;
    usleep(250);
    GPIO_CLR = 1<<PIN;
    usleep(250);
  } else {
    GPIO_CLR = 1<<PIN;
    usleep(250);
    GPIO_SET = 1<<PIN;
    usleep(250);
  }
} // sendBit


// Set up a memory region to access GPIO
void setup_io() {

    /* open /dev/mem */
    if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
      printf("can't open /dev/mem \n");
      exit (-1);
    }

    /* mmap GPIO */

    // Allocate MAP block
    if ((gpio_mem = malloc(BLOCK_SIZE + (PAGE_SIZE-1))) == NULL) {
      printf("allocation error \n");
      exit (-1);
    }

    // Make sure pointer is on 4K boundary
    if ((unsigned long)gpio_mem % PAGE_SIZE)
     gpio_mem += PAGE_SIZE - ((unsigned long)gpio_mem % PAGE_SIZE);

    // Now map it
    gpio_map = (unsigned char *)mmap(
      (caddr_t)gpio_mem,
      BLOCK_SIZE,
      PROT_READ|PROT_WRITE,
      MAP_SHARED|MAP_FIXED,
      mem_fd,
      GPIO_BASE
    );

    if ((long)gpio_map < 0) {
      printf("mmap error %d\n", (int)gpio_map);
      exit (-1);
    }

    // Always use volatile pointer!
    gpio = (volatile unsigned *)gpio_map;

     // Switch the relevant GPIO pin to output mode
    INP_GPIO(PIN); // must use INP_GPIO before we can use OUT_GPIO
    OUT_GPIO(PIN);

    GPIO_CLR = 1<<PIN;

} // setup_io
