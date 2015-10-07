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

extern ReceivedMsgType received_msg_type;

// extracts the transaction msg from the VOTE-REQ
// sets transaction_msg to the extracted transaction msg above
// sets the transaction_id_ variable
// populates participants_ vector
// DOES NOT populate up_ vector
// returns true if VOTE-REQ is received
bool Process::ExtractFromVoteReq(const string &msg, string &transaction_msg ) {
    bool ret;
    std::istringstream iss(msg);
    string extracted_msg;
    iss >> extracted_msg;
    if (extracted_msg == kVoteReq) {    // it's a VOTE-REQ
        ret = true;
    } else  { // it's something else
        //TODO: take actions appropriately, like check log for previous transaction decision.
        cout << "P" << get_pid() << ": Unexpected msg received from P" << my_coordinator_ << endl;
        ret = false;
    }

    if (!ret) {
        return false;
    }

    int received_tid;
    iss >> received_tid;
    transaction_id_ = received_tid;

    string temp, transaction_type;
    iss >> transaction_type;
    transaction_msg = transaction_type + " ";
    if (transaction_type == kAdd) {
        iss >> temp;
        transaction_msg = transaction_msg + temp + " ";
        iss >> temp;
        transaction_msg = transaction_msg + temp;
    } else if (transaction_type == kRemove) {
        iss >> temp;
        transaction_msg = transaction_msg + temp;
    } else if (transaction_type == kEdit) {
        iss >> temp;
        transaction_msg = transaction_msg + temp + " ";
        iss >> temp;
        transaction_msg = transaction_msg + temp + " ";
        iss >> temp;
        transaction_msg = transaction_msg + temp;
    }

    int n, id;
    iss >> n;
    for (int i = 0; i < n; ++i) {
        iss >> id;
        participants_.insert(id);
    }
    return ret;
}

// participant waits for VOTE-REQ from my_coordinator_
// extracts the transaction msg from the VOTE-REQ
// sets transaction_msg to the extracted transaction msg above
// sets the transaction_id_ variable
// populates participants_ vector
// DOES NOT populate up_ vector
bool Process::WaitForVoteReq(string &transaction_msg) {
    int pid = my_coordinator_;
    bool ret;
    char buf[kMaxDataSize];
    int num_bytes;
    //TODO: write code to extract multiple messages

    fd_set temp_set;
    FD_ZERO(&temp_set);
    FD_SET(get_fd(pid), &temp_set);
    int fd_max = get_fd(pid);
    int rv;
    rv = select(fd_max + 1, &temp_set, NULL, NULL, (timeval*)&kTimeout);
    if (rv == -1) { //error in select
        cout << "P" << get_pid() << ": ERROR in select() for P" << pid << endl;
        pthread_exit(NULL);
    } else if (rv == 0) {   //timeout
        cout<<"TIMEOUT"<<endl;
        my_state_ = ABORTED;
    } else {    // activity happened on the socket
        if ((num_bytes = recv(get_fd(pid), buf, kMaxDataSize - 1, 0)) == -1) {
            cout << "P" << get_pid() << ": ERROR in receiving for P" << pid << endl;
            pthread_exit(NULL); //TODO: think about whether it should be exit or not
        } else if (num_bytes == 0) {     //connection closed
            cout << "P" << get_pid() << ": Connection closed by P" << pid << endl;
            // if coordinator closes connection, it is equivalent to coordinator crashing
            // can treat it as TIMEOUT
            // TODO: verify argument
            my_state_ = ABORTED;
            //TODO: handle connection close based on different cases
        } else {
            buf[num_bytes] = '\0';
            cout << "P" << get_pid() << ": Msg received from P" << pid << ": " << buf <<  endl;

            // in this case, we don't care about the received_tid,
            // because it will surely be for the transaction under consideration
            ret = ExtractFromVoteReq(string(buf), transaction_msg);
            //TODO: handle return bool
        }
    }
    cout << "P" << get_pid() << ": Receive thread exiting for P" << pid << endl;
    return ret;

}

// send <msg_to_send> to coordinator
void Process::SendMsgToCoordinator(const string &msg_to_send) {
    string msg;
    ConstructGeneralMsg(msg_to_send, transaction_id_, msg);
    if (send(get_fd(my_coordinator_), msg.c_str(), msg.size(), 0) == -1) {
        cout << "P" << get_pid() << ": ERROR: sending to P" << my_coordinator_ << endl;
    }
    else {
        cout << "P" << get_pid() << ": Msg sent to P" << my_coordinator_ << ": " << msg << endl;
    }
}

// waits for PRE-COMMIT or ABORT from coordinator
// on receipt, updates my_state_ variable
// on timeout, initiates termination protocol
void Process::ReceivePreCommitOrAbortFromCoordinator() {
    int pid = my_coordinator_;
    char buf[kMaxDataSize];
    int num_bytes;
    //TODO: write code to extract multiple messages

    fd_set temp_set;
    FD_ZERO(&temp_set);
    FD_SET(get_fd(pid), &temp_set);
    int fd_max = get_fd(pid);
    int rv;
    rv = select(fd_max + 1, &temp_set, NULL, NULL, (timeval*)&kTimeout);
    if (rv == -1) { //error in select
        cout << "P" << get_pid() << ": ERROR in select() for P" << pid << endl;
        pthread_exit(NULL);
    } else if (rv == 0) {   //timeout
        //TODO: initiate election protocol
    } else {    // activity happened on the socket
        if ((num_bytes = recv(get_fd(pid), buf, kMaxDataSize - 1, 0)) == -1) {
            cout << "P" << get_pid() << ": ERROR in receiving for P" << pid << endl;
            pthread_exit(NULL); //TODO: think about whether it should be exit or not
        } else if (num_bytes == 0) {     //connection closed
            cout << "P" << get_pid() << ": Connection closed by P" << pid << endl;
            // if coordinator closes connection, it is equivalent to coordinator crashing
            // can treat it as TIMEOUT
            // TODO: verify argument
            // TODO: need to initiate termination protocol
            // execute actions same as above if(rv==0) case
            //TODO: handle connection close based on different cases
        } else {
            buf[num_bytes] = '\0';
            cout << "P" << get_pid() << ": Msg received from P" << pid << ": " << buf <<  endl;

            string extracted_msg;
            int received_tid;
            // in this case, we don't care about the received_tid,
            // because it will surely be for the transaction under consideration
            ExtractMsg(string(buf), extracted_msg, received_tid);
            if (extracted_msg == kPreCommit && received_tid == transaction_id_) {
                // making sure msg is for the curr transaction and not a future one
                my_state_ = COMMITTABLE;
            } else if (extracted_msg == kAbort && received_tid == transaction_id_) {
                my_state_ = ABORTED;
            } else {
                //TODO: take actions appropriately, like check log for previous transaction decision.
                cout << "P" << get_pid() << ": Unexpected msg received from P" << pid << endl;
            }
        }
    }
    cout << "P" << get_pid() << ": Receive thread exiting for P" << pid << endl;
}

// Waits for COMMIT from coordinator
// on timeout, initiates termination protocol
void Process::ReceiveCommitFromCoordinator() {
    int pid = my_coordinator_;
    char buf[kMaxDataSize];
    int num_bytes;
    //TODO: write code to extract multiple messages

    fd_set temp_set;
    FD_ZERO(&temp_set);
    FD_SET(get_fd(pid), &temp_set);
    int fd_max = get_fd(pid);
    int rv;
    rv = select(fd_max + 1, &temp_set, NULL, NULL, (timeval*)&kTimeout);
    if (rv == -1) { //error in select
        cout << "P" << get_pid() << ": ERROR in select() for P" << pid << endl;
        pthread_exit(NULL);
    } else if (rv == 0) {   //timeout
        //TODO: initiate election protocol
    } else {    // activity happened on the socket
        if ((num_bytes = recv(get_fd(pid), buf, kMaxDataSize - 1, 0)) == -1) {
            cout << "P" << get_pid() << ": ERROR in receiving for P" << pid << endl;
            pthread_exit(NULL); //TODO: think about whether it should be exit or not
        } else if (num_bytes == 0) {     //connection closed
            cout << "P" << get_pid() << ": Connection closed by P" << pid << endl;
            // if coordinator closes connection, it is equivalent to coordinator crashing
            // can treat it as TIMEOUT
            // TODO: verify argument
            // TODO: need to initiate termination protocol
            // execute actions same as above if(rv==0) case
            //TODO: handle connection close based on different cases
        } else {
            buf[num_bytes] = '\0';
            cout << "P" << get_pid() << ": Msg received from P" << pid << ": " << buf <<  endl;

            string extracted_msg;
            int received_tid;
            // in this case, we don't care about the received_tid,
            // because it will surely be for the transaction under consideration
            ExtractMsg(string(buf), extracted_msg, received_tid);
            if (extracted_msg == kCommit && received_tid == transaction_id_) {
                // making sure msg is for the curr transaction and not a future one
                my_state_ = COMMITTED;
            } else {
                //TODO: take actions appropriately, like check log for previous transaction decision.
                cout << "P" << get_pid() << ": Unexpected msg received from P" << pid << endl;
            }
        }
    }
    cout << "P" << get_pid() << ": Receive thread exiting for P" << pid << endl;
}

// ALIVE connect to each process in participants_ list
// adds them to the UP set.
void Process::ConstructUpSet() {
    up_.insert(my_coordinator_);
    for (auto const &p : participants_) {
        if (p == get_pid()) continue;
        cout<<p<<endl;
        if (ConnectToProcessAlive(p)) {
            up_.insert(p);
        } else {
            //TODO: I don't think we need to do anything special
            // apart from not adding participant_[i] to the upset.
            cout << "P" << get_pid() << ": Unable to connect ALIVE to P" << p << endl;
        }
    }
    cout<<"END"<<endl;
}

// Function for a process which behaves as a normal participant
// normal participant means one who has not suffered a failure
void Process::ParticipantMode() {
    //TODO: find a better way to set coordinator
    set_my_coordinator(0);

    // connect to coordinator
    if (!ConnectToProcess(my_coordinator_)) {
        // unable to connect to coordinator
        // TODO: handle this case
        // TODO: start election protocol
    }

    // usleep(kGeneralSleep); //sleep to make sure connections are established

    string transaction_msg;
    if (!WaitForVoteReq(transaction_msg)) {
        // Some error happened in rcving VOTE REQ
        // TODO: check if special actions required
    } else { // VOTE-REQ received.
        // ConstructUpSet();

        // pthread_t send_alive_thread, receive_alive_thread;
        // CreateAliveThreads(receive_alive_thread, send_alive_thread);

        // usleep(kGeneralSleep); //sleep to make sure connections are established
    }

    if (my_state_ == ABORTED) {
        //TODO: write ABORT in log
    }
    Vote(transaction_msg);
    if (my_state_ ==  ABORTED) { //participant's vote was NO
        SendMsgToCoordinator(kNo);
        //TODO: write ABORT in log
    } else { //participant's vote was YES
        //TODO: write YES in log
        SendMsgToCoordinator(kYes);
        ReceivePreCommitOrAbortFromCoordinator();
        if (my_state_ == COMMITTABLE) { // coord sent PRE-COMMIT
            SendMsgToCoordinator(kAck);
            ReceiveCommitFromCoordinator();
            if (my_state_ == COMMITTED) {
                //TODO: write COMMIT in log
            }
            // TODO: might need to check other values of my_state_
            // because of results of termination protocol
        } else if (my_state_ == ABORTED) { // coord sent ABORT
            //TODO: write ABORT in log
        }
    }
}

//TODO: when participant recovers, make sure that it ignores STATE-REQ from new coord