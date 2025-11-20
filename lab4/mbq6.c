// Open-ended prefetcher microbenchmark
// To compile: /cad2/ece552f/compiler/bin/ssbig-na-sstrix-gcc mbq6.c -O0 -o mbq6
// To run: ./sim-cache -config q6.cfg mbq6

#include <stdlib.h>

static volatile int array[1000000];

int main(void) {
    register int sum = 0;
    register int i = 0;
    register int j = 0;
    register int k = 1;

    /* Test with several different patterns and strides: */
    // for (i = 0, j = 1; i < 50000; i++) {
    //     if (i*64 >= 1000000 || j*25 >= 1000000) {
    //         break;
    //     }
    
    //     sum += array[i*8];
    //     sum += array[j*25];
    //     sum += array[i*64];
    //     j+=2;
    // }

    // for (k = 1; k < 1000000; k = (k + 8) * 2) {
    //     sum += array[k];
    // }

    /* Test with several different patterns and strides determined by the modulo of i: */
    for(; i < 1000000;) {
        if (i%4 == 0) {
            i += 64;
        } else if (i%4 == 1) {
            i += 25;
        } else if (i%4 == 2) {
            i += 99;
        } else if (i%4 == 3) {
            i += 128;
        } 

        sum += array[i];
    }

    
    return 0;
}

