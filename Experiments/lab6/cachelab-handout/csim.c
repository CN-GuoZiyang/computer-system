#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include "cachelab.h"

//#define DEBUG_ON 
//内存地址长度
#define ADDRESS_LENGTH 64

/* Type: Memory address */
//定义内存地址
typedef unsigned long long int mem_addr_t;

/* Type: Cache line
   LRU is a counter used to implement LRU replacement policy  */
//定义了一个缓存行
typedef struct cache_line {
    char valid; //有效位
    mem_addr_t tag; //标记位
    unsigned long long int lru; //缓存块，存有信息（时间）
} cache_line_t;

typedef cache_line_t* cache_set_t;  //定义一个组指针指向组的第一个行
typedef cache_set_t* cache_t;   //定义一个缓存指针指向第一个组

/* Globals set by command line args */
//命令行设置的参数
int verbosity = 0; /* print trace if set */ //显示轨迹信息
int s = 0; /* set index bits */     //设置内存中组索引的位数
int b = 0; /* block offset bits */  //内存块中地址的位数（块偏移位数）
int E = 0; /* associativity */  //每组中的缓存行数
char* trace_file = NULL;    //trace_file的char指针

/* Derived from command line args */
int S; /* number of sets 缓存中的组个数*/
int B; /* block size (bytes) 高速缓存块的字节数*/

/* Counters used to record cache statistics */
//用于记录cache的性能统计
int miss_count = 0;     //不命中次数
int hit_count = 0;      //命中次数
int eviction_count = 0; //驱逐次数
unsigned long long int lru_counter = 1; //一个记录最后一次访问时间的值，越小越早访问

/* The cache we are simulating */
//主缓存
cache_t cache;  
mem_addr_t set_index_mask;

/* 
 * initCache - Allocate memory, write 0's for valid and tag and LRU
 * also computes the set_index_mask
 * 初始化缓存，将缓存中的所有数据位置0，同时计算set_index_mask
 */
void initCache()
{
    cache = malloc(S * sizeof(cache_set_t));    //先分配cache的内存
    int i = 0;
    int j = 0;
    for(i = 0; i < S; i ++)
    {
        cache[i] = malloc(E * sizeof(cache_line_t));    //分配每个组的内存
        for(j = 0; j < E; j ++)
        {
            cache[i][j].valid = 'n';
            cache[i][j].tag = 0;
            cache[i][j].lru = 0;
        }
    }
}


/* 
 * freeCache - free allocated memory
 * 释放分配的内存
 */
void freeCache()
{
    int i = 0;
    for(i = 0; i < S; i ++)
    {
        free(cache[i]); //逐个组释放
    }
    free(cache);    //释放整个cache
}


/* 
 * accessData - Access data at memory address addr. 按照内存访问数据
 *   If it is already in cache, increast hit_count  如果已经在cache中了，hit_cache++
 *   If it is not in cache, bring it in cache, increase miss count. 如果不在cache中，把它放入cache中，miss_count++
 *   Also increase eviction_count if a line is evicted. 同时如果一个行被驱逐时，eviction_count++
 */
void accessData(mem_addr_t addr)
{
    unsigned long long int temp = addr; //用于位操作的临时保存
    unsigned long long int tempIndex;   //组索引
    tempIndex = temp << (ADDRESS_LENGTH - b - s);    //剩余组索引和块偏移
    tempIndex = tempIndex >> (ADDRESS_LENGTH - s);   //得到组索引
    unsigned long long int tempTag;    //标记
    tempTag = temp >> (s + b);
    int i = 0;
    int flag = 0;   //指示最终是否命中
    int hasInvalid = 0; //标记该组是否存在非有效位（空位）
    for(i = 0; i < E; i ++) //组已经确定，遍历该组的行
    {
        //有效且标记位相等
        if(cache[tempIndex][i].valid == 'y' && tempTag == cache[tempIndex][i].tag)
        {
            lru_counter++;  //时间++
            hit_count++;    //命中次数++
            cache[tempIndex][i].lru = lru_counter;  //重置块最后访问时间
            flag = 1;   //标记此次访问命中
            break;
        }
        if(cache[tempIndex][i].valid != 'y')
        {
            hasInvalid = 1;
        }
    }

    if(flag == 0)   //不命中的情况
    {
        miss_count++;   //不命中次数++
        if(hasInvalid == 1) //存在空位，直接替换空位
        {
            for(i = 0; i < E; i ++)
            {
                if(cache[tempIndex][i].valid != 'y')    //发现空块
                {
                    lru_counter++;  //时间++
                    cache[tempIndex][i].valid = 'y';    //该块有效
                    cache[tempIndex][i].lru = lru_counter;  //写入块最后访问时间
                    cache[tempIndex][i].tag = tempTag;  //写入块标记
                    break;
                }
            }
        }
        else    //无空块，执行LRU替换策略，驱逐最早被使用的块
        {
            eviction_count++;   //驱逐次数++
            unsigned long long int tempTime;    //临时的最早的时间
            tempTime = cache[tempIndex][0].lru;
            unsigned long long int replaceIndex = 0;    //被替换的块的索引
            for(i = 0; i < E; i ++) //寻找最早被使用的块
            {
                if(cache[tempIndex][i].lru < tempTime)
                {
                    tempTime = cache[tempIndex][i].lru;
                    replaceIndex = i;
                }
            }
            lru_counter++;  //时间++
            cache[tempIndex][replaceIndex].valid = 'y';
            cache[tempIndex][replaceIndex].lru = lru_counter;
            cache[tempIndex][replaceIndex].tag = tempTag;
        }
    }
}


/*
 * replayTrace - replays the given trace file against the cache 
 */
void replayTrace(char* trace_fn)
{
    char buf[1000];
    mem_addr_t addr=0;
    unsigned int len=0;
    FILE* trace_fp = fopen(trace_fn, "r");

    if(!trace_fp){
        fprintf(stderr, "%s: %s\n", trace_fn, strerror(errno));
        exit(1);
    }

    while( fgets(buf, 1000, trace_fp) != NULL) {
        if(buf[1]=='S' || buf[1]=='L' || buf[1]=='M') {
            sscanf(buf+3, "%llx,%u", &addr, &len);
      
            if(verbosity)
                printf("%c %llx,%u ", buf[1], addr, len);

            accessData(addr);

            /* If the instruction is R/W then access again */
            if(buf[1]=='M')
                accessData(addr);
            
            if (verbosity)
                printf("\n");
        }
    }

    fclose(trace_fp);
}

/*
 * printUsage - Print usage info
 */
void printUsage(char* argv[])
{
    printf("Usage: %s [-hv] -s <num> -E <num> -b <num> -t <file>\n", argv[0]);
    printf("Options:\n");
    printf("  -h         Print this help message.\n");
    printf("  -v         Optional verbose flag.\n");
    printf("  -s <num>   Number of set index bits.\n");
    printf("  -E <num>   Number of lines per set.\n");
    printf("  -b <num>   Number of block offset bits.\n");
    printf("  -t <file>  Trace file.\n");
    printf("\nExamples:\n");
    printf("  linux>  %s -s 4 -E 1 -b 4 -t traces/yi.trace\n", argv[0]);
    printf("  linux>  %s -v -s 8 -E 2 -b 4 -t traces/yi.trace\n", argv[0]);
    exit(0);
}

/*
 * main - Main routine 
 */
int main(int argc, char* argv[])
{
    char c;

    while( (c=getopt(argc,argv,"s:E:b:t:vh")) != -1){
        switch(c){
        case 's':
            s = atoi(optarg);
            break;
        case 'E':
            E = atoi(optarg);
            break;
        case 'b':
            b = atoi(optarg);
            break;
        case 't':
            trace_file = optarg;
            break;
        case 'v':
            verbosity = 1;
            break;
        case 'h':
            printUsage(argv);
            exit(0);
        default:
            printUsage(argv);
            exit(1);
        }
    }

    /* Make sure that all required command line args were specified */
    if (s == 0 || E == 0 || b == 0 || trace_file == NULL) {
        printf("%s: Missing required command line argument\n", argv[0]);
        printUsage(argv);
        exit(1);
    }

    /* Compute S, E and B from command line args */
    S = pow(2, s);
    B = pow(2, b);
 
    /* Initialize cache */
    initCache();

#ifdef DEBUG_ON
    printf("DEBUG: S:%u E:%u B:%u trace:%s\n", S, E, B, trace_file);
    printf("DEBUG: set_index_mask: %llu\n", set_index_mask);
#endif
 
    replayTrace(trace_file);

    /* Free allocated memory */
    freeCache();

    /* Output the hit and miss statistics for the autograder */
    printSummary(hit_count, miss_count, eviction_count);
    return 0;
}
