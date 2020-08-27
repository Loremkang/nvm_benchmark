#include <thread>
#include <iostream>
#include <map>
#include <string>
using namespace std;

int t;
string* strs;
map<string, int> str2int;
const int TC = 72;
thread* threads[TC];

long long T = 100000000ll * 72 / TC;
long long anss[10000];

void work_map(int tid, int l, int r) {
	for (int i = l; i < r; i ++) {
		anss[tid*32] += str2int[strs[(i&7)]];
	}
}

void map_test() {
	for (int i = 0; i < t; i ++) {
		str2int[strs[i]] = i;
	}
	for (int i = 0; i < TC; i ++) {
		threads[i] = new thread(work_map, i, 0, T);
	}
	long long ans = 0;
	for (int i = 0; i < TC; i ++) {
		threads[i]->join();
		ans += anss[i*32];
	}
	cout<<ans<<endl;
}

const int MOD = 256;
int hash_array[MOD];
inline int hash_func(const string& str) {
	int len = str.length();
	int ans = 0;
	for (int i = 0; i < len; i ++) {
		ans = ans * 25 + str[i];
		ans &= MOD - 1;
	}
	return ans;
}

void work_hash(int tid, int l, int r) {
	for (int i = l; i < r; i ++) {
		anss[tid*32] += hash_array[hash_func(strs[(i&7)])];
	}
}

void hash_test() {
	for (int i = 0; i < t; i ++) {
		int& t = hash_array[hash_func(strs[i])];
		cout<<hash_func(strs[i])<<' '<<t<<' '<<strs[i]<<endl;
		if (t != 0) {
			exit(0);
		}
		t = i + 1;
	}
	for (int i = 0; i < TC; i ++) {
		threads[i] = new thread(work_hash, i, 0, T);
	}
	long long ans = 0;
	for (int i = 0; i < TC; i ++) {
		threads[i]->join();
		ans += anss[i*32];
	}
	cout<<ans<<endl;
}
		
int main() {
	freopen("test.in", "r", stdin);
	cin>>t;
	strs = new string[t];
	for (int i = 0; i < t; i ++) {
		cin >> strs[i];
	}
	//map_test();
	hash_test();
}
