#include "process.h"
#include "constants.h"
#include "fstream"
#include "sstream"
#include "iostream"
#include "unistd.h"
#include "limits.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include "sys/time.h"
#include "sstream"
using namespace std;

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
        cout << "P" << get_pid() << ": Unexpected msg received from P" << get_my_coordinator() << endl;
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
    int pid = get_my_coordinator();
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
        // pthread_exit(NULL);
    } else if (rv == 0) {   //timeout
        cout << "TIMEOUT" << endl;
        my_state_ = ABORTED;
        RemoveFromUpSet(pid);

    } else {    // activity happened on the socket
        // cout << "P" << get_pid() << ": fd for" << pid << "=" << get_fd(pid) << endl;

        if ((num_bytes = recv(get_fd(pid), buf, kMaxDataSize - 1, 0)) == -1) {
            cout << "P" << get_pid() << ": ERROR in receiving for P" << pid << endl;
            RemoveFromUpSet(pid);
            // pthread_exit(NULL); //TODO: think about whether it should be exit or not
        } else if (num_bytes == 0) {     //connection closed
            cout << "P" << get_pid() << ": Connection closed by P" << pid << endl;
            // if coordinator closes connection, it is equivalent to coordinator crashing
            // can treat it as TIMEOUT
            // TODO: verify argument
            my_state_ = ABORTED;
            RemoveFromUpSet(pid);
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
    // cout << "P" << get_pid() << ": Receive thread exiting for P" << pid << endl;
    return ret;

}

// send <msg_to_send> to coordinator
void Process::SendMsgToCoordinator(const string &msg_to_send) {
    string msg;
    ConstructGeneralMsg(msg_to_send, transaction_id_, msg);
    int mc = get_my_coordinator();
    if (mc == INT_MAX)
        return;
    ContinueOrDie();
    if (send(get_fd(mc), msg.c_str(), msg.size(), 0) == -1) {
        cout << "P" << get_pid() << ": ERROR: sending msg to coord to P" << mc << endl;
        RemoveFromUpSet(my_coordinator_);
    }
    else {
        cout << "P" << get_pid() << ": Msg sent to P" << mc << ": " << msg << endl;
    }
    DecrementNumMessages();
}

// waits for PRE-COMMIT or ABORT from coordinator
// on receipt, updates my_state_ variable
// on timeout, initiates termination protocol
void Process::ReceivePreCommitOrAbortFromCoordinator() {
    int pid = get_my_coordinator();
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
        // pthread_exit(NULL);
    } else if (rv == 0) {
        //timeout
        RemoveFromUpSet(pid);
        //do i need to set somethign here
    } else {    // activity happened on the socket
        if ((num_bytes = recv(get_fd(pid), buf, kMaxDataSize - 1, 0)) == -1) {
            cout << "P" << get_pid() << ": ERROR in receiving for P" << pid << endl;
            RemoveFromUpSet(pid);
        } else if (num_bytes == 0) {     //connection closed
            cout << "P" << get_pid() << ": Connection closed by P" << pid << endl;
            // if coordinator closes connection, it is equivalent to coordinator crashing
            // can treat it as TIMEOUT
            RemoveFromUpSet(pid);
            // TODO: verify argument
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
    // cout << "P" << get_pid() << ": Receive thread exiting for P" << pid << endl;
}

// waits for PRE-COMMIT or ABORT or COMMIT from coordinator
// on receipt, updates my_state_ variable
// on timeout, initiates termination protocol
void Process::ReceiveAnythingFromCoordinator() {
    int pid = get_my_coordinator();
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
        // pthread_exit(NULL);
    } else if (rv == 0) {
        //timeout
        RemoveFromUpSet(pid);
        //do i need to set somethign here
    } else {    // activity happened on the socket
        if ((num_bytes = recv(get_fd(pid), buf, kMaxDataSize - 1, 0)) == -1) {
            cout << "P" << get_pid() << ": ERROR in receiving for P" << pid << endl;
            RemoveFromUpSet(pid);
        } else if (num_bytes == 0) {     //connection closed
            cout << "P" << get_pid() << ": Connection closed by P" << pid << endl;
            // if coordinator closes connection, it is equivalent to coordinator crashing
            // can treat it as TIMEOUT
            RemoveFromUpSet(pid);
            // TODO: verify argument
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
            } else  if (extracted_msg == kCommit && received_tid == transaction_id_) {
                my_state_ = COMMITTED;
            } else {
                //TODO: take actions appropriately, like check log for previous transaction decision.
                cout << "P" << get_pid() << ": Unexpected msg received from P" << pid << endl;
            }
        }
    }
    // cout << "P" << get_pid() << ": Receive thread exiting for P" << pid << endl;
}

// Waits for COMMIT from coordinator
// on timeout, initiates termination protocol
void Process::ReceiveCommitFromCoordinator() {
    int pid = get_my_coordinator();
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
        // pthread_exit(NULL);
    } else if (rv == 0) {
        //timeout
        RemoveFromUpSet(pid);
    } else {    // activity happened on the socket
        if ((num_bytes = recv(get_fd(pid), buf, kMaxDataSize - 1, 0)) == -1) {
            cout << "P" << get_pid() << ": ERROR in receiving for P" << pid << endl;
            RemoveFromUpSet(pid);
        } else if (num_bytes == 0) {     //connection closed
            cout << "P" << get_pid() << ": Connection closed by P" << pid << endl;
            RemoveFromUpSet(pid);
            // if coordinator closes connection, it is equivalent to coordinator crashing
            // can treat it as TIMEOUT
            // TODO: verify argument
            // execute actions same as above if(rv==0) case
            //TODO: handle connection close based on different cases
        } else {
            buf[num_bytes] = '\0';
            cout << "P" << get_pid() << ": Msg received from P" << pid << ": " << buf <<  "at " << time(NULL) % 100 << endl;

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
    // cout << "P" << get_pid() << ": Receive thread exiting for P" << pid << endl;
}

// ALIVE connect to each process in participants_ list
// adds them to the UP set.
void Process::ConstructUpSet() {
    up_.insert(get_my_coordinator());
    for (auto const &p : participants_) {
        if (p == get_pid()) continue;
        // cout<<p<<endl;
        // ConnectToProcess(p);
        if (ConnectToProcessAlive(p)) {
            if(ConnectToProcessSDR(p)){
                if(ConnectToProcessUp(p)){
                    up_.insert(p);
                }
                else 
                    cout << "P" << get_pid() << ": Unable to connect Up to P" << p << endl;
            }
            else{
                cout << "P" << get_pid() << ": Unable to connect SDR to P" << p << endl;
            }
        } else {
            cout << "P" << get_pid() << ": Unable to connect ALIVE to P" << p << endl;
        }
    }
}

// Function for a process which behaves as a normal participant
// normal participant means one who has not suffered a failure
void Process::ParticipantMode() {

    //create SR thread here

    //TODO: find a better way to set coordinator
    // set_my_coordinator(0);

    // connect to coordinator
    // if (!ConnectToProcess(my_coordinator_)) {
    //     my_coordinator_ = -1;
    //     // unable to connect to coordinator
    //     // (is it really required to )start election protocol
    // }

    usleep(kGeneralSleep); //sleep to make sure connections are established

    string transaction_msg;
    if (!WaitForVoteReq(transaction_msg)) {
        // Some error happened in rcving VOTE REQ
        // TODO: check if special actions required
    } else { // VOTE-REQ received.
        ConstructUpSet();

        pthread_t send_alive_thread;
        vector<pthread_t> receive_alive_threads(up_.size());
        CreateAliveThreads(receive_alive_threads, send_alive_thread);

        // create SDR threads only for the first transaction
        // because they will keep on running forever.
        if (transaction_id_ == 0) {
            // one sdr receive thread for each participant, not just those in up_
            // because any participant could ask for Dec Req in future.
            // size = participant_.size()-1 because it contains self
            // size + 1 for coordinator
            vector<pthread_t> sdr_receive_threads(participants_.size());
            vector<pthread_t> up_receive_threads(participants_.size());
            int i = 0;
            for (auto it = participants_.begin(); it != participants_.end(); ++it) {
                //make sure you don't create a SDR receive thread for self
                if (*it == get_pid()) continue;
                CreateSDRThread(*it, sdr_receive_threads[i]);
                CreateUpThread(*it, up_receive_threads[i]);
                i++;
            }
            CreateSDRThread(get_my_coordinator(), sdr_receive_threads[i]);
            CreateUpThread(get_my_coordinator(), up_receive_threads[i]);
        }
    }

    if (my_state_ == ABORTED) {
        //ignore log as doesnt matter when participant doesnt get votereq
        return;
    }

    //else
    LogVoteReq();
    LogUp();
    Vote(transaction_msg);
    // Print();

    if (my_state_ ==  ABORTED)
    {   //participant's vote was NO
        SendMsgToCoordinator(kNo);
        LogAbort();
    }
    else
    {   //participant's vote was YES
        LogYes();
        SendMsgToCoordinator(kYes);
        ReceivePreCommitOrAbortFromCoordinator();

        if (my_state_ == COMMITTABLE)
        {   // coord sent PRE-COMMIT
            LogPreCommit();
            SendMsgToCoordinator(kAck);
            // cout<<pid_<<" sent ack to coord at "<<time(NULL)%100<<endl;
            ReceiveCommitFromCoordinator();
            //this detects timeout, exits and state will be the same as intial
            if (my_state_ == COMMITTED)
            {
                LogCommit();
            }
            else
            {
                RemoveFromUpSet(get_my_coordinator());
                Timeout();

            }
        }

        else if (my_state_ == ABORTED)
        {   // coord sent ABORT
            LogAbort();
        }

        else
        {
            RemoveFromUpSet(get_my_coordinator());
            Timeout();
            // Print();

        }
        // TODO: might need to check other values of my_state_
        // because of results of termination protocol
    }

    //participant_termination_protocol
    //wait till a) new_coord_thread exits (wait it will aslo exit when someone
    //                                     else becomes new coord) or
    //          b) till new coord sends me dec
    //      i can just wait till b if recv dec is handled by someone


//TODO: when participant recovers, make sure that it ignores STATE-REQ from new coord
    while (!(my_state_ == ABORTED || my_state_ == COMMITTED))
    {
        //what does this thread do while timeout() waiting for SR thread to respond.
        //maybe i can just do, wait till a decision is made by SR thread
        //know with the use of a shared variable
        //waiting blocking

        //if this participant is new coord, then it will have waited there to get a decision
        //else, we have to log abort or commit in SR thread receiving part
        usleep(kGeneralSleep);
    }
    if (my_state_ == ABORTED)
        {
            prev_decisions_.push_back(ABORT);
        }
    else
        {
            prev_decisions_.push_back(COMMIT);
        }
}



// create thread 1 that always receives state requests
//     if it receives one SR, then create a thread 2 that prepares response and waits for it.
//     if another SR comes to thread 1, then
//         if 2 is still waiting for something, kill thread 2
//         if thread 2 has made a dec and exits, then return state to SR request
