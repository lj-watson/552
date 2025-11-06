#define ITERATIONS 1000000

// To compile: /cad2/ece552f/compiler/bin/ssbig-na-sstrix-gcc mb.c -O0 -o mb
// To run: sim-safe -max:inst 1000000 mb

int main (int argc, char *argv[]) {
    register int i = 0;
    register int r1 = 0;
    register int r2 = 0;
    register int r3 = 0;

    register float r4 = 0.0;
    register float r5 = 0.0;

    int val = 1;
    // Pointer is used for load instructions
    register int *ld = &val;

    for (; i < ITERATIONS; ) {
        i++;
        r1 = *ld;
        r2 = r1 + 1;
        *ld = r1 + r3;

        r4 = r5 + 1.2;
        r5 = r4 + 1.2;
    }
}
