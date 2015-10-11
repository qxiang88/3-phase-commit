#include "process.h"
#include "constants.h"
#include "iostream"
#include "fstream"
#include "unistd.h"
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include "limits.h"
#include <assert.h> 
#include "sstream"
#include <algorithm>

using namespace std;

extern pthread_mutex_t fd_lock;
extern pthread_mutex_t up_lock;
extern pthread_mutex_t alive_fd_lock;
extern pthread_mutex_t up_fd_lock;
extern pthread_mutex_t sdr_fd_lock;
extern ReceivedMsgType received_msg_type;
extern pthread_mutex_t log_lock;
extern pthread_mutex_t new_coord_lock;
extern pthread_mutex_t state_req_lock;
extern pthread_mutex_t my_coord_lock;
extern pthread_mutex_t my_state_lock;
extern pthread_mutex_t num_messages_lock;
extern pthread_mutex_t resume_lock;

void Process::SendState(int recp)
{
    string msg;
    string msg_to_send = to_string((int)my_state_);
    ConstructGeneralMsg(msg_to_send, transaction_id_, msg);
    // cout << "trying to send st to " << recp << endl;
    if (recp == INT_MAX)
        return;
    ContinueOrDie();
    if (send(get_fd(recp), msg.c_str(), msg.size(), 0) == -1) {
        cout << "P" << get_pid() << ": ERROR: sending state to P" << recp << endl;
        RemoveFromUpSet(recp);
    }
    else {
        cout << "P" << get_pid() << ": Msg sent to P" << recp << ": " << msg << endl;
    }
    DecrementNumMessages();
}

void Process::SendDecision(int recp)
{
    string msg;
    int code_to_send;
    if (my_state_ == COMMITTED)
        code_to_send = COMMIT;
    else if (my_state_ == ABORTED)
        code_to_send = ABORT;
    string msg_to_send = to_string(code_to_send);
    ConstructGeneralMsg(msg_to_send, transaction_id_, msg);
    // cout << get_fd(recp) << " " << recp << endl;
    ContinueOrDie();
    if (send(get_fd(recp), msg.c_str(), msg.size(), 0) == -1) {
        timeval aftertime;
        gettimeofday(&aftertime, NULL);
        cout << "P" << get_pid() << ": ERROR: sending decision to P" << recp << " t=" << aftertime.tv_sec << "," << aftertime.tv_usec << endl;
        RemoveFromUpSet(recp);
    }
    else {
        // cout << "P" << get_pid() << ": decision Msg sent to P" << recp << ": " << msg << endl;
    }
    DecrementNumMessages();
}

void Process::SendPrevDecision(int recp, int tid)
{
    string msg;
    int code_to_send = prev_decisions_[tid];
    string msg_to_send = to_string(code_to_send);
    ConstructGeneralMsg(msg_to_send, transaction_id_, msg);
    ContinueOrDie();
    if (send(get_fd(recp), msg.c_str(), msg.size(), 0) == -1) {
        cout << "P" << get_pid() << ": ERROR: sending prev dec to P" << recp << endl;
        RemoveFromUpSet(recp);
    }
    else {
        cout << "P" << get_pid() << ": Msg sent to P" << recp << ": " << msg << endl;
    }
    DecrementNumMessages();
}

bool Process::SendURElected(int recp)
{
    //send it on SR thread only
    string msg;
    string msg_to_send = kURElected;
    ConstructGeneralMsg(msg_to_send, transaction_id_, msg);
    bool ret;

    ContinueOrDie();
    if (send(get_sdr_fd(recp), msg.c_str(), msg.size(), 0) == -1) {
        cout << "P" << get_pid() << ": ERROR: sending urelected to P" << recp << endl;
        RemoveFromUpSet(recp);
        ret = false;
    }
    else {
        cout << "P" << get_pid() << ": URElected Msg sent to P" << recp << ": " << msg << endl;
        ret = true;
    }
    DecrementNumMessages();
    return ret;
}


// sends ABORT message to process with pid=process_id
void Process::SendAbortToProcess(int process_id) {
    string msg;
    ConstructGeneralMsg(kAbort, transaction_id_, msg);
    ContinueOrDie();
    if (send(get_fd(process_id), msg.c_str(), msg.size(), 0) == -1) {
        cout << "P" << get_pid() << ": ERROR: sending abort to P" << process_id << endl;
        RemoveFromUpSet(process_id);
    }
    else {
        cout << "P" << get_pid() << ": Msg sent to P" << process_id << ": " << msg << endl;
    }
    DecrementNumMessages();
}


void Process::SendDecReqToAll(const string & msg) {

    //this only contains operational processes for non timeout cases
    for ( auto it = participants_.begin(); it != participants_.end(); ++it ) {
        if ((*it) == get_pid()) continue; // do not send to self
        // cout << "P" << get_pid() << ": sdr_fd for P" << (*it) << "=" << get_sdr_fd(*it) << endl;
        ContinueOrDie();
        if (send(get_sdr_fd(*it), msg.c_str(), msg.size(), 0) == -1) {
            cout << "P" << get_pid() << ": ERROR1: sending to P" << (*it) << endl;
            // RemoveFromUpSet(*it);
            // no need to update up set
        }
        else {
            // cout << "P" << get_pid() << ": Msg sent to P" << (*it) << ": " << msg << endl;
        }
        DecrementNumMessages();
    }
}
