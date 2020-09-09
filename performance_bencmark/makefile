all: benchmark

benchmark: main.cpp flush_functions.h
	g++ main.cpp -o main -Wall -Wextra -Wconversion -O2 -mavx512f -g -march=native -mavx -mavx2 -pthread