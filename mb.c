/*
ECE552 Lab 2 Microbenchmark
Description: This microbenchmarks runs a large loop of 
branches to test prediction accuracy on the 2-level predictor. 
The first loop should result in a high accuracy for the predictor,
as it has 6 history bits and the branch is periodic every
7 iterations, therefore the predictor can always hold the
complete history of the branch instruction once it reaches 
steady state. 
The 2nd loop should result in a low accuracy for the 2-level
predictor, as the branch is periodic every 8 iterations, and the
predictor cannot hold the complete history of the branch.
The loop iteration is always taken (until the end) so it
is always predictable be any of the predictors.

Assembly:
    1144:	8b 55 fc             	mov    -0x4(%rbp),%edx
    ... more modulo instructions
    116c:	85 c0                	test   %eax,%eax --> check result of modulo
    116e:	75 04                	jne    1174 <main+0x4b> --> Jump to 1174 inst. (take branch) if not zero
    1170:	83 45 f8 01          	addl   $0x1,-0x8(%rbp) --> val++ if branch not taken
    1174:	83 45 fc 01          	addl   $0x1,-0x4(%rbp) --> sum++ (jump target)
    1178:	83 45 f4 01          	addl   $0x1,-0xc(%rbp) --> i++
    117c:	81 7d f4 3f 42 0f 00 	cmpl   $0xf423f,-0xc(%rbp) --> check if i < 1000000
    1183:	7e bf                	jle    1144 <main+0x1b> --> loop branch

Resuts (1st loop):
    Number of conditional branches: 2018247
    Number of mispredicted branches: 1762
    Comparison: 2 bit sat mispredicts: 144709

Resuts (2nd loop):
    Number of conditional branches: 2018247
    Number of mispredicted branches: 126778
    Comparison: 2 bit sat mispredicts: 126856

We can see the results match our expectations, as the 2-level
predictor predicted most of the branches correctly in the first
loop and most incorrectly in the second loop. We expect 2 million
branches as there are 1 million for loop iterations and 1 branch
per loop. The for loop is easy to predict so that is why the
mispredictions are around 1 million for the 2nd loop
*/

// compile: gcc -O0 -static -no-pie -fno-pic mb.c -o mb
// dissassemble: objdump -d -l -S mb > mb.asm
// run /cad2/ece552f/pin/run_branchtrace /homes/w/<utorid>/ece552/cbp4-assign2/mb
// predictor branchtrace.gz

int main()
{
    int sum = 0;
    int val = 0;

    // Can be predicted in steady state (T T T T T T N)
    /*
    for(int i = 0; i < 1000000; i++)
    {
        if(sum % 7 == 0) val++;
        sum++;
    }
    */

    // Cannot be predicted in steady state (T T T T T T T N)
    
    for(int i = 0; i < 1000000; i++)
    {
        if(sum % 8 == 0) val++;
        sum++;
    }
    
}