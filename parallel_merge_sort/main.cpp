#include <iostream>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <sys/mman.h>
#include <thread>
#include <fcntl.h>
using namespace std;

int MAXN = 100000000;
int n = 100000000;
int TOTAL_THREADS = 16;
int TOTAL_DIVISIONS = 16;

void printArr(long long *a, int n) {
    return;
    for (int i = 0; i < n; i ++) {
        printf("%lld ", a[i]);
    }
    printf("\n");
}

inline void merge(long long *arr, long long *tmp, int l, int mid, int r) {
    int ll = l, rr = mid, start = l, size = r - l;
    while (ll < mid && rr < r) {
        if (arr[ll] <= arr[rr]) {
            tmp[l ++] = arr[ll ++];
        } else {
            tmp[l ++] = arr[rr ++];
        }
    }
    while (ll != mid) {
        tmp[l ++] = arr[ll ++];
    }
    while (rr != r) {
        tmp[l ++] = arr[rr ++];
    }
    memcpy(arr + start, tmp + start, sizeof(long long) * size);
}

void merge_sort_serial(long long *arr, long long *tmp, int l, int r) {
    if (r - l == 1) return;
    int mid = (l + r) >> 1;
    merge_sort_serial(arr, tmp, l, mid);
    merge_sort_serial(arr, tmp, mid, r);
    // printf("%d*%d*%d  ", l, mid, r); printArr(arr + l, r - l);
    merge(arr, tmp, l, mid, r);
    // printArr(arr + l, r - l);
}

void work(long long *arr, long long *tmp, int l, int r, int id, int total_divisions) {
    int len = (r - l) / total_divisions;
    int ll = id * len + l;
    int rr = (id + 1) * len + l;
    // merge_sort_serial(arr, tmp, ll, rr);
    sort(arr + ll, arr + rr);
}

void work_for_thread(long long *arr, long long *tmp, int id, int threads, int total_divisions) {
    int num_of_work = total_divisions / threads;
    for (int i = id * num_of_work; i < (id + 1) * num_of_work; i ++) {
        printf("Work %d For Thread %d\n", i, id);
        work(arr, tmp, 0, n, i, total_divisions);
    }
}

void bind(pthread_t thread, int cpuid) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpuid * 4, &cpuset);
    int rc = pthread_setaffinity_np(thread, sizeof(cpu_set_t),
                                    &cpuset);
}

// bool merge_sort_parallel(long long *arr, long long *tmp, int l, int r, int id, int total_threads) {
//     bind(pthread_self(), id - 1);
//     int mid = (l + r) >> 1;
//     if (i * 2 < total_threads) {
//         merge_sort_parallel(arr, tmp, l, mid, id, total_threads);
//     }
// }

void printTime(timespec time_sys) {
    printf("process_time: %ld ns, %ld s\n", time_sys.tv_nsec, time_sys.tv_sec);
}

int main() {
    bind(pthread_self(), 17);
    srand(time(NULL));
	//int fd = open("../nvram/buffer.txt", "rw");
	char* file_name = "/mnt/pmem0/khb/buffer_10G.txt";

	int fd = open(file_name, O_RDWR);
	cout<<fd<<endl;
	long long total_len = 10ll << 30;
	long long *a = (long long*) mmap(0, total_len, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_SHARED, fd, 0);
    // long long *a = new long long [MAXN];

    // long long *tmp = new long long [MAXN];
    long long *tmp = a + MAXN;
    for (int i = 0; i < n; i ++) {
        a[i] = i;
    }
    for (int i = 0; i < n; i ++) {
        int k = rand() % (n - i);
        swap(a[i], a[i + k]);
    }

    printArr(a, n);
    timespec start, end;
    clock_gettime(CLOCK_REALTIME, &start);
    printTime(start);

    // merge_sort_serial(a, tmp, 0, n);

    thread thrs[TOTAL_THREADS];
    for (int i = 0; i < TOTAL_THREADS; i ++) {
        // thrs[i] = thread(merge_sort_serial, a, tmp, i * (n / TOTAL_THREADS), (i + 1) * (n / TOTAL_THREADS));
        thrs[i] = thread(work_for_thread, a, tmp, i, TOTAL_THREADS, TOTAL_DIVISIONS);
        bind(thrs[i].native_handle(), i);
    }
    for (int i = 0; i < TOTAL_THREADS; i ++) {
        thrs[i].join();
    }
    clock_gettime(CLOCK_REALTIME, &end);
    printTime(end);
    cout << "Time Spent: " << 1e-9 * ((end.tv_sec - start.tv_sec) * 1000000000 + end.tv_nsec - start.tv_nsec) << endl;

    
    // merge_sort_parallel(a, tmp, 0, n, 1, 4)

    printArr(a, n);

    return 0;
}