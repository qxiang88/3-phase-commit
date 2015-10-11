#include "process.h"
#include "constants.h"
#include "limits.h"
#include "fstream"
#include <assert.h> 
#include "sstream"
#include "iostream"
#include "unistd.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include "sys/time.h"
#include "sstream"
#include <algorithm>
using namespace std;

pthread_mutex_t fd_lock;
extern pthread_mutex_t up_lock;
pthread_mutex_t alive_fd_lock;
pthread_mutex_t up_fd_lock;
pthread_mutex_t sdr_fd_lock;
ReceivedMsgType received_msg_type;
pthread_mutex_t log_lock;
pthread_mutex_t new_coord_lock;
pthread_mutex_t state_req_lock;
pthread_mutex_t my_coord_lock;
pthread_mutex_t my_state_lock;
pthread_mutex_t num_messages_lock;
pthread_mutex_t resume_lock;


void Process::ThreeWayHandshake() {
    while (get_handshake() != EXPECTING) {
        usleep(kMiniSleep);
    }

    set_handshake(READY);
}
// entry function for a process created normally by the controller
// normally means it is the first time this process has been spawned
// no need to read log
// if it is P0, declare self as coordinator
void* ThreadEntry(void* _p) {
    Process *p = (Process *)_p;
    p->InitializeLocks();

    if (!(p->LoadPlaylist())) {
        cout << "P" << p->get_pid() << ": Exiting" << endl;
        pthread_exit(NULL);
    }

    // TODO: share all internal thread info with controller
    // TODO: think whether we need to add the temporary receive threads as well?
    pthread_t server_thread;
    p->CreateThread(server_thread, server, (void *)p);

    // sleep to make sure server is up and listening
    // usleep(kGeneralSleep);

    while (true) {
        int status = p->get_my_status();
        if (status == RECOVERY) {
            cout << "Resurrecting process " << p->get_pid() << endl;
            p->Recovery();
            p->set_my_status(DONE);
            cout << "P" << p->get_pid() << ": Recovery mode over" << endl;
        }
        else {
            p->ThreeWayHandshake();
            // if my_status_ is DONE, it means it completed prev transaction
            // so, it enters normal modes

            if (status == DONE) {
                // if pid=0, then it is the coordinator
                p->set_my_status(RUNNING);

                if (p->get_pid() == p->get_my_coordinator()) {
                    p->CoordinatorMode();
                    cout << "P" << p->get_pid() << ": Coordinator mode over" << endl;
                    p->set_my_status(DONE);
                } else {
                    p->ParticipantMode();
                    cout << "P" << p->get_pid() << ": Participant mode over" << endl;
                    p->set_my_status(DONE);
                }
            } else {
                cout << "P" << p->get_pid() << ": Unexpected Status" << p->get_my_status() << endl;
                //TODO: verify if process can enter ThreadEntry with any other runningstatus
            }
        }
    }

    void* status;
    pthread_join(server_thread, &status);
    pthread_exit(NULL);
}

void Process::Initialize(int pid,
                         string log_file,
                         string playlist_file,
                         int coord_id,
                         ProcessRunningStatus status) {
    pid_ = pid;
    log_file_ = log_file;
    playlist_file_ = playlist_file;
    set_my_coordinator(coord_id);
    my_status_ = status;

    log_.clear();
    fd_.clear();
    alive_fd_.clear();
    sdr_fd_.clear();
    up_fd_.clear();
    thread_set.clear();
    thread_set_alive_.clear();
    up_.clear();
    participants_.clear();
    participant_state_map_.clear();
    rcv_alive_thread_arg.clear();
    fd_.resize(N, -1);
    alive_fd_.resize(N, -1);
    sdr_fd_.resize(N, -1);
    up_fd_.resize(N, -1);

    transaction_id_ = -1;
    my_state_ = UNINITIALIZED;
    newcoord_thread = 0;
    state_req_in_progress = false;
    new_coord_thread_made = false;
    server_sockfd_ = -1;
    num_messages_ = INT_MAX;
    resume_ = false;
    handshake_ = BLANK;
}

// Initialize all locks
void Process::InitializeLocks() {
    if (pthread_mutex_init(&fd_lock, NULL) != 0) {
        cout << "P" << get_pid() << ": Mutex init failed" << endl;
        pthread_exit(NULL);
    }

    if (pthread_mutex_init(&alive_fd_lock, NULL) != 0) {
        cout << "P" << get_pid() << ": Mutex init failed" << endl;
        pthread_exit(NULL);
    }

    if (pthread_mutex_init(&sdr_fd_lock, NULL) != 0) {
        cout << "P" << get_pid() << ": Mutex init failed" << endl;
        pthread_exit(NULL);
    }

    if (pthread_mutex_init(&up_fd_lock, NULL) != 0) {
        cout << "P" << get_pid() << ": Mutex init failed" << endl;
        pthread_exit(NULL);
    }

    if (pthread_mutex_init(&log_lock, NULL) != 0) {
        cout << "P" << get_pid() << ": Mutex init failed" << endl;
        pthread_exit(NULL);
    }

    if (pthread_mutex_init(&up_lock, NULL) != 0) {
        cout << "P" << get_pid() << ": Mutex init failed" << endl;
        pthread_exit(NULL);
    }

    if (pthread_mutex_init(&new_coord_lock, NULL) != 0) {
        cout << "P" << get_pid() << ": Mutex init failed" << endl;
        pthread_exit(NULL);
    }

    if (pthread_mutex_init(&my_coord_lock, NULL) != 0) {
        cout << "P" << get_pid() << ": Mutex init failed" << endl;
        pthread_exit(NULL);
    }

    if (pthread_mutex_init(&num_messages_lock, NULL) != 0) {
        cout << "P" << get_pid() << ": Mutex init failed" << endl;
        pthread_exit(NULL);
    }

    if (pthread_mutex_init(&resume_lock, NULL) != 0) {
        cout << "P" << get_pid() << ": Mutex init failed" << endl;
        pthread_exit(NULL);
    }
}

void Process::Reset(int coord_id) {
    set_my_coordinator(coord_id);

    log_.clear();
    up_.clear();
    thread_set_alive_.clear();
    participants_.clear();
    participant_state_map_.clear();

    transaction_id_ = -1;
    my_state_ = UNINITIALIZED;
    newcoord_thread = 0;
    new_coord_thread_made = false;
    state_req_in_progress = false;
    num_messages_ = INT_MAX;
    resume_ = false;
    handshake_ = BLANK;
}
// reads the playlist file
// loads it into playlist_ unordered map
bool Process::LoadPlaylist() {
    ifstream fin;
    fin.exceptions ( ifstream::failbit | ifstream::badbit );
    try {
        fin.open(playlist_file_.c_str());
        string song_name;
        string song_url;
        while (!fin.eof()) {
            fin >> song_name;
            fin >> song_url;
            playlist_.insert(make_pair(song_name, song_url));
        }
        fin.close();
        return true;
    } catch (ifstream::failure e) {
        cout << e.what() << endl;
        if (fin.is_open()) fin.close();
        return false;
    }
}

//----------------------GETTERS--------------------------------------------------

int Process::get_pid() {
    return pid_;
}

int Process::get_server_sockfd() {
    return server_sockfd_;
}

int Process::get_transaction_id() {
    return transaction_id_;
}
// get socket fd corresponding to process_id's send connection
int Process::get_fd(int process_id) {
    if (process_id == INT_MAX) return -1;
    int ret;
    pthread_mutex_lock(&fd_lock);
    ret = fd_[process_id];
    pthread_mutex_unlock(&fd_lock);
    return ret;
}
// get socket fd corresponding to process_id's alive connection
int Process::get_alive_fd(int process_id) {
    if (process_id == INT_MAX) return -1;
    int ret;
    pthread_mutex_lock(&alive_fd_lock);
    ret = alive_fd_[process_id];
    pthread_mutex_unlock(&alive_fd_lock);
    return ret;
}

int Process::get_up_fd(int process_id) {
    if (process_id == INT_MAX) return -1;
    int ret;
    pthread_mutex_lock(&up_fd_lock);
    ret = up_fd_[process_id];
    pthread_mutex_unlock(&up_fd_lock);
    return ret;
}

int Process::get_sdr_fd(int process_id) {
    if (process_id == INT_MAX) return -1;
    int ret;
    pthread_mutex_lock(&sdr_fd_lock);
    ret = sdr_fd_[process_id];
    pthread_mutex_unlock(&sdr_fd_lock);
    return ret;
}

ProcessRunningStatus Process::get_my_status() {
    return my_status_;
}

ProcessState Process::get_my_state()
{
    ProcessState local;
    pthread_mutex_lock(&my_state_lock);
    local = my_state_;
    pthread_mutex_unlock(&my_state_lock);
    return local;
}

int Process::get_my_coordinator()
{
    int mc;
    pthread_mutex_lock(&my_coord_lock);
    mc = my_coordinator_;
    pthread_mutex_unlock(&my_coord_lock);
    return mc;
}

Handshake Process::get_handshake() {
    return handshake_;
}

int Process::get_num_messages() {
    int num;
    pthread_mutex_lock(&num_messages_lock);
    num = num_messages_;
    pthread_mutex_unlock(&num_messages_lock);
    return num;
}

bool Process::get_resume() {
    int res;
    pthread_mutex_lock(&resume_lock);
    res = resume_;
    pthread_mutex_unlock(&resume_lock);
    return res;
}

//-------------------SETTERS-----------------------------------------------------

void Process::set_pid(int process_id) {
    pid_ = process_id;
}

void Process::set_log_file(string logfile) {
    log_file_ = logfile;
}

void Process::set_playlist_file(string playlistfile) {
    playlist_file_ = playlistfile;
}

void Process::set_my_coordinator(int process_id) {
    pthread_mutex_lock(&my_coord_lock);
    my_coordinator_ = process_id;
    pthread_mutex_unlock(&my_coord_lock);

    set_coordinator(process_id);
}

void Process::set_transaction_id(int tid) {
    transaction_id_ = tid;
}

void Process::set_server_sockfd(int socket_fd) {
    server_sockfd_ = socket_fd;
}
// TODO: remember to set _fd_ to -1 on connection close
// saves socket fd for connection from a send port
void Process::set_fd(int process_id, int new_fd) {
    cout << "P" << get_pid() << ": for P" << process_id << ": old=" << get_fd(process_id);
    pthread_mutex_lock(&fd_lock);
    if (fd_[process_id] == -1)
    {
        fd_[process_id] = new_fd;
    }
    pthread_mutex_unlock(&fd_lock);
    cout << ": new=" << get_fd(process_id) << endl;
}

void Process::set_up_fd(int process_id, int new_fd) {
    pthread_mutex_lock(&up_fd_lock);
    if (up_fd_[process_id] == -1)
    {
        up_fd_[process_id] = new_fd;
    }
    pthread_mutex_unlock(&up_fd_lock);
}

// TODO: remember to set fd_ to -1 on connection close
// saves socket fd for connection from a send port
void Process::set_sdr_fd(int process_id, int new_fd) {
    pthread_mutex_lock(&sdr_fd_lock);
    if (sdr_fd_[process_id] == -1)
    {
        sdr_fd_[process_id] = new_fd;
    }
    pthread_mutex_unlock(&sdr_fd_lock);
}

void Process::set_alive_fd(int process_id, int new_fd) {
    pthread_mutex_lock(&alive_fd_lock);
    if (alive_fd_[process_id] == -1)
    {
        alive_fd_[process_id] = new_fd;
    }
    pthread_mutex_unlock(&alive_fd_lock);
}

void Process::set_my_status(ProcessRunningStatus status) {
    my_status_ = status;
}

void Process::set_my_state(ProcessState state)
{
    pthread_mutex_lock(&my_state_lock);
    my_state_ = state;
    pthread_mutex_unlock(&my_state_lock);
}

void Process::set_state_req_in_progress(bool val)
{
    pthread_mutex_lock(&state_req_lock);
    state_req_in_progress = val;
    pthread_mutex_unlock(&state_req_lock);
}
//TODO: handshake lock
void Process::set_handshake(Handshake hs) {
    handshake_ = hs;
}

void Process::set_num_messages(int num) {
    pthread_mutex_lock(&num_messages_lock);
    num_messages_ = num;
    pthread_mutex_unlock(&num_messages_lock);
}

void Process::DecrementNumMessages() {
    set_num_messages(get_num_messages() - 1);
}

void Process::set_resume(bool res) {
    pthread_mutex_lock(&resume_lock);
    resume_ = res;
    pthread_mutex_unlock(&resume_lock);
}

//-----------------------CLOSEFD,RESET-------------------------------------------------

void Process::Close_server_sockfd() {
    close(server_sockfd_) ;
}

void Process::CloseUpFDs()
{
    for (auto it = up_fd_.begin(); it != up_fd_.end(); it++)
    {
        if ((*it) != -1)
            close(*it);
    }
}

void Process::CloseFDs()
{
    for (auto it = fd_.begin(); it != fd_.end(); it++)
    {
        if ((*it) != -1)
            close(*it);
    }
}
void Process::CloseAliveFDs()
{

    for (auto it = alive_fd_.begin(); it != alive_fd_.end(); it++)
    {
        if ((*it) != -1)
            close(*it);
    }
}
void Process::CloseSDRFDs()
{

    for (auto it = sdr_fd_.begin(); it != sdr_fd_.end(); it++)
    {
        if ((*it) != -1)
            close(*it);
    }
}

void Process::reset_fd(int process_id) {
    // cout<<"P"<<get_pid()<<"reseting"<<process_id<<endl;
    pthread_mutex_lock(&fd_lock);
    close(fd_[process_id]);
    fd_[process_id] = -1;
    pthread_mutex_unlock(&fd_lock);
}

void Process::reset_up_fd(int process_id){
    pthread_mutex_lock(&up_fd_lock);
    close(up_fd_[process_id]);
    up_fd_[process_id] = -1;
    pthread_mutex_unlock(&up_fd_lock);
}

void Process::reset_alive_fd(int process_id) {
    pthread_mutex_lock(&alive_fd_lock);
    close(alive_fd_[process_id]);
    alive_fd_[process_id] = -1;
    pthread_mutex_unlock(&alive_fd_lock);
}

void Process::reset_sdr_fd(int process_id) {
    pthread_mutex_lock(&sdr_fd_lock);
    close(sdr_fd_[process_id]);
    sdr_fd_[process_id] = -1;
    pthread_mutex_unlock(&sdr_fd_lock);
}

//------------------------------------------------------------------------

// print function for debugging purposes
void Process::Print() {
    cout << "pid=" << get_pid() << " ";
    for (int i = 0; i < N; ++i) {
        cout << get_fd(i) << ",";
    }
    cout << endl;
}

//------------------------------------------------------------------------

// returns immediately if resume_ is true
// if resume is false, waits till num_messages_ is positive
void Process::WaitOrProceed() {
    if (get_resume()) {
        return; // if resume is true, then ignore num_messages_ and let 3PC run
    } else {
        // resume is false. Check value of num_messages_
        // waits till num_messages_ is positive
        while (get_num_messages() <= 0) {
            usleep(kMiniSleep);
        }
    }
}

// constructs general msgs of form (without quotes)
// "<msgbody> <transaction_id> $"
// works for following message bodies
// msg_body = ABORT,
// outputs constructed msg in msg
void Process::ConstructGeneralMsg(const string & msg_body,
                                  const int transaction_id, string & msg) {
    msg = msg_body + " " + to_string(transaction_id) + " $" ;
}

// takes as input the received_msg
// extracts the core message body from it to extracted_msg
// extracts transaction id in the received_msg to received_tid
void Process::ExtractMsg(const string & received_msg, string & extracted_msg, int &received_tid) {
    std::istringstream iss(received_msg);
    iss >> extracted_msg;
    iss >> received_tid;
}

// process's voting function based on the loaded
// playlist and the transaction trans
// sets my_state_ accordingly
void Process::Vote(string trans) {
    std::istringstream iss(trans);
    string transaction_type, song_name;
    iss >> transaction_type;
    iss >> song_name;
    if (transaction_type == kAdd) {
        if (playlist_.find(song_name) != playlist_.end()) {
            // song_name already exists. Vote NO
            my_state_ = ABORTED;
        } else {
            my_state_ = UNCERTAIN;
        }
    } else if (transaction_type == kRemove) {
        if (playlist_.find(song_name) == playlist_.end()) {
            // song_name does not exist. Vote NO
            my_state_ = ABORTED;
        } else {
            my_state_ = UNCERTAIN;
        }
    } else if (transaction_type == kEdit) {
        string new_name, new_url;
        iss >> new_name;
        iss >> new_url;
        if (playlist_.find(song_name) == playlist_.end()) {
            // song_name does not exist. Vote NO
            my_state_ = ABORTED;
        } else {
            // song_name exists.
            // Check if the new_name already exists
            if (playlist_.find(new_name) != playlist_.end()) {
                //new_name already exists. Can't edit. Vote NO
                my_state_ = ABORTED;
            } else {
                // song_name can be edited to new_name and new_url. Vote YES
                my_state_ = UNCERTAIN;
            }
        }
    }
}


vector<string> split(string s, char delimiter)
{
    stringstream ss(s);
    string temp;
    vector<string> rval;
    while (getline(ss, temp, delimiter))
    {
        rval.push_back(temp);
    }
    return rval;
}
