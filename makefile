all: controller

controller: controller.o process.o socket.o
	g++ -std=c++0x -o controller controller.o process.o socket.o -pthread

controller.o: controller.cpp controller.h constants.h process.h
	g++ -std=c++0x -c controller.cpp 

process.o: process.cpp process.h
	g++ -std=c++0x -c process.cpp

socket.o: socket.cpp
	g++ -std=c++0x -c socket.cpp

clean:
	rm -f *.o controller