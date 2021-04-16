tun: main.o tunnel.o
	gcc main.o tunnel.o -o tun -levent -lstdc++
tunnel.o: tunnel.h tunnel.cpp
	gcc -c tunnel.cpp -o tunnel.o
main.o: tunnel.h main.cpp
	gcc -c main.cpp -o main.o
clean:
	rm *.o tun
