#ifndef PROCESS_H
#define PROCESS_H

#include "controller.h"
#include "string"
using namespace std;

// entry function for each process thread.
// takes as argument pointer to the Process object
void* ThreadEntry(void *P);

class Process : public Controller {
public:
    Process(int pid, string log_file, string playlist_file)
        :   pid_(pid),
            log_file_(log_file),
            playlist_file_(playlist_file) {}

private:
    int pid_;
    string log_file_;
    string playlist_file_;
    // the coordinator which this process perceives
    // this is not same as the coordinator_ of Controller class
    // coordinator_ of controller class is the actual coordinator of the system
    // make sure to call Controller::set_coordinator() fn 
    // everytime a process selects a new coordinator
    // so that the Controller always knows the coordinator ID
    int my_coordinator_;

};

#endif //PROCESS_H