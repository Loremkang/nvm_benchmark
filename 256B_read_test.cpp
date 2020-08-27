#include <fcntl.h>
#include <cstring>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
#include <iostream>
#include <cassert>
#include <cpuid.h>
#include <mutex>
#include <emmintrin.h>
#include <immintrin.h>
#include <xmmintrin.h>
#include <thread>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
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

long long *a;
long long *aa;

inline void work(long long *a, long long thread_id, long long l, long long r) {
	long long t = 0;
	for (long long i = l; i < r; i += 32) {
		//t += a[i];
		__m64* mem_addr = (__m64*)&a[i];
		__m64* tmpval = (__m64*)&i;
		_mm_stream_pi (mem_addr, *tmpval);
	}
	cout<<l<<' '<<r<<' '<<t<<endl;
}

const int CNT = 16;

inline void threadtest(long long length) {
	thread thr[CNT];
	for (int i = 0; i < CNT; i ++) {
		cout<<"Thread ID: "<<i<<" Length: "<<length<<endl;
		long long size = length / CNT;
		long long l = size * i;
		long long r = size * (i + 1);
		thr[i] = thread(work, a, i, l, r);
	}
	for (int i = 0; i < CNT; i ++) {
		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET(i * 4, &cpuset);
		int rc = pthread_setaffinity_np(thr[i].native_handle(),
										sizeof(cpu_set_t), &cpuset);
		if (rc != 0) {
		std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
		}
	}
	for (int i = 0; i < CNT; i ++) {
		thr[i].join();
	}
}

//long long a[1000000];
int main() {
	srand(time(NULL));
	//int fd = open("../nvram/buffer.txt", "rw");
	char* file_name = "/mnt/pmem0/khb/buffer_10G.txt";
	int fd = open(file_name, O_RDWR);
	cout<<fd<<endl;
	a = (long long*) mmap(0, 10ll<<30, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_SHARED, fd, 0);
	//aa = new long long[10000000000ll];
	printAddr((long long)a);
	int T = 10;
	init_func();
	long long t = 0;
	while (T--) {
		//memset(addrCache, -1, sizeof(addrCache));
		//int K = 32;
		long long N = (1ll << 30ll);
		threadtest(N);
		//calc(0, N);
		//for (int i = 0; i < N; i ++) {
			//long long *a = new long long [8];
			//memset(a, 0, sizeof(long long) * 8);
			//_mm512_stream_si512((__m512i*)(a + 8 * i), *((__m512i*)a));
			//a[i] = i;
		//}
		//flush_func(a, 8*N);
		//_mm_mfence();
	}
	cout<<t<<endl;
	return 0;
}
