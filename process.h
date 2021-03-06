#ifndef PROCESS_H
#define PROCESS_H

#include "controller.h"
#include "string"
#include "fstream"
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
extern void* SendUpReq(void *_p);
extern void* ReceiveAlive(void *_p);
extern void* ReceiveStateOrDecReq(void *_p);
extern void* ReceiveUp(void* _arg);
// extern void* ReceiveUpSet(void* _rcv_thread_arg);
extern void* ReceiveDecision(void *_p);
extern void* NewCoordinatorMode(void *_p);
extern int return_port_no(struct sockaddr *sa);
extern void sigchld_handler(int s);
extern vector<string> split(string s, char delimiter);
extern set<int> convertStringToSet(string s);
extern string ConvertSetToString(set<int> a);
extern void PrintUpSet(int, set<int>);
extern void* SendDecReq(void *_p);
extern void* SendUpReq(void *_p);
// extern void* CheckFdReceiveThread(void *_arg);

struct ReceiveSDRUpThreadArgument;
struct ReceiveAliveThreadArgument;

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

typedef enum
{
    EXPECTING, READY, INIT3PC, BLANK
} Handshake;

// status of each process. Used by controller to move to next transaction
typedef enum
{
    DYING,

    // process is still involved in a transaction related message-passing (but never failed)
    RUNNING,

    // process has completed 3PC for the curr transaction. Coordinator mode/Participant Mode over
    DONE,

    // process is in recovery mode. This means it failed in the past. When it reaches a decision,
    // its state should change to DONE
    RECOVERY
} ProcessRunningStatus;

class Process : public Controller {
public:
    void Initialize(int pid,
                    string log_file,
                    string playlist_file,
                    int coord_id,
                    ProcessRunningStatus status);

    void Reset(int coord_id);
    bool LoadPlaylist();
    bool ConnectToProcess(int process_id);
    bool ConnectToProcessAlive(int process_id);
    bool ConnectToProcessSDR(int process_id);
    bool ConnectToProcessUp(int process_id);
    
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
    void WaitForUpResponse();
    void SendCommitToAll();
    bool ExtractFromVoteReq(const string &msg, string &transaction_msg );
    int WaitForVoteReq(string &transaction_msg);
    void SendMsgToCoordinator(const string &msg_to_send, int c_id);
    void ReceivePreCommitOrAbortFromCoordinator(int c_id);
    void ReceiveAnythingFromCoordinator(int c_id);
    void ReceiveCommitFromCoordinator(int c_id);
    void CreateAliveThreads(vector<pthread_t> &receive_alive_thread, pthread_t &send_alive_thread);
    void CreateSDRThread(int process_id, pthread_t &sdr_receive_thread);
    void CreateUpThread(int process_id, pthread_t &up_receive_thread);
    void UpdateUpSet(std::set<int> &alive_processes);
    void RemoveFromUpSet(int);
    void ConstructUpSet(int coord_id);
    void SendState(int);
    void AddThreadToSet(pthread_t thread);
    void RemoveThreadFromSet(pthread_t thread);
    void CreateThread(pthread_t &thread, void* (*f)(void* ), void* arg);
    void ThreeWayHandshake();
    void WaitForInit3PC();
    void AddThreadToSetAlive(pthread_t thread);
    void RemoveThreadFromSetAlive(pthread_t thread);
    void CreateThreadForAlive(pthread_t &thread, void* (*f)(void* ), void* arg);
    void KillAliveThreads();
    void DecrementNumMessages();
    void ContinueOrDie();
    void Die();
    string ConvertSetToString(set<int> a);

    void SendUpReqToAll();
    void SendMyUp(int pid_other);
    void ConstructUpReq(string &msg);
    void ConstructUpResponse(string &msg);
    string ConvertUpSetToString();
    void SetUpAndWaitRecovery();

    

    void CloseFDs();
    void CloseAliveFDs();
    void CloseSDRFDs();
    void CloseUpFDs();
    // void CheckFdReceive(int);
    // void CheckAllFdReceives() ;


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
    bool CheckForTotalFailure(set<int> &alive_processes_now, set<int> &intersection_up, vector<bool> &crashed, bool &operational_process_exists);
    void TotalFailure();
    void AddToLog(string s, bool new_round = false);
    int GetCoordinator();
    void LoadParticipants();
    string GetVote();
    string GetDecision();
    bool CheckCoordinator();
    void LoadTransactionId();
    bool LoadLogAndPrevDecisions();
    void LoadUp();
    void LogCommit();
    void LogPreCommit();
    void LogAbort();
    void LogYes();
    void LogVoteReq(int coord_id);
    void LogStart();
    void LogUp();
    void LogCommitOrAbort();

    vector<string> get_log();
    int get_pid();
    int get_fd(int process_id);
    int get_up_fd(int process_id); 
    int get_alive_fd(int process_id);
    int get_sdr_fd(int process_id);
    void set_pid(int process_id);
    void set_fd(int process_id, int new_fd);
    void set_up_fd(int process_id, int new_fd); 
    void set_alive_fd(int process_id, int new_fd);
    void set_sdr_fd(int process_id, int new_fd);
    void set_log_file(string logfile);
    void set_playlist_file(string playlistfile);
    void set_my_coordinator(int process_id);
    int get_transaction_id();
    void set_transaction_id(int tid);
    Handshake get_handshake();
    void set_handshake(Handshake hs);
    ProcessState get_my_state();
    int get_my_coordinator();
    void set_state_req_in_progress(bool );
    void set_my_state(ProcessState state);
    ProcessRunningStatus get_my_status();
    void set_my_status(ProcessRunningStatus status);
    void set_server_sockfd(int socket_fd);
    int get_server_sockfd();
    float get_num_messages();
    void set_num_messages(float num);
    bool get_decision_logged();
    void set_decision_logged();
    void Close_server_sockfd();
    void reset_fd(int process_id);
    void reset_alive_fd(int process_id);
    void reset_up_fd(int process_id);
    void reset_sdr_fd(int process_id);
    void my_backtrace();



    // list of processes operational for a transaction (and hence, an iteration of 3PC)
    // operational for an iteration is defined as a process which
    // NEVER failed during that iteration
    // does not include self
    set<int> up_;
    map< int, set<int> > all_up_sets_;
    set<int> previous_up_;
    // list of processes involved in a transaction (and hence, an iteration of 3PC)
    unordered_set<int> participants_;
    std::unordered_map<int, ProcessState> participant_state_map_;
    pthread_t newcoord_thread;
    vector<Decision> prev_decisions_;
    // set of all threads created by a process (except alive threads)
    std::unordered_set<pthread_t> thread_set;
    // set of all alive threads created by a process
    std::unordered_set<pthread_t> thread_set_alive_;
    bool new_coord_thread_made;
    bool dead;
    vector<ReceiveAliveThreadArgument*> rcv_alive_thread_arg;

    // ofstream backout;

private:
    int pid_;
    string log_file_;
    string playlist_file_;
    std::unordered_map<string, string> playlist_;
    bool state_req_in_progress;
    Handshake handshake_;

    // socket fd for each process corresponding to send connection
    std::vector<int> fd_;

    // socket fd for each process corresponding to alive connection
    std::vector<int> alive_fd_;
    std::vector<int> sdr_fd_;
    std::vector<int> up_fd_;

    // state of each process
    // for use by coordinator
    ProcessState my_state_;     // processes self-state

    // map of participant process ids and their state
    // to be used only by coordinator
    map<int, vector<string> > log_;

    // Process' own running status
    ProcessRunningStatus my_status_;
    int server_sockfd_;
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
    // number of messages after which process kills itself
    // float value because a value like 2.5 can be used to encode case
    // where process sends 2 messages and waits for receive
    // as oppposed to value=2, where process dies immediately after sending msg2
    float num_messages_;
    bool decision_logged_;

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

struct ReceiveUpThreadArgument
{
    Process *p;
    int pid;
    int transaction_id;
    set<int> up;
    ReceivedMsgType received_msg_type;
};

struct ReceiveAliveThreadArgument
{
    Process *p;
    int pid_from_whom;
};

struct ReceiveSDRUpThreadArgument
{
    Process *p;
    int for_whom;
};

struct ReceiveCoordThreadArgument
{
    Process *p;
    int c_id;
};


#endif //PROCESS_H