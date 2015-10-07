#include "process.h"
#include "constants.h"
#include "fstream"
#include "sstream"
#include "iostream"
#include "unistd.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include "sys/time.h"
#include "sstream"
using namespace std;

// construct VOTE-REQ msg. Sample format (without quotes:
// "<kVoteReq> <transaction_id> <transaction body> <numOfParticipants(=2)> <pid#1> <pid#2> $"
void Process::ConstructVoteReq(string &msg) {
    msg = kVoteReq + " " + to_string(transaction_id_) + " ";
    string trans = get_transaction(transaction_id_);
    if (trans == "NULL") {
        // no more transactions left
        // TODO: handle this case
        // TODO: make sure that if this is the case
        // a similar case is handled in CoordinatorMode() just before Vote() call
    } else {
        msg = msg + trans;
        msg = msg + " " + to_string(participant_state_map_.size());
        for ( auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
            msg = msg + " " + to_string(it->first);
        }
        msg = msg + " $";
    }
}

void Process::ConstructStateReq(string &msg) {
    msg = kStateReq + " " + to_string(transaction_id_);
    msg = msg + " $";
}

//send VOTE-REQ to all participants
void Process::SendVoteReqToAll(const string &msg) {
    for ( auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        // if ((it->first) == get_pid()) continue; // do not send to self

        if (send(get_fd(it->first), msg.c_str(), msg.size(), 0) == -1) {
            cout << "P" << get_pid() << ": ERROR: sending to P" << (it->first) << endl;
        }
        else {
            cout << "P" << get_pid() << ": Msg sent to P" << (it->first) << ": " << msg << endl;
        }
    }
}

void Process::SendStateReqToAll(const string &msg) {

    //this only contains operational processes for non timeout cases
    for ( auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        // if ((it->first) == get_pid()) continue; // do not send to self

        if (send(get_fd(it->first), msg.c_str(), msg.size(), 0) == -1) {
            cout << "P" << get_pid() << ": ERROR: sending to P" << (it->first) << endl;
        }
        else {
            cout << "P" << get_pid() << ": Msg sent to P" << (it->first) << ": " << msg << endl;
        }
    }
}

// coordinator waits for votes from all participants
// creates one receive thread for each participant
// sets state of each participant in participant_state_map_
void Process::WaitForVotes() {
    int n = participant_state_map_.size();
    std::vector<pthread_t> receive_thread(n);

    ReceiveThreadArgument **rcv_thread_arg = new ReceiveThreadArgument*[n];
    int i = 0;
    for (auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        rcv_thread_arg[i] = new ReceiveThreadArgument;
        rcv_thread_arg[i]->p = this;
        rcv_thread_arg[i]->pid = it->first;
        rcv_thread_arg[i]->transaction_id = transaction_id_;
        rcv_thread_arg[i]->expected_msg1 = kYes;
        rcv_thread_arg[i]->expected_msg2 = kNo;

        if (pthread_create(&receive_thread[i], NULL, ReceiveVoteFromParticipant, (void *)rcv_thread_arg[i])) {
            cout << "P" << get_pid() << ": ERROR: Unable to create receive thread for P" << get_pid() << endl;
            pthread_exit(NULL);
        }
        i++;
    }
    
    void* status;
    
    i = 0;
    for (auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        pthread_join(receive_thread[i], &status);
        if ((rcv_thread_arg[i]->received_msg_type) == ERROR) {
            //TODO: not necessarily. handle
            pthread_exit(NULL);
        } else if ((rcv_thread_arg[i]->received_msg_type) == YES) {
            // if a participant votes yes, mark its state as UNCERTAIN
            it->second = UNCERTAIN;
        } else if ((rcv_thread_arg[i]->received_msg_type) == NO) {
            // if a participant votes no, mark its state as ABORTED
            it->second = ABORTED;
        } else if ((rcv_thread_arg[i]->received_msg_type) == TIMEOUT) {
            // if a participant votes no, mark its state as PROCESSTIMEOUT
            it->second = PROCESSTIMEOUT;
        }
        i++;
    }
}

// coordinator waits for ACK from each participant
// creates one receive thread for each participant
// ignores the timeout of ACK receipts
void Process::WaitForAck() {
    int n = participant_state_map_.size();
    std::vector<pthread_t> receive_thread(n);

    ReceiveThreadArgument **rcv_thread_arg = new ReceiveThreadArgument*[n];
    int i = 0;
    for (auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        rcv_thread_arg[i] = new ReceiveThreadArgument;
        rcv_thread_arg[i]->p = this;
        rcv_thread_arg[i]->pid = it->first;
        rcv_thread_arg[i]->transaction_id = transaction_id_;
        rcv_thread_arg[i]->expected_msg1 = kAck;
        rcv_thread_arg[i]->expected_msg2 = "NULL";
        if (pthread_create(&receive_thread[i], NULL, ReceiveAckFromParticipant, (void *)rcv_thread_arg[i])) {
            cout << "P" << get_pid() << ": ERROR: Unable to create receive thread for P" << get_pid() << endl;
            pthread_exit(NULL);
        }
        i++;
    }

    void* status;
    i = 0;
    for (auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        pthread_join(receive_thread[i], &status);
        if ((rcv_thread_arg[i]->received_msg_type) == ERROR) {
            //TODO: not necessarily. handle
            pthread_exit(NULL);
        } else if ((rcv_thread_arg[i]->received_msg_type) == ACK) {
            // if a participant ACKed, good for you
            // no need to do anything
        } else if ((rcv_thread_arg[i]->received_msg_type) == TIMEOUT) {
            // if a participant timedout in ACK, skip it
            // no need to do anything
        }
        i++;
    }
}

void Process::WaitForStates() {
    int n = participant_state_map_.size();
    std::vector<pthread_t> receive_thread(n);

    ReceiveStateThreadArgument **rcv_thread_arg = new ReceiveStateThreadArgument*[n];
    int i = 0;
    for (auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        rcv_thread_arg[i] = new ReceiveStateThreadArgument;
        rcv_thread_arg[i]->p = this;
        rcv_thread_arg[i]->pid = it->first;
        rcv_thread_arg[i]->transaction_id = transaction_id_;
        rcv_thread_arg[i]->st = UNINITIALIZED;
        // if (pthread_create(&receive_thread[i], NULL, ReceiveStateFromParticipant, (void *)rcv_thread_arg[i])) {
        //     cout << "P" << get_pid() << ": ERROR: Unable to create receive thread for P" << get_pid() << endl;
        //     pthread_exit(NULL);
        // }
        i++;
    }
    
    void* status;
    i = 0;
    for (auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        pthread_join(receive_thread[i], &status);
        if ((rcv_thread_arg[i]->received_msg_type) == ERROR) {
            //TODO: not necessarily. handle
            pthread_exit(NULL);
        } else if ((rcv_thread_arg[i]->received_msg_type) == TIMEOUT) {
            // if a participant votes no, mark its state as PROCESSTIMEOUT
            it->second = PROCESSTIMEOUT;
        }
        else {
            it->second = rcv_thread_arg[i]->st;
        }

        i++;
    }
}

// send PRE-COMMIT to all participants
void Process::SendPreCommitToAll() {
    string msg;
    ConstructGeneralMsg(kPreCommit, transaction_id_, msg);
    for ( auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        // if ((it->first) == get_pid()) continue; // do not send to self

        if (send(get_fd(it->first), msg.c_str(), msg.size(), 0) == -1) {
            cout << "P" << get_pid() << ": ERROR: sending to P" << (it->first) << endl;
        }
        else {
            cout << "P" << get_pid() << ": Msg sent to P" << (it->first) << ": " << msg << endl;
        }
    }
}

void Process::SendPreCommitToProcess(int process_id) {
    string msg;
    ConstructGeneralMsg(kPreCommit, transaction_id_, msg);

    if (send(get_fd(process_id), msg.c_str(), msg.size(), 0) == -1) {
        cout << "P" << get_pid() << ": ERROR: sending to P" << process_id << endl;
    }
    else {
        cout << "P" << get_pid() << ": Msg sent to P" << process_id << ": " << msg << endl;
    }
}


// send COMMIT to all participants
void Process::SendCommitToAll() {
    string msg;
    ConstructGeneralMsg(kCommit, transaction_id_, msg);
    for ( auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        // if ((it->first) == get_pid()) continue; // do not send to self

        if (send(get_fd(it->first), msg.c_str(), msg.size(), 0) == -1) {
            cout << "P" << get_pid() << ": ERROR: sending to P" << (it->first) << endl;
        }
        else {
            cout << "P" << get_pid() << ": Msg sent to P" << (it->first) << ": " << msg << endl;
        }
    }
}

// thread for receiving vote from ONE participant
// sets the participant's state in participant_state_map_
void* ReceiveVoteFromParticipant(void* _rcv_thread_arg) {
    ReceiveThreadArgument *rcv_thread_arg = (ReceiveThreadArgument *)_rcv_thread_arg;
    int pid = rcv_thread_arg->pid;
    int tid = rcv_thread_arg->transaction_id;
    string msg1 = rcv_thread_arg->expected_msg1;    //YES vote
    string msg2 = rcv_thread_arg->expected_msg2;    //NO vote
    Process *p = rcv_thread_arg->p;
    
    char buf[kMaxDataSize];
    int num_bytes;
    //TODO: write code to extract multiple messages

    fd_set temp_set;
    FD_ZERO(&temp_set);
    FD_SET(p->get_fd(pid), &temp_set);
    int fd_max = p->get_fd(pid);
    int rv;
    rv = select(fd_max + 1, &temp_set, NULL, NULL, (timeval*)&kTimeout);
    if (rv == -1) { //error in select
        cout << "P" << p->get_pid() << ": ERROR in select() for P" << pid << endl;
        rcv_thread_arg->received_msg_type = ERROR;
    } else if (rv == 0) {   //timeout
        rcv_thread_arg->received_msg_type = TIMEOUT;
    } else {    // activity happened on the socket
        if ((num_bytes = recv(p->get_fd(pid), buf, kMaxDataSize - 1, 0)) == -1) {
            cout << "P" << p->get_pid() << ": ERROR in receiving for P" << pid << endl;
            rcv_thread_arg->received_msg_type = ERROR;
        } else if (num_bytes == 0) {     //connection closed
            cout << "P" << p->get_pid() << ": Connection closed by P" << pid << endl;        
            // if participant closes connection, it is equivalent to it crashing
            // can treat it as TIMEOUT
            // TODO: verify argument
            rcv_thread_arg->received_msg_type = TIMEOUT;
            //TODO: handle connection close based on different cases
        } else {
            buf[num_bytes] = '\0';
            cout << "P" << p->get_pid() << ": Msg received from P" << pid << ": " << buf <<  endl;

            string extracted_msg;
            int received_tid;
            // in this case, we don't care about the received_tid,
            // because it will surely be for the transaction under consideration
            p->ExtractMsg(string(buf), extracted_msg, received_tid);

            if (extracted_msg == msg1) {    // it's a YES
                rcv_thread_arg->received_msg_type = YES;
            } else if (extracted_msg == msg2) { // it's a NO
                rcv_thread_arg->received_msg_type = NO;
            } else {
                //TODO: take actions appropriately, like check log for previous transaction decision.
                cout << "P" << p->get_pid() << ": Unexpected msg received from P" << pid << endl;
                rcv_thread_arg->received_msg_type = ERROR;
            }
        }
    }
    cout << "P" << p->get_pid() << ": Receive thread exiting for P" << pid << endl;
    return NULL;
}

// thread for receiving ACK messages from ONE participant
void* ReceiveAckFromParticipant(void* _rcv_thread_arg) {
    ReceiveThreadArgument *rcv_thread_arg = (ReceiveThreadArgument *)_rcv_thread_arg;
    int pid = rcv_thread_arg->pid;
    int tid = rcv_thread_arg->transaction_id;
    string msg1 = rcv_thread_arg->expected_msg1;    //ACK
    string msg2 = rcv_thread_arg->expected_msg2;    //NULL
    Process *p = rcv_thread_arg->p;

    char buf[kMaxDataSize];
    int num_bytes;
    //TODO: write code to extract multiple messages

    fd_set temp_set;
    FD_ZERO(&temp_set);
    FD_SET(p->get_fd(pid), &temp_set);
    int fd_max = p->get_fd(pid);
    int rv;
    rv = select(fd_max + 1, &temp_set, NULL, NULL, (timeval*)&kTimeout);
    if (rv == -1) { //error in select
        cout << "P" << p->get_pid() << ": ERROR in select() for P" << pid << endl;
        rcv_thread_arg->received_msg_type = ERROR;
    } else if (rv == 0) {   //timeout
        rcv_thread_arg->received_msg_type = TIMEOUT;
    } else {    // activity happened on the socket
        if ((num_bytes = recv(p->get_fd(pid), buf, kMaxDataSize - 1, 0)) == -1) {
            cout << "P" << p->get_pid() << ": ERROR in receiving for P" << pid << endl;
            rcv_thread_arg->received_msg_type = ERROR;
        } else if (num_bytes == 0) {     //connection closed
            cout << "P" << p->get_pid() << ": Connection closed by P" << pid << endl;
            // if participant closes connection, it is equivalent to it crashing
            // can treat it as TIMEOUT
            // TODO: verify argument
            rcv_thread_arg->received_msg_type = TIMEOUT;
            //TODO: handle connection close based on different cases
        } else {
            buf[num_bytes] = '\0';
            cout << "P" << p->get_pid() << ": Msg received from P" << pid << ": " << buf <<  endl;

            string extracted_msg;
            int received_tid;
            // in this case, we don't care about the received_tid,
            // because it will surely be for the transaction under consideration
            p->ExtractMsg(string(buf), extracted_msg, received_tid);

            if (extracted_msg == msg1) {    // it's an ACK
                rcv_thread_arg->received_msg_type = ACK;
            } else {
                //TODO: take actions appropriately, like check log for previous transaction decision.
                cout << "P" << p->get_pid() << ": Unexpected msg received from P" << pid << endl;
                rcv_thread_arg->received_msg_type = ERROR;
            }
        }
    }
    cout << "P" << p->get_pid() << ": Receive thread exiting for P" << pid << endl;
    return NULL;
}

void* ReceiveStateFromParticipant(void* _rcv_thread_arg) {
    ReceiveStateThreadArgument *rcv_thread_arg = (ReceiveStateThreadArgument *)_rcv_thread_arg;
    int pid = rcv_thread_arg->pid;
    int tid = rcv_thread_arg->transaction_id;
    Process *p = rcv_thread_arg->p;

    char buf[kMaxDataSize];
    int num_bytes;
    //TODO: write code to extract multiple messages

    fd_set temp_set;
    FD_ZERO(&temp_set);
    FD_SET(p->get_fd(pid), &temp_set);
    int fd_max = p->get_fd(pid);
    int rv;
    rv = select(fd_max + 1, &temp_set, NULL, NULL, (timeval*)&kTimeout);
    if (rv == -1) { //error in select
        cout << "P" << p->get_pid() << ": ERROR in select() for P" << pid << endl;
        rcv_thread_arg->received_msg_type = ERROR;
    } else if (rv == 0) {   //timeout
        rcv_thread_arg->received_msg_type = TIMEOUT;
    } else {    // activity happened on the socket
        if ((num_bytes = recv(p->get_fd(pid), buf, kMaxDataSize - 1, 0)) == -1) {
            cout << "P" << p->get_pid() << ": ERROR in receiving for P" << pid << endl;
            rcv_thread_arg->received_msg_type = ERROR;
        } else if (num_bytes == 0) {     //connection closed
            cout << "P" << p->get_pid() << ": Connection closed by P" << pid << endl;        
            // if participant closes connection, it is equivalent to it crashing
            // can treat it as TIMEOUT
            // TODO: verify argument
            rcv_thread_arg->received_msg_type = TIMEOUT;
            //TODO: handle connection close based on different cases
        } else {
            buf[num_bytes] = '\0';
            cout << "P" << p->get_pid() << ": Msg received from P" << pid << ": " << buf <<  endl;

            string extracted_msg;
            int received_tid;
            // in this case, we don't care about the received_tid,
            // because it will surely be for the transaction under consideration
            p->ExtractMsg(string(buf), extracted_msg, received_tid);

            ProcessState msg = static_cast<ProcessState>(atoi(extracted_msg.c_str()));
            rcv_thread_arg->st = msg;
            //assumes that correct message type is sent by participant

            // if (msg==)  {    // it's a YES
            //     received_msg_type = YES;
            // } else if (extracted_msg == msg2) { // it's a NO
            //     received_msg_type = NO;
            // } else {
            //     //TODO: take actions appropriately, like check log for previous transaction decision.
            //     cout << "P" << p->get_pid() << ": Unexpected msg received from P" << pid << endl;
            //     received_msg_type = ERROR;
            // }
        }
    }
    cout << "P" << p->get_pid() << ": Receive thread exiting for P" << pid << endl;
    return NULL;
}

// Function for a process which behaves as a normal coordinator
// normal coordinator means one who has been elected by the Controller
// and not suffered any failure
// or the result of an election protocol
void Process::CoordinatorMode() {
    //TODO: find a better way to set coordinator
    set_my_coordinator(0);
    //TODO: handle transaction IDs
    //TODO: increment it
    //TODO: send it to ConstructVoteReq;

    // connect to each participant
    for (int i = 0; i < N; ++i) {
        if (i == get_pid()) continue;
        if (ConnectToProcess(i)) {
            participant_state_map_.insert(make_pair(i, UNINITIALIZED));
            // setup alive connection to this process
            // if (ConnectToProcessAlive(i)) {
            //     up_.insert(i);
            // } else {
            //     // Practically, this should not happen, since it just connected to i.
            //     // TODO: not handling this rare case presently
            //     // this causes up_ to deviate from participant_state_map_ at the beginning
            //     cout << "P" << get_pid() << ": Unable to connect ALIVE to P" << i << endl;
            // }
        }
    }
    // usleep(kGeneralSleep); //sleep to make sure connections are established

    // pthread_t send_alive_thread, receive_alive_thread;
    // CreateAliveThreads(receive_alive_thread, send_alive_thread);

    string msg;
    ConstructVoteReq(msg);
    SendVoteReqToAll(msg);
    LogStart();
    WaitForVotes();

    string trans = get_transaction(transaction_id_);
    //TODO: Handle case when trans = "NULL". See also ConstructVoteReq same cases
    Vote(trans); //coordinator's self vote
    // iterate through the states of all processes
    bool abort = false;
    for (const auto& ps : participant_state_map_) {
        if (ps.second == PROCESSTIMEOUT || ps.second == ABORTED) {
            abort = true;
            break;
        }
    }

    if (my_state_ == ABORTED)
        abort = true;

    if (abort) {
        LogAbort();
        // send ABORT message to all participants which voted YES
        for (const auto& ps : participant_state_map_) {
            if (ps.second == UNCERTAIN) {
                //TODO: make sure that transaction_id_ is handled correct after TODO at start of this fn
                SendAbortToProcess(ps.first);
            }
        }
    } else {
        LogPreCommit();
        SendPreCommitToAll();
        WaitForAck();
        LogCommit();
        SendCommitToAll();
    }
}

void Process::NewCoordinatorMode() {    
    //TODO: send tid to ConstructVoteReq;

    // connect to each participant
    participant_state_map_.clear();
    //set participant state map but only those processes that are alive
    for (auto it = up_.begin(); it!=up_.end(); it++) {
        if (*it == get_pid()) continue;
        if (ConnectToProcess(*it))
            participant_state_map_.insert(make_pair(*it, UNINITIALIZED));
    }

    string msg;
    ConstructStateReq(msg);
    SendStateReqToAll(msg);
    WaitForStates();

    // iterate through the states of all processes
    // bool abort = false, committed = false, commit = false, ;
    //ProcessState key = ABORTED;
    auto it = participant_state_map_.find(ABORTED);
    if (my_state_ == ABORTED || it!=participant_state_map_.end())
    {
        if (my_state_ != ABORTED)
        {
            LogAbort();
                   //only log if other process is abort. 
                //if i know aborted, means already in log abort
            my_state_ = ABORTED;
        }

        for (const auto& ps : participant_state_map_) {
            SendAbortToProcess(ps.first);
        }

        return;
    }
    
    // key = COMMITTED;
    it = participant_state_map_.find(COMMITTED);
    if (my_state_ == COMMITTED || it!=participant_state_map_.end())
    {
        if (my_state_ != COMMITTED)
        {
            LogCommit();
            my_state_ = COMMITTED;
        }
        SendCommitToAll();
        return;
    }


    bool uncert = true;
    for (const auto& ps : participant_state_map_) {
        if(ps.second!=UNCERTAIN)
        {
            uncert = false;
            break;
        }
    }
    if(uncert && my_state_==UNCERTAIN)
    {
        LogAbort();
        for (const auto& ps : participant_state_map_) {
            SendAbortToProcess(ps.first);
        }
        return;
    }

    //else
    //some are commitable
    LogPreCommit();
    for (const auto& ps : participant_state_map_) {
        if(ps.second == UNCERTAIN)
            SendPreCommitToProcess(ps.first);
    }
    WaitForAck();
    LogCommit();
    SendCommitToAll();
    return;
}