#ifndef CONTROLLER_H
#define CONTROLLER_H

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
    int get_listen_port(int process_id);
    int get_send_port_pid_map(int port_num);
    int get_send_port(int process_id);


protected:
    static int N;

private:
    // number of processes
    static std::vector<int> listen_port_;
    static std::vector<int> send_port_;
    // maps send ports to PIDs
    static std::map<int, int> send_port_pid_map_;
    // Process object's pointer for each process
    std::vector<Process> process_;
    // vector of threads for each process
    std::vector<pthread_t> process_thread_;
    int coordinator_;

};

#endif //CONTROLLER_H