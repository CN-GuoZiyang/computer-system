#include <stdio.h>

int main()
{
    int i = 0;
    while(1)
    {
        scanf("%d", &i);
        printf("%d\n", i & 0xF);
    }
    return 0;
}