#include "flush_functions.h"
#include <thread>
#define FILESIZE 5*1024*1024*1024ULL + 24
using namespace std;

inline long long equalSplitStartAddr(void* addr, int cid, int total_cid,  int length) {
	int pieces = length / total_cid;
	return ((long long)addr) + pieces * cid;
}

inline void sequentialWrites(int cid, void* addr, long long length) {
	/*char* startAddr = (char*)(((long long) addr) & (-64ll));
	length += (char*)addr - startAddr;
	cout<<addr<<' '<<length<<endl;
	__m512i zero512 = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
	length = length / 64 - 1;
	for (int i = 0; i < length; i ++) {
		//__m512i zero512 = _mm512_setzero_si512();
		//__m512i zero512 = _mm512_set_epi32(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);
		startAddr += 64;
		__m512i* mem_addr = (__m512i*)(startAddr);
		_mm512_stream_si512(mem_addr, zero512);
	}*/
	long long* startAddr = (long long*) addr;
	length /= 8;
	for (int i = 0; i < length; i ++) {
		startAddr[i] = 0;
	}
}

inline void randomWrites(int cid, void* addr, long long capacity, int granularity, int count, int length) {
	int pieces = capacity / granularity;
	cout<<pieces<<' '<<granularity<<endl;
	for (int i = 0; i < count; i ++) {
		long long id = rand() % pieces;
		void* target_addr = ((char*)addr) + id * granularity;
		sequentialWrites(cid, target_addr, length);
	}
}

template< class Function, class... Args>
inline void runParallel(int thread_count, Function&& function, void* addr, long long length, Args&&... args) {
	thread *thr = new thread[thread_count];
	for (long long i = 0; i < thread_count; i ++) {
		cout<<i<<" calc"<<endl;
		// thr[i] = thread(calc, a, i, length);
		//thr[i] = thread(function, i, (char*)addr, length, args...);
		thr[i] = thread(function, i, ((char*)addr) + i * length, length, args...);
		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET(i * 4, &cpuset);
		int rc = pthread_setaffinity_np(thr[i].native_handle(),
										sizeof(cpu_set_t), &cpuset);
		if (rc != 0) {
			std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
		}
	}
	for (int i = 0; i < thread_count; i ++) {
		thr[i].join();
	}
}

//long long a[1000000];
int main(int argc, char** argv) {
	srand(time(NULL));
	//int fd = open("../nvram/buffer.txt", "rw");
	char* file_name = "/mnt/pmem0/khb/buffer_10G.txt";
	int fd = open(file_name, O_RDWR);
	cout<<fd<<endl;
	long long total_len = 10ll << 30;
	char *a = (char*) mmap(0, total_len, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_SHARED, fd, 0);
	//char *a = new char[total_len + 8192];
	cout<<"Base Addr: "; printAddr((long long)a);
	if (((long long)a) & 63) {
		a = (char*)(((long long)a) & (-64ll));
		a = a + 64ll;
		cout<<"Moved Addr: "; printAddr((long long)a);
	}
	printAddr((long long)a);
	int T = 10;
	init_func();
	int thread_count = atoi(argv[1]);
	while (T--) {
		//runParallel(thread_count, randomWrites, a, total_len, 4 * 1024, 1024ll * 1024 / thread_count, 4 * 1024);
		runParallel(thread_count, sequentialWrites, a, total_len / thread_count);
		for (int i = 0; i < 10; i ++) {
			int offset = rand() % (total_len / 8);
			long long *output = (long long *)a;
			printAddr(offset);
			cout<<(long long)output[offset]<<endl;
		}
		
	}
	return 0;
}
