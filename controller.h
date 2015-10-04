#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "pthread.h"
#include "vector"
#include "string"
#include "map"
using namespace std;

class Process;

class Controller {
public:
    bool ReadConfigFile();
    void WaitForThreadJoins();
    bool CreateProcesses();
    void set_coordinator(int coordinator_id);
    int get_coordinator();

protected:
    int N;

private:
    // number of processes
    std::vector<int> listen_port_;
    std::vector<int> send_port_;
    // maps send ports to PIDs
    std::map<int, int> send_port_pid_map_;
    // Process object's pointer for each process
    std::vector<Process> process_;
    // vector of threads for each process
    std::vector<pthread_t> process_thread_;
    int coordinator_;

};

#endif //CONTROLLER_H