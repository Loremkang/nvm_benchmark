#include <iostream>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <thread>
#include "normal_counter.h"
#include "multi_counter.h"
using namespace std;

int thread_num, limit_number, total_work;
bool* thread_count;
multi_counter* counter;

void thread_work(int id) {
    cout<<id<<endl;
    for (int i = 0; i < total_work; i ++) {
        while (true) {
            int v = counter->read();
            if (v < limit_number) {
                counter->add(1, id);
                break;
            }
            cout<<"FAIL!!!"<<endl;
        }
        // do something
        counter->add(-1, id);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cout << "./fast_concurrent_counter [#num_of_threads] [#num_of_limit] [#total_work]" << endl;
    }
    thread_num = atoi(argv[1]);
    limit_number = atoi(argv[2]);
    total_work = atoi(argv[3]);
    counter = new multi_counter(thread_num);
    cout << thread_num << endl;
    thread_count = new bool[64 * thread_num];
    thread** thrs = new thread*[thread_num];
    for (int i = 0; i < thread_num; i ++) {
        thrs[i] = new thread(thread_work, i);
    }
    for (int i = 0; i < thread_num; i ++) {
        thrs[i]->join();
    }
    return 0;
}