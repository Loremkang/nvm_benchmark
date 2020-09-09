all: benchmark

benchmark: write_after_flush.cpp
	g++ write_after_flush.cpp -o write_after_flush -Wall -Wextra -Wconversion -O0 -mavx512f -g -march=native -mavx -mavx2 -pthread