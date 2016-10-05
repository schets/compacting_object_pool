all:
	g++ -std=c++11 -O3 -g -c tree.cpp -fno-omit-frame-pointer
	gcc -O3 -std=c99 single_list.c common.c -c -fno-omit-frame-pointer
	g++ *.o -o test
