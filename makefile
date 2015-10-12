all: controller cleanlog

controller: controller.o process.o socket.o coordinator.o participant.o alive.o sdr.o up.o log.o recover.o send.o thread.o
	g++ -g -rdynamic -std=c++0x -o controller controller.o process.o socket.o coordinator.o participant.o alive.o sdr.o up.o log.o recover.o send.o thread.o -pthread

controller.o: controller.cpp controller.h constants.h process.h
	g++ -g -rdynamic -std=c++0x -c controller.cpp 

process.o: process.cpp controller.h constants.h process.h
	g++ -g -rdynamic -std=c++0x -c process.cpp

socket.o: socket.cpp controller.h constants.h process.h
	g++ -g -rdynamic -std=c++0x -c socket.cpp

coordinator.o: coordinator.cpp controller.h constants.h process.h
	g++ -g -rdynamic -std=c++0x -c coordinator.cpp

participant.o: participant.cpp controller.h constants.h process.h
	g++ -g -rdynamic -std=c++0x -c participant.cpp

alive.o: alive.cpp controller.h constants.h process.h
	g++ -g -rdynamic -std=c++0x -c alive.cpp

sdr.o: sdr.cpp controller.h constants.h process.h
	g++ -g -rdynamic -std=c++0x -c sdr.cpp

up.o: up.cpp controller.h constants.h process.h
	g++ -g -rdynamic -std=c++0x -c up.cpp

log.o: log.cpp controller.h constants.h process.h
	g++ -g -rdynamic -std=c++0x -c log.cpp

recover.o: recover.cpp controller.h constants.h process.h
	g++ -g -rdynamic -std=c++0x -c recover.cpp

send.o: send.cpp controller.h constants.h process.h
	g++ -g -rdynamic -std=c++0x -c send.cpp

thread.o: thread.cpp controller.h constants.h process.h
	g++ -g -rdynamic -std=c++0x -c thread.cpp

clean:
	rm -f *.o controller

cleanlog:
	rm -rf log/*
	mkdir log/recalivelog/
	mkdir log/sendalivelog/
	mkdir log/sdr/
	mkdir log/up/
	mkdir log/backout/
	# ./controller
	