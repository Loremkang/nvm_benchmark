#include <thread>
using namespace std;

struct normal_counter {
    int cnt;
    mutex mu;
    
public:
    int read() {
        int ret = 0;
        mu.lock();
        ret = cnt;
        mu.unlock();
        return ret;
    }

    void write(int v) {
        mu.lock();
        cnt = v;
        mu.unlock();
    }

    bool cas(int pre_val, int new_val) {
        bool ret = false;
        mu.lock();
        if (cnt == pre_val) {
            cnt = new_val;
            ret = true;
        }
        mu.unlock();
        return ret;
    }

    void add(int delta) {
        mu.lock();
        cnt += delta;
        mu.unlock();
    }
};
