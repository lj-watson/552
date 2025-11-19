// next line prefetch microbenchmark
// To compile: /cad2/ece552f/compiler/bin/ssbig-na-sstrix-gcc mbq1.c -O0 -o mb
// To run: ./sim-cache -config q1.cfg mb

static volatile double array[1000000];
int main(void)
{
    double sum = 0;
    register int i = 0;
    // change to i+=2 for 2nd test
    for(; i < 1000000; i++)
    {
        sum += array[i];
    }
}