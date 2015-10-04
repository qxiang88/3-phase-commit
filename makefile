all: controller

controller: controller.o process.o
	g++ -o controller controller.o process.o -pthread

controller.o: controller.cpp controller.h constants.h process.h
	g++ -c controller.cpp 

process.o: process.cpp process.h
	g++ -c process.cpp

clean:
	rm -f *.o controller