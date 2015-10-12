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
        // cout << "P" << get_pid() << ": fd for" << it->first << "=" << get_fd(it->first) << endl;

        ContinueOrDie();
        if (send(get_fd(it->first), msg.c_str(), msg.size(), 0) == -1) {
            cout << "P" << get_pid() << ": ERROR: sending votereq to P" << (it->first) << endl;
            RemoveFromUpSet(it->first);
        }
        else {
            cout << "P" << get_pid() << ": Msg sent to P" << (it->first) << ": " << msg << endl;
        }
        DecrementNumMessages();
    }
}

void Process::SendStateReqToAll(const string &msg) {

    //this only contains operational processes for non timeout cases
    for ( auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        // if ((it->first) == get_pid()) continue; // do not send to self
        ContinueOrDie();
        if (send(get_sdr_fd(it->first), msg.c_str(), msg.size(), 0) == -1) {
            // cout << "P" << get_pid() << ": ERROR: sending state req to P" << (it->first) << endl;
            RemoveFromUpSet(it->first);
        }
        else {
            cout << "P" << get_pid() << ": Msg sent to P" << (it->first) << ": " << msg << endl;
        }
        DecrementNumMessages();
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
        CreateThread(receive_thread[i], ReceiveVoteFromParticipant, (void *)rcv_thread_arg[i]);
        i++;
    }

    void* status;

    i = 0;
    for (auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        pthread_join(receive_thread[i], &status);
        RemoveThreadFromSet(receive_thread[i]);
        if ((rcv_thread_arg[i]->received_msg_type) == ERROR) {
            cout<<"!!!!!!!!!!"<<endl;
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
        CreateThread(receive_thread[i], ReceiveAckFromParticipant, (void *)rcv_thread_arg[i]);
        i++;
    }

    void* status;
    i = 0;
    for (auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        pthread_join(receive_thread[i], &status);
        RemoveThreadFromSet(receive_thread[i]);
        if ((rcv_thread_arg[i]->received_msg_type) == ERROR) {
            cout<<"!!!!!!!!!!"<<endl;
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
        // rcv_thread_arg[i]->received_msg_type = UNINITIALIZED;

        CreateThread(receive_thread[i], ReceiveStateFromParticipant, (void *)rcv_thread_arg[i]);
        i++;
    }

    void* status;
    i = 0;
    for (auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        pthread_join(receive_thread[i], &status);
        RemoveThreadFromSet(receive_thread[i]);
        if ((rcv_thread_arg[i]->st) == UNINITIALIZED) {
            //TODO: not necessarily. handle
            //error
            // pthread_exit(NULL);
        }
        else {
            it->second = rcv_thread_arg[i]->st;

        }
        i++;
        // cout << "Set received state as " << it->second << "at " << (time(NULL) % 100) << endl;
    }
}

// send PRE-COMMIT to all participants
void Process::SendPreCommitToAll() {
    string msg;
    ConstructGeneralMsg(kPreCommit, transaction_id_, msg);
    for ( auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        // if ((it->first) == get_pid()) continue; // do not send to self
        ContinueOrDie();
        if (send(get_fd(it->first), msg.c_str(), msg.size(), 0) == -1) {
            // cout << "P" << get_pid() << ": ERROR: sending pc to all P" << (it->first) << endl;
            RemoveFromUpSet(it->first);
        }
        else {
            cout << "P" << get_pid() << ": Msg sent to P" << (it->first) << ": " << msg << endl;
        }
        DecrementNumMessages();
    }
}

void Process::SendPreCommitToProcess(int process_id) {
    string msg;
    ConstructGeneralMsg(kPreCommit, transaction_id_, msg);
    ContinueOrDie();
    if (send(get_fd(process_id), msg.c_str(), msg.size(), 0) == -1) {
        cout << "P" << get_pid() << ": ERROR: sending pc to P" << process_id << endl;
        RemoveFromUpSet(process_id);
    }
    else {
        cout << "P" << get_pid() << ": Msg sent to P" << process_id << ": " << msg << endl;
    }
    DecrementNumMessages();
}


// send COMMIT to all participants
void Process::SendCommitToAll() {
    string msg;
    ConstructGeneralMsg(kCommit, transaction_id_, msg);
    for ( auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        // if ((it->first) == get_pid()) continue; // do not send to self
        ContinueOrDie();
        if (send(get_fd(it->first), msg.c_str(), msg.size(), 0) == -1) {
            // cout << "P" << get_pid() << ": ERROR: sending c to all: to P" << (it->first) << endl;
            RemoveFromUpSet(it->first);
        }
        else {
            cout << "P" << get_pid() << ": Msg sent to P" << (it->first) << ": " << msg << endl;
        }
        DecrementNumMessages();
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
        // cout << "P" << p->get_pid() << ": ERROR in select() for P" << pid << strerror(errno)<<endl;
        rcv_thread_arg->received_msg_type = TIMEOUT;
        p->RemoveFromUpSet(pid);
    } else if (rv == 0) {   //timeout
        rcv_thread_arg->received_msg_type = TIMEOUT;
        p->RemoveFromUpSet(pid);
    } else {    // activity happened on the socket
        if ((num_bytes = recv(p->get_fd(pid), buf, kMaxDataSize - 1, 0)) == -1) {
            cout << "P" << p->get_pid() << ": ERROR in receiving for P" << pid << endl;
            rcv_thread_arg->received_msg_type = TIMEOUT;
            p->RemoveFromUpSet(pid);
        } else if (num_bytes == 0) {     //connection closed
            cout << "P" << p->get_pid() << ": Connection closed by P" << pid << endl;
            // if participant closes connection, it is equivalent to it crashing
            // can treat it as TIMEOUT
            // TODO: verify argument
            rcv_thread_arg->received_msg_type = TIMEOUT;
            p->RemoveFromUpSet(pid);
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
    // cout << "P" << p->get_pid() << ": Receive thread exiting for P" << pid << endl;
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
        // cout << "P" << p->get_pid() << ": ERROR in select() for P" << pid << strerror(errno)<< endl;
        rcv_thread_arg->received_msg_type = TIMEOUT;
        p->RemoveFromUpSet(pid);
    } else if (rv == 0) {   //timeout
        rcv_thread_arg->received_msg_type = TIMEOUT;
        p->RemoveFromUpSet(pid);
    } else {    // activity happened on the socket
        if ((num_bytes = recv(p->get_fd(pid), buf, kMaxDataSize - 1, 0)) == -1) {
            cout << "P" << p->get_pid() << ": ERROR in receiving for P" << pid << endl;
            rcv_thread_arg->received_msg_type = TIMEOUT;
            p->RemoveFromUpSet(pid);
        } else if (num_bytes == 0) {     //connection closed
            cout << "P" << p->get_pid() << ": Connection closed by P" << pid << endl;
            // if participant closes connection, it is equivalent to it crashing
            // can treat it as TIMEOUT
            // TODO: verify argument
            rcv_thread_arg->received_msg_type = TIMEOUT;
            p->RemoveFromUpSet(pid);
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
                cout << "P" << p->get_pid() << ": Unexpected msg received from P" << pid << buf << endl;
                rcv_thread_arg->received_msg_type = ERROR;
            }
        }
    }
    // cout << "P" << p->get_pid() << ": Receive thread exiting for P" << pid << endl;
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
        // cout << "P" << p->get_pid() << ": ERROR in select() for P" << pid << strerror(errno)<< endl;
        rcv_thread_arg->st = PROCESSTIMEOUT;
        p->RemoveFromUpSet(pid);
    } else if (rv == 0) {   //timeout
        rcv_thread_arg->st = PROCESSTIMEOUT;
        p->RemoveFromUpSet(pid);
    } else {    // activity happened on the socket
        if ((num_bytes = recv(p->get_fd(pid), buf, kMaxDataSize - 1, 0)) == -1) {
            cout << "P" << p->get_pid() << ": ERROR in receiving for P" << pid << endl;
            rcv_thread_arg->st = PROCESSTIMEOUT;
            p->RemoveFromUpSet(pid);
        } else if (num_bytes == 0) {     //connection closed
            cout << "P" << p->get_pid() << ": Connection closed by P" << pid << endl;
            // if participant closes connection, it is equivalent to it crashing
            // can treat it as TIMEOUT
            // TODO: verify argument
            rcv_thread_arg->st = PROCESSTIMEOUT;
            p->RemoveFromUpSet(pid);
            //TODO: handle connection close based on different cases
        } else {
            buf[num_bytes] = '\0';
            cout << "P" << p->get_pid() << ": State Msg received from P" << pid << ": " << buf <<  endl;

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
    // cout << "P" << p->get_pid() << ": Receive thread exiting for P" << pid << endl;
    return NULL;
}

void Process::WaitForInit3PC() {
    while (get_handshake() != INIT3PC) {
        usleep(kMiniSleep);
    }
}
// Function for a process which behaves as a normal coordinator
// normal coordinator means one who has been elected by the Controller
// and not suffered any failure
// or the result of an election protocol
void Process::CoordinatorMode() {
    //TODO: find a better way to set coordinator
    // set_my_coordinator(0);
    WaitForInit3PC();
    //TODO: handle transaction IDs
    //TODO: increment it
    //TODO: send it to ConstructVoteReq;

    for (const auto &pm : participant_state_map_) {
        if (pm.first == get_pid()) continue;
        if (ConnectToProcess(pm.first))
        {   // setup alive connection to this process
            if (ConnectToProcessAlive(pm.first))
            {
                if (ConnectToProcessSDR(pm.first))
                {
                    if (ConnectToProcessUp(pm.first)) {
                        up_.insert(pm.first);
                    } else {
                        cout << "P" << get_pid() << ": Unable to connect UP to P" << pm.first << endl;
                    }
                } else {
                    cout << "P" << get_pid() << ": Unable to connect SDR to P" << pm.first << endl;
                }
            }
            else {
                // Practically, this should not happen, since it just connected to i.
                // TODO: not handling this rare case presently
                // this causes up_ to deviate from participant_state_map_ at the beginning
                cout << "P" << get_pid() << ": Unable to connect ALIVE to P" << pm.first << endl;
            }
        } else {
            cout << "P" << get_pid() << ": Unable to connect to P" << pm.first << endl;
        }
    }
    usleep(kGeneralSleep); //sleep to make sure connections are established

    // create SDR threads only for the first transaction
    // because they will keep on running forever.
    if (transaction_id_ == 0) {
        // one sdr receive thread for each participant, not just those in up_
        // because any participant could ask for Dec Req in future.
        // size = participant_state_map_.size() because it does not contain self
        vector<pthread_t> sdr_receive_threads(participant_state_map_.size());
        vector<pthread_t> up_receive_threads(participant_state_map_.size());
        int i = 0;
        for (auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it) {
            //make sure you don't create a SDR receive thread for self
            if (it->first == get_pid()) continue;
            CreateSDRThread(it->first, sdr_receive_threads[i]);
            CreateUpThread(it->first, sdr_receive_threads[i]);
            i++;
        }
    }

    string msg;
    ConstructVoteReq(msg);
    LogStart();
    LogUp();
    SendVoteReqToAll(msg);

    pthread_t send_alive_thread;
    vector<pthread_t> receive_alive_threads(up_.size());

    CreateAliveThreads(receive_alive_threads, send_alive_thread);

    WaitForVotes();
    string trans = get_transaction(transaction_id_);
    //TODO: Handle case when trans = "NULL". See also ConstructVoteReq same cases
    Vote(trans); //coordinator's self vote

    bool abort = false;
    for (const auto& ps : participant_state_map_) {
        if (ps.second == PROCESSTIMEOUT || ps.second == ABORTED) {
            abort = true;
            break;
        }
    }
    if (get_my_state() == ABORTED)
        abort = true;

    if (abort) {
        LogAbort();
        set_my_state(ABORTED);

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
        // return;
        LogCommit();
        set_my_state(COMMITTED);
        SendCommitToAll();
    }

    if (get_my_state() == ABORTED)
        prev_decisions_.push_back(ABORT);
    else
        prev_decisions_.push_back(COMMIT);
}

void* NewCoordinatorMode(void * _p) {
    //TODO: send tid to ConstructVoteReq;
    Process *p = (Process *)_p;
    ofstream outf("log/newcoord" + to_string(p->get_pid()) + "," + to_string(time(NULL) % 100), fstream::app);
    outf << "NewCoordSet" << p->get_pid() << endl;

    // connect to each participant
    p->participant_state_map_.clear();
    //set participant state map but only those processes that are alive
    for (auto it = p->up_.begin(); it != p->up_.end(); it++) {
        if (*it == p->get_pid()) continue;
        if (p->ConnectToProcess(*it))
            p->participant_state_map_.insert(make_pair(*it, UNINITIALIZED));
        // else
            // cout << "P" << p->get_pid() << ": Unable to connect to P" << *it << endl;
    }
    // return NULL;
    string msg;
    p->ConstructStateReq(msg);

    p->SendStateReqToAll(msg);
    outf << "sent state req" << endl;
    p->WaitForStates();
    ProcessState my_st = p->get_my_state();

    // if(p->get_pid() == 1) return NULL;

    bool aborted = false;
    for (const auto& ps : p->participant_state_map_) {
        if (ps.second == ABORTED)
        {
            aborted = true;
            break;
        }
    }

    if (my_st == ABORTED || aborted)
    {
        if (my_st != ABORTED)
        {
            p->LogAbort();
            //only log if other process is abort.
            //if i know aborted, means already in log abort
            my_st = ABORTED;

        }

        for (const auto& ps : p->participant_state_map_) {
            p->SendAbortToProcess(ps.first);
        }
        p->set_my_state(ABORTED);
        p->RemoveThreadFromSet(pthread_self());
        return NULL;
    }

    bool committed = false;
    for (const auto& ps : p->participant_state_map_) {
        if (ps.second == COMMITTED)
        {
            committed = true;
            break;
        }
    }

    if (my_st == COMMITTED || committed)
    {
        if (my_st != COMMITTED)
        {
            p->LogCommit();
            my_st = COMMITTED;
        }
        p->SendCommitToAll();
        p->set_my_state(COMMITTED);
        p->RemoveThreadFromSet(pthread_self());
        return NULL;
    }


    bool uncert = true;
    for (const auto& ps : p->participant_state_map_) {
        if (ps.second == PROCESSTIMEOUT)continue;
        if (ps.second != UNCERTAIN)
        {
            uncert = false;
            break;
        }
    }
    if (uncert && my_st == UNCERTAIN)
    {
        outf << "sending abort" <<  "at " << time(NULL) % 100 << endl;
        p->LogAbort();
        for (const auto& ps : p->participant_state_map_) {
            p->SendAbortToProcess(ps.first);
        }
        p->set_my_state(ABORTED);
        p->RemoveThreadFromSet(pthread_self());
        return NULL;
    }

    //else
    //some are commitable
    p->LogPreCommit();
    outf << "sending precommit at " << time(NULL) % 100 << endl;
    for (const auto& ps : p->participant_state_map_) {
        p->SendPreCommitToProcess(ps.first);
    }
    p->WaitForAck();
    p->LogCommit();
    p->set_my_state(COMMITTED);
    p->SendCommitToAll();
    outf << "sent commit " << "at " << time(NULL) % 100 << endl;
    if (my_st == ABORTED)
        p->prev_decisions_.push_back(ABORT);
    else
        p->prev_decisions_.push_back(COMMIT);

    p->RemoveThreadFromSet(pthread_self());
    return NULL;
}