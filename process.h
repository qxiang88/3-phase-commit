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
extern void* ThreadEntry(void *p);
extern void* server(void* _p);
extern void* responder(void *_p);
extern void* ReceiveVoteFromParticipant(void* _rcv_thread_arg);
extern void* ReceiveAckFromParticipant(void* _rcv_thread_arg);
extern void* ReceiveStateFromParticipant(void* _rcv_thread_arg);
extern void* SendAlive(void *_p);
extern void* ReceiveAlive(void *_p);
extern void* ReceiveStateOrDecReq(void *_p);
extern void* ReceiveDecision(void *_p);
extern void* NewCoordinatorMode(void *_p);
extern int return_port_no(struct sockaddr *sa);
extern void sigchld_handler(int s);
extern vector<string> split(string s, char delimiter);
extern void PrintUpSet(int, unordered_set<int>);

typedef enum
{
    UNINITIALIZED, ABORTED, UNCERTAIN, COMMITTABLE, COMMITTED, PROCESSTIMEOUT
} ProcessState;

typedef enum
{
    ERROR, YES, NO, TIMEOUT, ACK
} ReceivedMsgType;

typedef enum
{
    ABORT, COMMIT
} Decision;

class Process : public Controller {
public:
    void Initialize(int pid, string log_file, string playlist_file);
    bool LoadPlaylist();
    bool ConnectToProcess(int process_id);
    bool ConnectToProcessAlive(int process_id);
    bool ConnectToProcessSDR(int process_id);
    void Print();
    void InitializeLocks();
    void CoordinatorMode();
    void ParticipantMode();
    void TerminationParticipantMode();
    void ConstructVoteReq(string &msg);
    void ConstructStateReq(string &msg);
    void SendVoteReqToAll(const string &msg);
    void SendStateReqToAll(const string &msg);
    void SendDecReqToAll(const string &msg);
    void WaitForVotes();
    void ExtractMsg(const string &received_msg, string &extracted_msg, int &received_tid);
    void Vote(string trans);
    void SendAbortToProcess(int process_id);
    void ConstructGeneralMsg(const string &msg_body,
                             const int transaction_id, string &msg);
    void SendPreCommitToProcess(int);
    void SendPreCommitToAll();
    void WaitForAck();
    void WaitForDecisionResponse();
    void SendCommitToAll();
    bool ExtractFromVoteReq(const string &msg, string &transaction_msg );
    bool WaitForVoteReq(string &transaction_msg);
    void SendMsgToCoordinator(const string &msg_to_send);
    void ReceivePreCommitOrAbortFromCoordinator();
    void ReceiveCommitFromCoordinator();
    void CreateAliveThreads(vector<pthread_t> &receive_alive_thread, pthread_t &send_alive_thread);
    void CreateSDRThread(int process_id, pthread_t &sdr_receive_thread);
    void UpdateUpSet(std::unordered_set<int> &alive_processes);
    void RemoveFromUpSet(int);
    void ConstructUpSet();
    void SendState(int);
    void AddThreadToSet(pthread_t thread);
    void RemoveThreadFromSet(pthread_t thread);
    void CreateThread(pthread_t &thread, void* (*f)(void* ), void* arg);

    void CloseFDs();
    void CloseAliveFDs();
    void CloseSDRFDs();


    void Recovery();
    void Timeout();
    void TerminationProtocol();
    void ElectionProtocol();
    bool SendURElected(int p);
    int GetNewCoordinator();
    void DecisionRequest();
    void WaitForStates();
    void SendDecision(int);
    void SendPrevDecision(int, int);

    void AddToLog(string s, bool new_round = false);
    int GetCoordinator();
    void LoadParticipants();
    string GetVote();
    string GetDecision();
    bool CheckCoordinator();
    void LoadTransactionId();
    void LoadLogAndPrevDecisions();
    void LoadUp();
    void LogCommit();
    void LogPreCommit();
    void LogAbort();
    void LogYes();
    void LogVoteReq();
    void LogStart();
    void LogUp();

    vector<string> get_log();
    int get_pid();
    int get_fd(int process_id);
    int get_alive_fd(int process_id);
    int get_sdr_fd(int process_id);
    void set_pid(int process_id);
    void set_fd(int process_id, int new_fd);
    void set_alive_fd(int process_id, int new_fd);
    void set_sdr_fd(int process_id, int new_fd);
    void set_log_file(string logfile);
    void set_playlist_file(string playlistfile);
    void set_my_coordinator(int process_id);
    ProcessState get_my_state();
    int get_my_coordinator();
    int get_transaction_id();
    void set_state_req_in_progress(bool );
    void set_my_state(ProcessState state);


    // list of processes operational for a transaction (and hence, an iteration of 3PC)
    // operational for an iteration is defined as a process which
    // NEVER failed during that iteration
    // does not include self
    unordered_set<int> up_;

    // list of processes involved in a transaction (and hence, an iteration of 3PC)
    unordered_set<int> participants_;
    std::unordered_map<int, ProcessState> participant_state_map_;
    pthread_t newcoord_thread;
    vector<Decision> prev_decisions_;
    // set of all threads created by a process
    std::unordered_set<pthread_t> thread_set;
    std::unordered_set<int> alive_processes;

private:
    int pid_;
    string log_file_;
    string playlist_file_;
    std::unordered_map<string, string> playlist_;
    bool state_req_in_progress;

    // socket fd for each process corresponding to send connection
    std::vector<int> fd_;

    // socket fd for each process corresponding to alive connection
    std::vector<int> alive_fd_;
    std::vector<int> sdr_fd_;

    // state of each process
    // for use by coordinator
    std::vector<ProcessState> process_state_;
    ProcessState my_state_;     // processes self-state

    // map of participant process ids and their state
    // to be used only by coordinator
    map<int, vector<string> > log_;


    // bool am_coordinator_;

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
    ReceivedMsgType received_msg_type;
};

struct ReceiveStateThreadArgument
{
    Process *p;
    ProcessState st;
    int pid;
    int transaction_id;
};

struct ReceiveDecThreadArgument
{
    Process *p;
    Decision decision;
    int pid;
    int transaction_id;
    ReceivedMsgType received_msg_type;
};


struct ReceiveAliveThreadArgument
{
    Process *p;
    int pid_from_whom;
};

struct ReceiveSDRThreadArgument
{
    Process *p;
    int pid_to_whom;
};

#endif //PROCESS_H