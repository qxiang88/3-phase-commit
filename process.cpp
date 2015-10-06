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
ReceivedMsgType received_msg_type;
pthread_mutex_t log_lock;

void Process::Initialize(int pid, string log_file, string playlist_file) {
    pid_ = pid;
    log_file_ = log_file_;
    playlist_file_ = playlist_file;
    fd_.resize(N, -1);
    process_state_.resize(N, UNINITIALIZED);
    my_state_ = UNINITIALIZED;
    transaction_id_ = 0;
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

void Process::set_fd(int process_id, int new_fd) {
    pthread_mutex_lock(&fd_lock);
    if (fd_[process_id] == -1)
    {
        fd_[process_id] = new_fd;
    }
    pthread_mutex_unlock(&fd_lock);
}

void Process::set_my_coordinator(int process_id) {
    my_coordinator_ = process_id;
    set_coordinator(process_id);
}

int Process::get_fd(int process_id) {
    int ret;
    pthread_mutex_lock(&fd_lock);
    ret = fd_[process_id];
    pthread_mutex_unlock(&fd_lock);
    return ret;
}

// print function for debugging purposes
void Process::Print() {
    cout << "pid=" << get_pid() << " ";
    for (int i = 0; i < N; ++i) {
        cout << fd_[i] << ",";
    }
    cout << endl;
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

    if (pthread_mutex_init(&log_lock, NULL) != 0) {
        cout << "P" << get_pid() << ": Mutex init failed" << endl;
        pthread_exit(NULL);
    }
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

    // pthread_t logger_thread;
    // if (pthread_create(&logger_thread, NULL, p->AddToLog(), (void *)p)) {
    //     cout << "P" << p->get_pid() << ": ERROR: Unable to create logger thread for P" << p->get_pid() << endl;
    //     pthread_exit(NULL);
    // }


    pthread_t server_thread;
    if (pthread_create(&server_thread, NULL, server, (void *)p)) {
        cout << "P" << p->get_pid() << ": ERROR: Unable to create server thread for P" << p->get_pid() << endl;
        pthread_exit(NULL);
    }
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
        if ((*it).compare("commit") || (*it).compare("abort") || (*it).compare("precommit"))
            return *it;
    }
    return "";
}

string Process::GetVote()
{
    vector<string> cur_trans_log = log_[transaction_id_];
    for (vector<string>::reverse_iterator it = cur_trans_log.rbegin(); it != cur_trans_log.rend(); ++it)
    {
        if ((*it).compare("yes"))
            return *it;

        else if ((*it).compare("abort"))
            return "abort";
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
        if (tokens[0].compare("start"))
        {
            vector<string> rv = split(tokens[1], ',');

            for (vector<string>::iterator it = rv.begin(); it < rv.end(); it++)
            {
                participants_.push_back(atoi((*it).c_str()));
            }
        }
    }

    else
    {
        if (tokens[0].compare("votereq"))
        {
            vector<string> rv = split(tokens[2], ',');
            for (vector<string>::iterator it = rv.begin(); it < rv.end(); it++)
            {
                participants_.push_back(atoi((*it).c_str()));
            }
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
        if (tokens[0].compare("votereq"))
        {
            return atoi(tokens[1].c_str());
        }
    }
}

// void Process::Recovery()
// {
//     LoadLog();
//     LoadTransactionId();
//     if(CheckCoordinator())
//     {
//         am_coordinator_ = true;

//         state = GetDecision()

//     }
// }

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

    for (int i = 0; i < participants_.size(); i++)
    {
        if (i)
            s += ",";
        s += to_string(participants_[i]);
    }

    AddToLog(s);
}

void Process::LogStart()
{
    string s = "start";
    s += " ";
    for (int i = 0; i < participants_.size(); i++)
    {
        if (i)
            s += ",";
        s += to_string(participants_[i]);
    }
    AddToLog(s);
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

