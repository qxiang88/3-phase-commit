#ifndef PROCESS_H
#define PROCESS_H

#include "controller.h"
#include "string"
#include "unordered_map"
#include "vector"
using namespace std;

// entry function for each process thread.
// takes as argument pointer to the Process object
void* ThreadEntry(void *p);
void* server(void* _p);
int return_port_no(struct sockaddr *sa);
void sigchld_handler(int s);
vector<string> split(string s, char delimiter);

class Process : public Controller {
public:
    void Initialize(int pid, string log_file, string playlist_file);
    bool LoadPlaylist();
    int ConnectToProcess(int process_id);
    void Print();
    void AddToFdSet(int add_fd);
    void RemoveFromFdSet(int remove_fd);
    void InitializeLocks();
    void UpdateTempFdSet();

    void AddToLog(string s, bool new_round = false);
    int GetCoordinator();
    vector<int> GetParticipants();
    string GetVote();
    string GetDecision();
    bool CheckCoordinator();
    void LoadTransactionId();
    void LoadLog();

    vector<string> get_log();
    int get_pid();
    int get_fd(int process_id);
    void set_pid(int process_id);
    void set_fd(int process_id, int new_fd);
    void set_log_file(string logfile);
    void set_playlist_file(string playlistfile);

private:
    int pid_;
    string log_file_;
    string playlist_file_;
    std::unordered_map<string, string> playlist_;
    // socket fd for connection to each process
    std::vector<int> fd;
    fd_set master_fds_;      // master file descriptor set
    fd_set temp_fds_;       // temp file descriptor set for select()
    int fd_max_;            // highest fd value currently in use

    map<int, vector<string> > log_;
    // vector<string> log_;
    // the coordinator which this process perceives
    // this is not same as the coordinator_ of Controller class
    // coordinator_ of controller class is the actual coordinator of the system
    // make sure to call Controller::set_coordinator() fn
    // everytime a process selects a new coordinator
    // so that the Controller always knows the coordinator ID
    int my_coordinator_;
    int transaction_id_;

};


#endif //PROCESS_H