all: controller cleanlog

controller: controller.o process.o socket.o coordinator.o participant.o
	g++ -std=c++0x -o controller controller.o process.o socket.o coordinator.o participant.o -pthread

controller.o: controller.cpp controller.h constants.h process.h
	g++ -std=c++0x -c controller.cpp 

process.o: process.cpp controller.h constants.h process.h
	g++ -std=c++0x -c process.cpp

socket.o: socket.cpp controller.h constants.h process.h
	g++ -std=c++0x -c socket.cpp

coordinator.o: coordinator.cpp process.cpp controller.h constants.h process.h
	g++ -std=c++0x -c coordinator.cpp

participant.o: participant.cpp process.cpp controller.h constants.h process.h
	g++ -std=c++0x -c participant.cpp

clean:
	rm -f *.o controller

cleanlog:
	rm -f log/*