#include <thread>
using namespace std;

struct multi_counter {
public:
    int* val;
    int thread_count;

    multi_counter(int threads) {
        thread_count = threads;
        val = new int [16 * thread_count];
        memset(val, 0, 64 * thread_count);
    }

    int read() {
        int ret = 0;
        for (int i = 0; i < thread_count; i ++) {
            ret += val[i << 4];
        }
        return ret;
    }

    void write(int v, int id) {
        val[id << 4] = v;
    }

    void add(int delta, int id) {
        val[id << 4] += delta;
    }
};
