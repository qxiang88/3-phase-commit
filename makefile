all: controller

controller: controller.o process.o socket.o
	g++ -std=c++11 -o controller controller.o process.o socket.o -pthread

controller.o: controller.cpp controller.h constants.h process.h
	g++ -std=c++11 -c controller.cpp 

process.o: process.cpp process.h
	g++ -std=c++11 -c process.cpp

socket.o: socket.cpp
	g++ -std=c++11 -c socket.cpp

clean:
	rm -f *.o controller