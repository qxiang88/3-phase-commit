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

pthread_mutex_t fd_lock;
extern pthread_mutex_t up_lock;
pthread_mutex_t alive_fd_lock;
pthread_mutex_t sdr_fd_lock;
ReceivedMsgType received_msg_type;
pthread_mutex_t log_lock;

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

    // pthread_t logger_thread;
    // if (pthread_create(&logger_thread, NULL, p->AddToLog(), (void *)p)) {
    //     cout << "P" << p->get_pid() << ": ERROR: Unable to create logger thread for P" << p->get_pid() << endl;
    //     pthread_exit(NULL);
    // }

    // TODO: share all internal thread info with controller
    // TODO: think whether we need to add the temporary receive threads as well?
    pthread_t server_thread;
    p->CreateThread(server_thread, server, (void *)p);

    // sleep to make sure server is up and listening
    usleep(kGeneralSleep);

    // if pid=0, then it is the coordinator
    //TODO: find a better way to set coordinator
    if (p->get_pid() == 0) {
        p->CoordinatorMode();
        cout << "P" << p->get_pid() << ": Coordinator mode over" << endl;
    } else {
        p->ParticipantMode();
        cout << "P" << p->get_pid() << ": Participant mode over" << endl;
    }

    void* status;
    pthread_join(server_thread, &status);
    pthread_exit(NULL);
}


void Process::Initialize(int pid, string log_file, string playlist_file) {
    pid_ = pid;
    log_file_ = log_file;
    playlist_file_ = playlist_file;
    fd_.resize(N, -1);
    alive_fd_.resize(N, -1);
    sdr_fd_.resize(N, -1);
    process_state_.resize(N, UNINITIALIZED);
    my_state_ = UNINITIALIZED;
    transaction_id_ = 0;
    thread_set.clear();
}

int Process::get_pid() {
    return pid_;
}

void Process::set_pid(int process_id) {
    pid_ = process_id;
}

void Process::set_log_file(string logfile) {
    log_file_ = logfile;
}

void Process::set_playlist_file(string playlistfile) {
    playlist_file_ = playlistfile;
}

// TODO: remember to set _fd_ to -1 on connection close
// saves socket fd for connection from a send port
void Process::set_fd(int process_id, int new_fd) {
    pthread_mutex_lock(&fd_lock);
    if (fd_[process_id] == -1)
    {
        fd_[process_id] = new_fd;
    }
    pthread_mutex_unlock(&fd_lock);
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

void Process::set_my_coordinator(int process_id) {
    my_coordinator_ = process_id;
    set_coordinator(process_id);
}

// get socket fd corresponding to process_id's send connection
int Process::get_fd(int process_id) {
    int ret;
    pthread_mutex_lock(&fd_lock);
    ret = fd_[process_id];
    pthread_mutex_unlock(&fd_lock);
    return ret;
}

// get socket fd corresponding to process_id's alive connection
int Process::get_alive_fd(int process_id) {
    int ret;
    pthread_mutex_lock(&alive_fd_lock);
    ret = alive_fd_[process_id];
    pthread_mutex_unlock(&alive_fd_lock);
    return ret;
}

int Process::get_sdr_fd(int process_id) {
    int ret;
    pthread_mutex_lock(&sdr_fd_lock);
    ret = sdr_fd_[process_id];
    pthread_mutex_unlock(&sdr_fd_lock);
    return ret;
}


ProcessState Process::get_my_state()
{
    return my_state_;
}

int Process::get_my_coordinator()
{
    return my_coordinator_;
}
// print function for debugging purposes
void Process::Print() {
    cout << "pid=" << get_pid() << " ";
    for (int i = 0; i < N; ++i) {
        cout << fd_[i] << ",";
    }
    cout << endl;
}

// adds the pthread_t entry to the thread_set
void Process::AddThreadToSet(pthread_t thread) {
    thread_set.insert(thread);
}

// removes the pthread_t entry from the thread_set
void Process::RemoveThreadFromSet(pthread_t thread) {
    thread_set.erase(thread);
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

// constructs general msgs of form (without quotes)
// "<msgbody> <transaction_id> $"
// works for following message bodies
// msg_body = ABORT,
// outputs constructed msg in msg
void Process::ConstructGeneralMsg(const string &msg_body,
                                  const int transaction_id, string &msg) {
    msg = msg_body + " " + to_string(transaction_id) + " $" ;
}

// sends ABORT message to process with pid=process_id
void Process::SendAbortToProcess(int process_id) {
    string msg;
    ConstructGeneralMsg(kAbort, transaction_id_, msg);

    if (send(get_fd(process_id), msg.c_str(), msg.size(), 0) == -1) {
        cout << "P" << get_pid() << ": ERROR: sending to P" << process_id << endl;
    }
    else {
        cout << "P" << get_pid() << ": Msg sent to P" << process_id << ": " << msg << endl;
    }
}

// takes as input the received_msg
// extracts the core message body from it to extracted_msg
// extracts transaction id in the received_msg to received_tid
void Process::ExtractMsg(const string &received_msg, string &extracted_msg, int &received_tid) {
    std::istringstream iss(received_msg);
    iss >> extracted_msg;
    iss >> received_tid;
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

    if (pthread_mutex_init(&log_lock, NULL) != 0) {
        cout << "P" << get_pid() << ": Mutex init failed" << endl;
        pthread_exit(NULL);
    }

    if (pthread_mutex_init(&up_lock, NULL) != 0) {
        cout << "P" << get_pid() << ": Mutex init failed" << endl;
        pthread_exit(NULL);
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

// creates one receive alive thread
// creates one send-alive thread
void Process::CreateAliveThreads(pthread_t &receive_alive_thread, pthread_t &send_alive_thread) {
    CreateThread(receive_alive_thread, SendAlive, (void *)this);
    CreateThread(send_alive_thread, ReceiveAlive, (void *)this);
}

void Process::CreateSDRThread(pthread_t &rec_sr_dr_thread) {
    CreateThread(rec_sr_dr_thread, ReceiveStateOrDecReq, (void *)this);
}

void Process::AddToLog(string s, bool new_round)
{
    pthread_mutex_lock(&fd_lock);

    if (new_round)
    {
        vector<string> new_trans_log;
        new_trans_log.push_back(s);
        log_[transaction_id_] = new_trans_log;

        stringstream ss;
        ss << "TID: " << to_string(transaction_id_) << endl << s;
        s = ss.str();
    }

    else
        log_[transaction_id_].push_back(s);

    ofstream outfile(log_file_.c_str(), fstream::app);
    if (outfile.is_open())
        outfile << s << endl;

    else
        cout << "couldn't open" << log_file_ << endl;

    pthread_mutex_unlock(&fd_lock);
    outfile.close();
    return;
}

void Process::LoadLog()
{
    string line;
    vector<string> trans_log;
    int round_id;
    ifstream myfile(log_file_);
    if (myfile.is_open())
    {
        while (getline(myfile, line))
        {
            if (line.empty())
                continue;

            size_t found = line.find("TID:");
            if (found != string::npos)
            {
                string id = line.substr(5);
                round_id = atoi(id.c_str());
            }
            else
            {
                log_[round_id].push_back(line);
            }
        }
        myfile.close();
    }
    else
    {
        cout << "Failed to load log file" << endl;
    }
}

void Process::LoadTransactionId()
{
    if (!log_.empty())
        transaction_id_ = log_.rbegin()->first;
    else
        cout << "Error. Log empty" << endl;
}

bool Process::CheckCoordinator()
{
    transaction_id_ = (log_.rbegin())->first;
    size_t found = log_[transaction_id_][0].find("start");
    if (found != string::npos)
        return true;
    else
        return false;
}

string Process::GetDecision()
{
    //null string means no decision
    vector<string> cur_trans_log = log_[transaction_id_];
    for (vector<string>::reverse_iterator it = cur_trans_log.rbegin(); it != cur_trans_log.rend(); ++it)
    {
        if (   (*it) == "commit" || (*it) == "abort" || (*it) == "precommit" )
            return *it;
    }
    return "";
}

string Process::GetVote()
{
    vector<string> cur_trans_log = log_[transaction_id_];
    for (vector<string>::reverse_iterator it = cur_trans_log.rbegin(); it != cur_trans_log.rend(); ++it)
    {
        if ( (*it) == "yes" )
            return *it;

        // else if ( (*it) == "abort")
        //     return *it;
    }

    return "";
}

void Process::LoadParticipants()
{
    //assumes first entry in round will have participants. change if not
    string entry = log_[transaction_id_][0];

    vector<string> tokens = split(entry, ' ');

    participants_.clear();

    if (CheckCoordinator())
    {
        if (tokens[0] == "start")
        {
            vector<string> rv = split(tokens[1], ',');

            for (vector<string>::iterator it = rv.begin(); it < rv.end(); it++)
            {
                participants_.insert(atoi((*it).c_str()));
            }
        }
    }

    else
    {
        if (tokens[0] == "votereq")
        {
            vector<string> rv = split(tokens[2], ',');
            for (vector<string>::iterator it = rv.begin(); it < rv.end(); it++)
            {
                participants_.insert(atoi((*it).c_str()));
            }

            participants_.insert(atoi(tokens[1].c_str()));
        }
    }

}

int Process::GetCoordinator()
{
    if (CheckCoordinator())
        return pid_;
    else
    {
        string entry = log_[transaction_id_][0];
        vector<string> tokens = split(entry, ' ');
        if (tokens[0] == "votereq")
        {
            return atoi(tokens[1].c_str());
        }
    }
}
//sets my_state_ accd to log and calls termination protocol
void Process::Recovery()
{
    LoadLog();
    LoadTransactionId();
    LoadParticipants();

    string decision = GetDecision();

    //probably need to send the decision to others

    if (decision == "commit")
        my_state_ = COMMITTED;

    else if (decision == "abort")
        my_state_ = ABORTED;

    else if (decision == "precommit")
    {
        my_state_ = COMMITTABLE;
        DecisionRequest();
    }

    else
    {   //no decision
        string vote = GetVote();
        if (vote == "yes")
        {
            my_state_ = UNCERTAIN;
            DecisionRequest();
        }
        else if (vote.empty())
        {
            my_state_ = ABORTED;
        }
    }
}

void Process::Timeout()
{
    TerminationProtocol();
}

void Process::DecisionRequest()
{
    //if total failure, then init termination protocol with total failure. give arg to TP
}

void Process::TerminationProtocol()
{   //called when a process times out.

    //sets new coord
    cout << "TerminationProtocol" << endl;
    ElectionProtocol();

    if (pid_ == my_coordinator_)
    {   //coord case
        //pass on arg saying total failue, then send to all
        CreateThread(newcoord_thread, NewCoordinatorMode, (void *)this);
    }
    else
    {
        SendURElected(my_coordinator_);
        //then do nothing here, the initial SR thread will see that a SR message is here
        //so that replies state and does all that shit
        //till we get a decision
        // TerminationParticipantMode();
    }
}

void Process::ElectionProtocol()
{
    int min = GetNewCoordinator();
    set_my_coordinator(min);
    cout << "P" << pid_ << ": my new coordinator=" << min << endl;
}

void Process::SendURElected(int recp)
{
    //send it on SR thread only
    string msg;
    string msg_to_send = kURElected;
    ConstructGeneralMsg(msg_to_send, transaction_id_, msg);
    if (send(get_sdr_fd(recp), msg.c_str(), msg.size(), 0) == -1) {
        cout << "P" << get_pid() << ": ERROR: sending to P" << recp << endl;
    }
    else {
        cout << "P" << get_pid() << ": URElected Msg sent to P" << recp << ": " << msg << endl;
    }

}

int Process::GetNewCoordinator()
{
    int min;
    for ( auto it = up_.cbegin(); it != up_.cend(); ++it )
    {
        if (it == up_.cbegin())
            min = (*it);
        else
        {
            if (*it < min)
                min = *it;
        }
    }
    return min;
}

//initial ones just to maintain uniformity. can be removed if want to handle string while calling
void Process::LogCommit()
{
    AddToLog("commit");
}

void Process::LogPreCommit()
{
    AddToLog("precommit");
}
void Process::LogAbort()
{
    AddToLog("abort");
}

void Process::LogYes()
{
    AddToLog("yes");
}

void Process::LogVoteReq()
{

    string s = "votereq";
    s += " ";
    s += to_string(my_coordinator_);
    s += " ";

    // for(int i=0; i<participants_.size(); i++)
    for ( auto it = participants_.begin(); it != participants_.end(); ++it )
    {
        if (it != participants_.begin())
            s += ",";
        s += to_string(*it);
    }

    AddToLog(s, true);
}

void Process::LogStart()
{
    string s = "start";
    s += " ";

    // for (const auto& ps : participant_state_map_) {
    //     if(&ps!=participant_state_map_.begin())
    //         s+=",";
    //     s+=to_string(ps.first);
    // }

    for ( auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it )
    {
        if (it != participant_state_map_.begin())
            s += ",";
        s += to_string(it->first);
    }

    AddToLog(s, true);
}

void Process::SendState(int recp)
{
    string msg;
    string msg_to_send = to_string((int)my_state_);
    ConstructGeneralMsg(msg_to_send, transaction_id_, msg);
    if (send(get_fd(recp), msg.c_str(), msg.size(), 0) == -1) {
        cout << "P" << get_pid() << ": ERROR: sending to P" << recp << endl;
    }
    else {
        cout << "P" << get_pid() << ": Msg sent to P" << recp << ": " << msg << endl;
    }
}

int Process::get_transaction_id()
{
    return transaction_id_;
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

