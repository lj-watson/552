// microbenchmark for 2 bit sat predictor

int main()
{
    int a = 0;
    int b = 0;

    /*
    
    // no change
    for(int i = 0; i < 1000000; i++)
    {
        if(1)
        {
            a++;
        }
        if(0)
        {
            b++;
        }
    }

    */

    // rare change, 2 bit sat filters out
    for(int i = 0; i < 1000000; i++)
    {
        if(i % 10 == 0)
        {
            a++;
        }
    }
    
    /*

    // oscillating, not predictable
    for(int i = 0; i < 1000000; i++)
    {
        if(i % 2 == 0)
        {
            a++;
        }
    }

    */

}