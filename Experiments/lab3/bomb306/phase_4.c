#include <stdio.h>

/**
 * 初始参数
 * x:$edx --- 14
 * y:$esi --- 0
 * z:$edi --- 12(第一个参数)
**/ 

int func4(int edx, int esi, int edi)
{
    int eax = edx;  //3
    int tmp = 0;
    eax = eax - esi;    //4
    tmp = eax;
    tmp = (unsigned)tmp >> 31;
    eax = eax + tmp;
    eax = eax / 2;
    tmp = eax + esi;    //9
    if(tmp > edi){      //10, 11
        edx = tmp - 1;
        return (2 * func4(edx, esi, edi));
    }
    eax = 0;
    if(tmp < edi){      //13, 14
        esi = tmp + 1;
        return (1 + 2 * func4(edx, esi, edi));
    }
    //esp = esp + 8;
    return eax;
}

int main()
{
    int tmp;
    int i = 0;
    for(i = 0; i < 15; i ++){
        if(func4(14, 0, i) == 2){
            printf("%d\n", i);
        }else{
            printf("%d:false\n", i);
        }
    }
    
    return 0;
}