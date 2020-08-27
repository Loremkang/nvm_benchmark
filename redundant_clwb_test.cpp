#include <fcntl.h>
#include <cstring>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
#include <iostream>
#include <cassert>
#include <cpuid.h>
#include <emmintrin.h>
#include <mutex>
#include <immintrin.h>
using namespace std;

#define FLUSH_ALIGN ((uintptr_t)64)

#ifdef _MSC_VER
#define pmem_clflushopt _mm_clflushopt
#define pmem_clwb _mm_clwb
#else
/*
 * The x86 memory instructions are new enough that the compiler
 * intrinsic functions are not always available.  The intrinsic
 * functions are defined here in terms of asm statements for now.
 */
#define pmem_clflushopt(addr)                                                  \
    asm volatile(".byte 0x66; clflush %0" : "+m"(*(volatile char *)(addr)));
#define pmem_clwb(addr)                                                        \
    asm volatile(".byte 0x66; xsaveopt %0" : "+m"(*(volatile char *)(addr)));
#endif /* _MSC_VER */

typedef void (*FlushFunc)(const void *, size_t);

static FlushFunc flush_func;
static bool flush_func_initialized = false;
static std::mutex mtx;

#define EAX_IDX 0
#define EBX_IDX 1
#define ECX_IDX 2
#define EDX_IDX 3

#ifndef bit_CLFLUSH
#define bit_CLFLUSH (1 << 19)
#endif

#ifndef bit_CLFLUSHOPT
#define bit_CLFLUSHOPT (1 << 23)
#endif

#ifndef bit_CLWB
#define bit_CLWB (1 << 24)
#endif


static void flush_clflush(const void *addr, size_t len) {
    uintptr_t uptr;

    /*
     * Loop through cache-line-size (typically 64B) aligned chunks
     * covering the given range.
     */
    for (uptr = (uintptr_t)addr & ~(FLUSH_ALIGN - 1);
         uptr < (uintptr_t)addr + len; uptr += FLUSH_ALIGN)
        _mm_clflush((char *)uptr);
}

static void flush_clflushopt(const void *addr, size_t len) {
    uintptr_t uptr;

    /*
     * Loop through cache-line-size (typically 64B) aligned chunks
     * covering the given range.
     */
    for (uptr = (uintptr_t)addr & ~(FLUSH_ALIGN - 1);
         uptr < (uintptr_t)addr + len; uptr += FLUSH_ALIGN) {
        pmem_clflushopt((char *)uptr);
    }
}

static inline void flush_clwb(const void *addr, size_t len) {
    uintptr_t uptr;

    /*
     * Loop through cache-line-size (typically 64B) aligned chunks
     * covering the given range.
     */
	#pragma omp parallel for num_threads(72)
    for (uptr = (uintptr_t)addr & ~(FLUSH_ALIGN - 1);
         uptr < (uintptr_t)addr + len; uptr += FLUSH_ALIGN) {
        pmem_clwb((char *)uptr);
    }
}


static inline void cpuid(unsigned func, unsigned subfunc, unsigned cpuinfo[4]) {
    __cpuid_count(func, subfunc, cpuinfo[EAX_IDX], cpuinfo[EBX_IDX],
                  cpuinfo[ECX_IDX], cpuinfo[EDX_IDX]);
}

/*
 * is_cpu_feature_present -- (internal) checks if CPU feature is supported
 */
static int is_cpu_feature_present(unsigned func, unsigned reg, unsigned bit) {
    unsigned cpuinfo[4] = {0};

    /* check CPUID level first */
    cpuid(0x0, 0x0, cpuinfo);
    if (cpuinfo[EAX_IDX] < func)
        return 0;

    cpuid(func, 0x0, cpuinfo);
    return (cpuinfo[reg] & bit) != 0;
}

/*
 * is_cpu_clflush_present -- checks if CLFLUSH instruction is supported
 */
int is_cpu_clflush_present() {
    return is_cpu_feature_present(0x1, EDX_IDX, bit_CLFLUSH);
}

/*
 * is_cpu_clflushopt_present -- checks if CLFLUSHOPT instruction is supported
 */
int is_cpu_clflushopt_present() {
    return is_cpu_feature_present(0x7, EBX_IDX, bit_CLFLUSHOPT);
}

/*
 * is_cpu_clwb_present -- checks if CLWB instruction is supported
 */
int is_cpu_clwb_present() {
    return is_cpu_feature_present(0x7, EBX_IDX, bit_CLWB);
}

void init_func() {
    std::lock_guard<std::mutex> lock_guard(mtx);

    if (is_cpu_clwb_present()) {
        flush_func = flush_clwb;
		cout<<"CLWB"<<endl;
    } else if (is_cpu_clflushopt_present()) {
        flush_func = flush_clflushopt;
		cout<<"CLF_OPT"<<endl;
    } else if (is_cpu_clflush_present()) {
        flush_func = flush_clflush;
		cout<<"CLF"<<endl;
    } else {
        assert(0);
    }

    flush_func_initialized = true;
    _mm_mfence();
}

inline void pmem_persist(const void *addr, size_t len) {
    if (!flush_func_initialized) {
        init_func();
    }

    flush_func(addr, len);

    //_mm_mfence();
}

inline void printAddr(long long addr) {
	printf("0x%016llo\n", addr);
	cout<<endl;
}

inline void flush_cl(long long addr, long long offset) {
	//cout<<addr<<' '<<offset<<endl;
	//printAddr((((addr << 20) | offset) << 6));
	flush_func((const void*)(((addr << 20) | offset) << 6), 8);
	_mm_mfence();
}

long long addrCache[(1<<20) * (1<<2)];
int rolling[1<<20];
inline void try_persist(const void *addr, size_t len) {
	long long addrr = ((long long)addr) >> 6;
	int offset = (addrr & ((1 << 20) - 1)) << 2;
	//printAddr((long long)addr);
	//printAddr(offset);
	addrr = addrr >> 20;
	//printAddr(addrr);
	if (addrCache[offset] == addrr || addrCache[offset + 1] == addrr || addrCache[offset + 2] == addrr || addrCache[offset + 3] == addrr) {
		return;
	}
	int& roll = rolling[offset >> 2];
	if (addrCache[offset + roll] != -1) {
		flush_cl(addrCache[offset + roll], offset >> 2);
	}
	addrCache[offset + roll] = addrr;
	roll = (roll + 1) & 3;
}

//long long a[1000000];
int main() {
	srand(time(NULL));
	//int fd = open("../nvram/buffer.txt", "rw");
	char* file_name = "/mnt/pmem0/khb/buffer_10G.txt";
	int fd = open(file_name, O_RDWR);
	long long* a = (long long*) mmap(0, 10ll<<30, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_ANON|MAP_PRIVATE, fd, 0);
	//long long *a = new long long[100000000];
	printAddr((long long)a);
	int T = 1000;
	init_func();
	while (T--) {
		//memset(addrCache, -1, sizeof(addrCache));
		int N = 1e6;
		int K = 32;
		/*
		#pragma omp parallel for num_threads(64)
		for (int i = 0; i < N; i ++) {
			//int k = (i & 7) + ((i >> 3) << 6);
			for (int j = 0; j < K; j ++) {
				//int p = k + (j << 3);
				int p;
				p = i * K + j;
				a[p] = a[p] + i;
			}
		}*/
		flush_func(a, 8*N);
		_mm_mfence();
		for (int i = 0; i < N; i ++) {
			//int k = (i & 7) + ((i >> 3) << 6);
			//flush_func(&a[i*8], 8);
			/*if (((i & 0xffffffff) == 0)) {
				_mm_mfence();
			}
			for (int j = 0; j < 8; j ++) {
				int p = k + (j << 3);
				p = i * 8 + j;
				//flush_func(&a[p], 8);
				//try_persist(&a[p], 8);
				//_mm_mfence();
				//pmem_persist(&a[j], 8);
			}*/
			//_mm_mfence();
		}
		/*for (int offset = 0; offset < (1 << 20); offset ++) {
			for (int j = 0; j < 4; j ++) {
				flush_cl(addrCache[(offset << 2) + j], offset);
			}
		}*/
		if ((T & 31) == 0) 
			cout<<a[rand() % (N * 8)]<<endl;
	}
	return 0;
}
