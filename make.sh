gcc -c GPIOhenglong.c -o GPIOhenglong.o
gcc -c main.c -o main.o
g++ -o UDPserver GPIOhenglong.o main.o -lpthread

