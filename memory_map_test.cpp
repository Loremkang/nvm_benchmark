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
#include <thread>
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
	//#pragma omp parallel for num_threads(72)
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

long long *a;

int REPEAT = 30;
inline void calc(int k, int length) {
	int T = REPEAT;
	while (T --) {
		for (int j = k * length; j < (k + 1) * length; j ++) {
			a[j] = a[j] + j;
		}
	}
}

inline void flushwork(int k, int length) {
	int T = REPEAT;
	while (T --) {
		flush_func(a + k * length, 8 * length);
		_mm_mfence();
	}
}

inline void test(int l, int r, int length) {
	#pragma omp parallel for num_threads(64)
	for (int i = 0; i < 64; i ++) {
		if (i < l) {
			int T = 10;
			while (T --) {
				for (int j = i * length; j < (i + 1) * length; j ++) {
					a[j] = a[j] + j;
				}
			}
		} else if (i >= r) {
			int T = 100;
			while (T --) {
				flush_func(a + i * length, 8 * length);
				_mm_mfence();
			}
		}
	}

}

inline void threadtest(int l, int r, int length) {
	thread thr[64];
	for (int i = 0; i < 64; i ++) {
		if (i < l) {
			//cout<<i<<" calc"<<endl;
			thr[i] = thread(calc, i, length);
		} else if (i >= r) {
			//cout<<i<<" flush"<<endl;
			thr[i] = thread(flushwork, i, length);
		}
	}
	for (int i = 0; i < 64; i ++) {
		if (i < l || i >= r) {
			thr[i].join();
			//cout<<i<<endl;
		}
	}
}

// typedef unsigned long long uint64_t;
typedef uint64_t phys_addr_t;
typedef uint64_t rte_iova_t;
#define RTE_BAD_IOVA ((rte_iova_t)-1)
#define PFN_MASK_SIZE 8

phys_addr_t
mem_virt2phy(const void *virtaddr)
{
	int fd, retval;
	uint64_t page, physaddr;
	unsigned long virt_pfn;
	int page_size;
	off_t offset;

	//if (phys_addrs_available == 0)
	//	return RTE_BAD_IOVA;

	/* standard page size */
	page_size = getpagesize();

	fd = open("/proc/self/pagemap", O_RDONLY);
	if (fd < 0) {
		//RTE_LOG(INFO, EAL, "%s(): cannot open /proc/self/pagemap: %s\n",
		//	__func__, strerror(errno));
		printf("Open Pagemap Error");
		return RTE_BAD_IOVA;
	}

	virt_pfn = (unsigned long)virtaddr / page_size;
	offset = sizeof(uint64_t) * virt_pfn;
	if (lseek(fd, offset, SEEK_SET) == (off_t) -1) {
		//RTE_LOG(INFO, EAL, "%s(): seek error in /proc/self/pagemap: %s\n",
		//		__func__, strerror(errno));
		printf("seek error in /proc/self/pagemap:");
		close(fd);
		return RTE_BAD_IOVA;
	}

	retval = read(fd, &page, PFN_MASK_SIZE);
	close(fd);
	if (retval < 0) {
		//RTE_LOG(INFO, EAL, "%s(): cannot read /proc/self/pagemap: %s\n",
		//		__func__, strerror(errno));
		printf("cannot read /proc/self/pagemap:");
		return RTE_BAD_IOVA;
	} else if (retval != PFN_MASK_SIZE) {
		//RTE_LOG(INFO, EAL, "%s(): read %d bytes from /proc/self/pagemap "
		//		"but expected %d:\n",
		//		__func__, retval, PFN_MASK_SIZE);
		printf("read %d bytes from /proc/self/pagemap but expedted %d:\n", retval, PFN_MASK_SIZE);
		return RTE_BAD_IOVA;
	}

	/*
	 * the pfn (page frame number) are bits 0-54 (see
	 * pagemap.txt in linux Documentation)
	 */
	if ((page & 0x7fffffffffffffULL) == 0)
		return RTE_BAD_IOVA;

	physaddr = ((page & 0x7fffffffffffffULL) * page_size)
		+ ((unsigned long)virtaddr % page_size);

	return physaddr;
}

//long long a[1000000];
int main() {
	srand(time(NULL));
	//int fd = open("../nvram/buffer.txt", "rw");
	int fd = open("../../buffer_10G.txt", O_RDWR);
	char* a = (char*) mmap(0, 10ll<<30, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_SHARED, fd, 0);
	//void* a = new char[10ll<<30];
	for (uint64_t i = 0; i < 10; i ++) {
		a[i * 2 * 1024 * 1024] = 'x';
		//cout<<i<<endl;
	}
	printf("%d\n", sizeof(a));
	printf("%016llx %016llx\n", a, (a + (10ll << 30)));
	
	for (int i = 0; i < 2621440; i ++) {
		if (mem_virt2phy(a + 4096ll * i) == 0xffffffffffffffffll) {
			printf("%d\n", i);
			printf("%016llx\n", a + 4096ll * i);
			break;
		}
	}
	//char c;
	//scanf("%c", &c);
	//close(fd);
	return 0;
}
