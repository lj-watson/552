// Open-ended prefetcher microbenchmark
// To compile: /cad2/ece552f/compiler/bin/ssbig-na-sstrix-gcc mbq6.c -O0 -o mbq6
// To run: ./sim-cache -config q6.cfg mbq6

#include <stdlib.h>

static volatile int array[1000000];

int main(void) {
    register int sum = 0;
    register int i = 0;
    register int j = 0;
    register int k = 0;

    /* Test with several different patterns and strides: */
    for (i = 0, j = 1; i < 50000; i++) {
        if (i*64 >= 1000000 || j*25 >= 1000000) {
            break;
        }
    
        sum += array[i*8];
        sum += array[j*25];
        sum += array[i*64];
        j+=2;
    }

    for (i = 0, j = 1; i < 100000; i++) {
        if (k*5 >= 1000000) {
            break;
        }
    
        sum += array[k*5];
        k+=5;
    }

    
    return 0;
}

