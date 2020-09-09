#include <cpuid.h>
#include <fcntl.h>
#include <cstring>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
#include <iostream>
#include <cassert>

#include <emmintrin.h>
#include <mutex>
#include <immintrin.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <thread>
using namespace std;

#define _mm_clwb(addr)                                                        \
    asm volatile(".byte 0x66; xsaveopt %0" : "+m"(*(volatile char *)(addr)));

//long long a[1000000];
int main(int argc, char** argv) {
	srand(time(NULL));
	//int fd = open("../nvram/buffer.txt", "rw");
	char* file_name = "/mnt/pmem0/khb/buffer_10G.txt";
	int fd = open(file_name, O_RDWR);
	cout<<fd<<endl;
	long long total_len = 10ll << 30;
	long long *a = (long long*) mmap(0, total_len, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_SHARED, fd, 0);
	//char *a = new char[total_len + 8192];
    memset(a, 0, 64UL);
    _mm_clwb(a);
    _mm_sfence();
    a += 8;
    for (int i = 0; i < 8 * 1024 * 1024; i ++) {
        memset(a, 0, 64UL);
        _mm_clwb(a);
        _mm_sfence();
        // *(a - 8) = (long long) a;
        // _mm_clwb(a - 8);
        // _mm_sfence();
        // a += 8;
        // _mm_sfence();
        // a[0] = 10;
        _mm_clwb(a);
        _mm_sfence();
        
    }
	return 0;
}
