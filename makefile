all: controller cleanlog

controller: controller.o process.o socket.o coordinator.o participant.o alive.o sdr.o
	g++ -g -std=c++0x -o controller controller.o process.o socket.o coordinator.o participant.o alive.o sdr.o -pthread

controller.o: controller.cpp controller.h constants.h process.h
	g++ -g -std=c++0x -c controller.cpp 

process.o: process.cpp controller.h constants.h process.h
	g++ -g -std=c++0x -c process.cpp

socket.o: socket.cpp controller.h constants.h process.h
	g++ -g -std=c++0x -c socket.cpp

coordinator.o: coordinator.cpp controller.h constants.h process.h
	g++ -g -std=c++0x -c coordinator.cpp

participant.o: participant.cpp controller.h constants.h process.h
	g++ -g -std=c++0x -c participant.cpp

alive.o: alive.cpp controller.h constants.h process.h
	g++ -g -std=c++0x -c alive.cpp

sdr.o: sdr.cpp controller.h constants.h process.h
	g++ -g -std=c++0x -c sdr.cpp

clean:
	rm -f *.o controller

cleanlog:
	rm -rf log/*
	mkdir log/recalivelog/
	mkdir log/sendalivelog/
	mkdir log/sdr/
	./controller