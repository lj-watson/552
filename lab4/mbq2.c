
#include <stdlib.h>
#include <time.h>

// stride prefetch microbenchmark
// To compile: /cad2/ece552f/compiler/bin/ssbig-na-sstrix-gcc mbq2.c -O0 -o mbq2
// To run: ./sim-cache -config q2.cfg mbq2

static volatile int array[1000000];
int main(void) {
    register int sum = 0;
    register int i = 0;
    register int j = 0;

    // Array with a constant stride of 8:
    // Can change the constant stride to any other value, should perform just as well
    for(; i < 1000000; i+=64) {
        sum += array[i];
    }

    // Test with random stride, should perform worse
    // Prefetches should stop due to state changes:
    // srand(time(NULL));
    // for (i = 0; i < 1000000; i += rand() % 10000) {
    //     sum += array[i*i];
    // }
    
    return 0;
}


