#ifndef PROCESS_H
#define PROCESS_H

#include "controller.h"
#include "string"
#include "unordered_map"
#include "unordered_set"
#include "vector"
using namespace std;

// entry function for each process thread.
// takes as argument pointer to the Process object
void* ThreadEntry(void *p);
void* server(void* _p);
void* ReceiveVoteFromParticipant(void* _rcv_thread_arg);
void* ReceiveAckFromParticipant(void* _rcv_thread_arg);
int return_port_no(struct sockaddr *sa);
void sigchld_handler(int s);
vector<string> split(string s, char delimiter);

typedef enum
{
    UNINITIALIZED, ABORTED, UNCERTAIN, COMMITTABLE, COMMITTED, PROCESSTIMEOUT
} ProcessState;

typedef enum
{
    ERROR, YES, NO, TIMEOUT, ACK
} ReceivedMsgType;

class Process : public Controller {
public:
    void Initialize(int pid, string log_file, string playlist_file);
    bool LoadPlaylist();
    bool ConnectToProcess(int process_id);
    void Print();
    // void AddToFdSet(int add_fd);
    // void RemoveFromFdSet(int remove_fd);
    void InitializeLocks();
    // void CreateCopiesForSelect(fd_set &temp_fds, int &temp_fd_max);
    void CoordinatorMode();
    void ParticipantMode();
    void ConstructVoteReq(string &msg);
    void SendVoteReqToAll(const string &msg);
    void WaitForVotes();
    void ExtractMsg(const string &received_msg, string &extracted_msg, int &received_tid);
    void Vote(string trans);
    void SendAbortToProcess(int process_id);
    void ConstructGeneralMsg(const string &msg_body,
                             const int transaction_id, string &msg);
    void SendPreCommitToAll();
    void WaitForAck();
    void SendCommitToAll();
    bool ExtractFromVoteReq(const string &msg, string &transaction_msg );
    bool WaitForVoteReq(string &transaction_msg);
    void SendMsgToCoordinator(const string &msg_to_send);
    void ReceivePreCommitOrAbortFromCoordinator();
    void ReceiveCommitFromCoordinator();










        void AddToLog(string s, bool new_round = false);
        int GetCoordinator();
        void LoadParticipants();
        string GetVote();
        string GetDecision();
        bool CheckCoordinator();
        void LoadTransactionId();
        void LoadLog();

        void LogCommit();
        void LogPreCommit();
        void LogAbort();
        void LogYes();
        void LogVoteReq();
        void LogStart();

        vector<string> get_log();
        int get_pid();
        int get_fd(int process_id);
        void set_pid(int process_id);
        void set_fd(int process_id, int new_fd);
        void set_log_file(string logfile);
        void set_playlist_file(string playlistfile);
        void set_my_coordinator(int process_id);

private:
        int pid_;
        string log_file_;
        string playlist_file_;
        std::unordered_map<string, string> playlist_;

        // socket fd for connection to each process
        std::vector<int> fd_;
        // fd_set master_fds_;      // master file descriptor set
        // int fd_max_;            // highest fd value currently in use

        // state of each process
        // for use by coordinator
        std::vector<ProcessState> process_state_;
        ProcessState my_state_;     // processes self-state

        // map of participant process ids and their state
        // to be used only by coordinator
        std::unordered_map<int, ProcessState> participant_state_map_;
        map<int, vector<string> > log_;

        bool am_coordinator_;
        vector<int> participants_;

        // the coordinator which this process perceives
        // this is not same as the coordinator_ of Controller class
        // coordinator_ of controller class is the actual coordinator of the system
        // make sure to call Controller::set_coordinator() fn
        // everytime a process selects a new coordinator
        // so that the Controller always knows the coordinator ID

        //can there be votereq of new process while one 3PC ongoing?
        int my_coordinator_;
        int transaction_id_;
    };

    struct ReceiveThreadArgument
    {
        Process *p;
        string expected_msg1;
        string expected_msg2;
        int pid;
        int transaction_id;
    };


#endif //PROCESS_H