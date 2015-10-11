#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "vector"
#include "string"
#include "map"
#include "unordered_set"
using namespace std;

class Process;
bool InitializeLocks();

class Controller {
public:
    bool ReadConfigFile();
    void WaitForThreadJoins();
    bool CreateProcesses();
    void CreateTransactions();
    bool ResurrectProcess(int process_id);
    void AddToAliveProcessIds(int process_id);
    void RemoveFromAliveProcessIds(int process_id);
    int ChooseCoordinator();
    void SetHandshakeToExpecting();
    void WaitTillHandshakeReady();
    void SetCoordHandshakeToInit3PC();
    void InformCoordinatorOfNewTransaction(int coord_id, int tid);
    void InformCoordiantorOfParticipants(int coord_id);
    void ResetProcesses(int coord_id);
    void IncrementPorts(int p);




    void set_coordinator(int coordinator_id);
    int get_coordinator();
    int get_listen_port(int process_id);
    int get_send_port_pid_map(int port_num);
    int get_send_port(int process_id);
    int get_alive_port_pid_map(int port_num);
    int get_alive_port(int process_id);
    int get_sdr_port_pid_map(int port_num);
    int get_sdr_port(int process_id);
    int get_up_port(int process_id);
    int get_up_port_pid_map(int port_num);

    // returns transaction string if transaction_id is valid
    // else returns the string "NULL"
    string get_transaction(int transaction_id);
    void KillProcess(int process_id);




protected:
    static int N;

private:
    static std::vector<int> listen_port_;
    // send_port number of each process
    static std::vector<int> send_port_;
    // alive_port number of each process
    static std::vector<int> alive_port_;
    static std::vector<int> sdr_port_;
    static std::vector<int> up_port_;

    // maps send ports to PIDs
    static std::map<int, int> send_port_pid_map_;
    // maps alive ports to PIDs
    static std::map<int, int> alive_port_pid_map_;
    static std::map<int, int> sdr_port_pid_map_;
    static std::map<int, int> up_port_pid_map_;

    // Process object's pointer for each process
    std::vector<Process> process_;
    // vector of threads for each process
    std::vector<pthread_t> process_thread_;
    static std::vector<string> transaction_;
    static int coordinator_;
    unordered_set<int> alive_process_ids_;

};

#endif //CONTROLLER_H