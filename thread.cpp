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
// adds the pthread_t entry to the thread_set
void Process::AddThreadToSet(pthread_t thread) {
    thread_set.insert(thread);
}

// adds the pthread_t entry to the thread_set_alive_
void Process::AddThreadToSetAlive(pthread_t thread) {
    thread_set_alive_.insert(thread);
}

// removes the pthread_t entry from the thread_set
void Process::RemoveThreadFromSet(pthread_t thread) {
    thread_set.erase(thread);
}

// removes the pthread_t entry from the thread_set_alive_
void Process::RemoveThreadFromSetAlive(pthread_t thread) {
    thread_set_alive_.erase(thread);
}

// kills all alive threads
void Process::KillAliveThreads() {
    for (const auto &th : thread_set_alive_) {
        pthread_cancel(th);
    }
}

// creates a new thread with passed
// adds the new thread to the thread set
void Process::CreateThread(pthread_t &thread, void* (*f)(void* ), void* arg) {
    if (pthread_create(&thread, NULL, f, arg)) {
        cout << "P" << get_pid() << ": ERROR: Unable to create thread" << endl;
        pthread_exit(NULL);
    }
    AddThreadToSet(thread);
}

// Especially for alive threads
// creates a new alive thread with passed
// adds the new alive thread to the alive_thread set
void Process::CreateThreadForAlive(pthread_t &thread, void* (*f)(void* ), void* arg) {
    if (pthread_create(&thread, NULL, f, arg)) {
        cout << "P" << get_pid() << ": ERROR: Unable to create thread" << endl;
        pthread_exit(NULL);
    }
    AddThreadToSetAlive(thread);
}

// creates one receive alive thread
// creates one send-alive thread
void Process::CreateAliveThreads(vector<pthread_t> &receive_alive_threads, pthread_t &send_alive_thread) {

    int n = up_.size();
    //
    rcv_alive_thread_arg.clear();
    rcv_alive_thread_arg.resize(n);

    // ReceiveAliveThreadArgument **rcv_thread_arg = new ReceiveAliveThreadArgument*[n];

    int i = 0;
    for (auto it = up_.begin(); it != up_.end(); ++it ) {
        rcv_alive_thread_arg[i] = new ReceiveAliveThreadArgument;
        rcv_alive_thread_arg[i]->p = this;
        rcv_alive_thread_arg[i]->pid_from_whom = *it;
        CreateThreadForAlive(receive_alive_threads[i], ReceiveAlive, (void *)rcv_alive_thread_arg[i]);
        i++;
    }
    // CreateThread(receive_alive_thread, ReceiveAlive, (void *)this);
    CreateThreadForAlive(send_alive_thread, SendAlive, (void *)this);
}

void Process::CreateUpThread(int process_id, pthread_t &up_receive_thread) {
    ReceiveSDRUpThreadArgument* rcv_up_thread_arg = new ReceiveSDRUpThreadArgument;
    rcv_up_thread_arg-> p = this;
    rcv_up_thread_arg->for_whom = process_id;
    CreateThread(up_receive_thread, ReceiveUp, (void *)rcv_up_thread_arg);
}

void Process::CreateSDRThread(int process_id, pthread_t &sdr_receive_thread) {
    ReceiveSDRUpThreadArgument* rcv_sdr_thread_arg = new ReceiveSDRUpThreadArgument;
    rcv_sdr_thread_arg-> p = this;
    rcv_sdr_thread_arg->for_whom = process_id;
    CreateThread(sdr_receive_thread, ReceiveStateOrDecReq, (void *)rcv_sdr_thread_arg);
}
