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
    // clear the master fd set
    // FD_ZERO(&master_fds_);

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
    // if (fd[process_id] == -1)
    // {
    fd_[process_id] = new_fd;
    // }
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
        cout << msg << endl;
    }
}

//send VOTE-REQ to all participants
void Process::SendVoteReqToAll(const string &msg) {
    for ( auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        // if ((it->first) == get_pid()) continue; // do not send to self

        if (send(get_fd(it->first), msg.c_str(), msg.size(), 0) == -1) {
            cout << "P" << get_pid() << ": ERROR: sending to process P" << (it->first) << endl;
        }
        else {
            cout << "P" << get_pid() << ": Msg sent to P" << (it->first) << ": " << msg << endl;
        }
    }
}

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
    ReceivedMsgType* r;
    for (int i = 0; i < n; ++i) {
        pthread_join(receive_thread[i], &status);
        r = (ReceivedMsgType*)status;
        if ((*r) == ERROR) {
            //TODO: not necessarily. handle
            pthread_exit(NULL);
        } else if ((*r) == YES) {
            // if a participant votes yes, mark its state as UNCERTAIN
            participant_state_map_[i] = UNCERTAIN;
        } else if ((*r) == NO) {
            // if a participant votes no, mark its state as ABORTED
            participant_state_map_[i] = ABORTED;
        } else if ((*r) == TIMEOUT) {
            // if a participant votes no, mark its state as PROCESSTIMEOUT
            participant_state_map_[i] = PROCESSTIMEOUT;
        }
    }
}

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
        rcv_thread_arg[i]->expected_msg1 = kYes;
        rcv_thread_arg[i]->expected_msg2 = "NULL";
        if (pthread_create(&receive_thread[i], NULL, ReceiveAckFromParticipant, (void *)rcv_thread_arg[i])) {
            cout << "P" << get_pid() << ": ERROR: Unable to create receive thread for P" << get_pid() << endl;
            pthread_exit(NULL);
        }
        i++;
    }

    void* status;
    ReceivedMsgType* r;
    for (int i = 0; i < n; ++i) {
        pthread_join(receive_thread[i], &status);
        r = (ReceivedMsgType*)status;
        if ((*r) == ERROR) {
            //TODO: not necessarily. handle
            pthread_exit(NULL);
        } else if ((*r) == ACK) {
            // if a participant ACKed, good for you
            // no need to do anything
        } else if ((*r) == TIMEOUT) {
            // if a participant timedout in ACK, skip it
            // no need to do anything
        }
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
        cout << "P" << get_pid() << ": ERROR: sending to process P" << process_id << endl;
    }
    else {
        cout << "P" << get_pid() << ": Msg sent to P" << process_id << ": " << msg << endl;
    }
}

// send PRE-COMMIT to all participants
void Process::SendPreCommitToAll() {
    string msg;
    ConstructGeneralMsg(kPreCommit, transaction_id_, msg);
    for ( auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        // if ((it->first) == get_pid()) continue; // do not send to self

        if (send(get_fd(it->first), msg.c_str(), msg.size(), 0) == -1) {
            cout << "P" << get_pid() << ": ERROR: sending to process P" << (it->first) << endl;
        }
        else {
            cout << "P" << get_pid() << ": Msg sent to P" << (it->first) << ": " << msg << endl;
        }
    }
}

// send COMMIT to all participants
void Process::SendCommitToAll() {
    string msg;
    ConstructGeneralMsg(kCommit, transaction_id_, msg);
    for ( auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        // if ((it->first) == get_pid()) continue; // do not send to self

        if (send(get_fd(it->first), msg.c_str(), msg.size(), 0) == -1) {
            cout << "P" << get_pid() << ": ERROR: sending to process P" << (it->first) << endl;
        }
        else {
            cout << "P" << get_pid() << ": Msg sent to P" << (it->first) << ": " << msg << endl;
        }
    }
}

void Process::CoordinatorMode() {
    set_my_coordinator(0);
    //TODO: handle transaction IDs
    //TODO: increment it
    //TODO: send it to ConstructVoteReq;

    // connect to each participant
    for (int i = 0; i < N; ++i) {
        if (i == get_pid()) continue;
        if (ConnectToProcess(i))
            participant_state_map_.insert(make_pair(i, UNINITIALIZED));
    }

    string msg;
    ConstructVoteReq(msg);
    SendVoteReqToAll(msg);
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

    if (abort) {
        // send ABORT message to all participants which voted YES
        for (const auto& ps : participant_state_map_) {
            if (ps.second == UNCERTAIN && ps.first != get_pid()) {
                //TODO: make sure that transaction_id_ is handled correct after TODO at start of this fn
                SendAbortToProcess(ps.first);
            }
        }
    } else {
        SendPreCommitToAll();
        WaitForAck();
        SendCommitToAll();
    }
}

// takes as input the received_msg
// extracts the core message body from it to extracted_msg
// extracts transaction id in the received_msg to received_tid
void Process::ExtractMsg(const string &received_msg, string &extracted_msg, int &received_tid) {
    std::istringstream iss(received_msg);
    iss >> extracted_msg;
    iss >> received_tid;
    // cout << "ExtractedMsg:" << extracted_msg << " " << "Receivedtid:" << received_tid << endl;
}

// thread for receiving message from processes
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
        received_msg_type = ERROR;
    } else if (rv == 0) {   //timeout
        received_msg_type = TIMEOUT;
    } else {    // activity happened on the socket
        if ((num_bytes = recv(p->get_fd(pid), buf, kMaxDataSize - 1, 0)) == -1) {
            cout << "P" << p->get_pid() << ": ERROR in receiving for P" << pid << endl;
            received_msg_type = ERROR;
        } else if (num_bytes == 0) {     //connection closed
            cout << "P" << p->get_pid() << ": Connection closed by P" << pid << endl;
            // if participant closes connection, it is equivalent to it crashing
            // can treat it as TIMEOUT
            // TODO: verify argument
            received_msg_type = TIMEOUT;
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
                received_msg_type = YES;
            } else if (extracted_msg == msg2) { // it's a NO
                received_msg_type = NO;
            } else {
                //TODO: take actions appropriately, like check log for previous transaction decision.
                cout << "P" << p->get_pid() << ": Unexpected msg received from P" << pid << endl;
                received_msg_type = ERROR;
            }
        }
    }
    cout << "P" << p->get_pid() << " Receive thread exiting for P" << pid << endl;
    return &received_msg_type;
}

// thread for receiving ACK messages from processes
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
        received_msg_type = ERROR;
    } else if (rv == 0) {   //timeout
        received_msg_type = TIMEOUT;
    } else {    // activity happened on the socket
        if ((num_bytes = recv(p->get_fd(pid), buf, kMaxDataSize - 1, 0)) == -1) {
            cout << "P" << p->get_pid() << ": ERROR in receiving for P" << pid << endl;
            received_msg_type = ERROR;
        } else if (num_bytes == 0) {     //connection closed
            cout << "P" << p->get_pid() << ": Connection closed by P" << pid << endl;
            // if participant closes connection, it is equivalent to it crashing
            // can treat it as TIMEOUT
            // TODO: verify argument
            received_msg_type = TIMEOUT;
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
                received_msg_type = ACK;
            } else {
                //TODO: take actions appropriately, like check log for previous transaction decision.
                cout << "P" << p->get_pid() << ": Unexpected msg received from P" << pid << endl;
                received_msg_type = ERROR;
            }
        }
    }
    cout << "P" << p->get_pid() << " Receive thread exiting for P" << pid << endl;
    return &received_msg_type;
}

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
    // sleep for 3 seconds to make sure server is up and listening
    usleep(1000 * 1000);

    // if pid=0, then it is the coordinator
    if (p->get_pid() == 0) {
        p->CoordinatorMode();
        cout << "Coord Over" << endl;
    }

    // if pid!=0, then it needs to connect only to coordinator
    // by default, coordinator will be zero
    // p->ConnectToProcess(0);
    // sleep for 1 seconds to make sure all connections are set up
    usleep(1000 * 1000);


    // usleep(5000 * 1000);
    // if (p->get_pid() != 0) {
    //     string msg = "YES 0 $";
    //     if (send(p->get_fd(0), msg.c_str(), msg.size(), 0) == -1)
    //     {
    //         cout << "P" << p->get_pid() << ": ERROR: sending to P0" << endl;
    //         exit(1);
    //     }
    //     else {
    //         cout << "P" << p->get_pid() << ": Msg sent to P0:" << msg << endl;
    //     }
    // }

    void* status;
    // pthread_join(receive_thread, &status);
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
        ss<<"TID: "<<to_string(transaction_id_)<<endl<<s;
        s = ss.str();
    }
        
    else
        log_[transaction_id_].push_back(s);
    

    ofstream outfile(log_file_.c_str(), fstream::app);
    if (outfile.is_open())
        outfile<<s<<endl;
    
    else
        cout<<"couldn't open"<<log_file_<<endl;

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
        if(myfile.is_open())
        {   
            while(getline(myfile,line))
            {
                if(line.empty())
                    continue;

                size_t found = line.find("TID:");
                if (found!=string::npos)
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
            cout<<"Failed to load log file"<<endl;
        }
}

void Process::LoadTransactionId()
{
    if(!log_.empty())
        transaction_id_ = log_.rbegin()->first;
    else
        cout<<"Error. Log empty"<<endl;
}

bool Process::CheckCoordinator()
{
    transaction_id_ = (log_.rbegin())->first;
    size_t found = log_[transaction_id_][0].find("start");
    if (found!=string::npos)
        return true;
    else
        return false;
}

string Process::GetDecision()
{
    //null string means no decision
    vector<string> cur_trans_log = log_[transaction_id_];
    for(vector<string>::reverse_iterator it = cur_trans_log.rbegin(); it!=cur_trans_log.rend(); ++it)
    {
        if((*it).compare("commit") || (*it).compare("abort") || (*it).compare("precommit"))
            return *it;
    }
    return "";
}

string Process::GetVote()
{
    vector<string> cur_trans_log = log_[transaction_id_];
    for(vector<string>::reverse_iterator it = cur_trans_log.rbegin(); it!=cur_trans_log.rend(); ++it)
    {
        if((*it).compare("yes"))
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

            for(vector<string>::iterator it=rv.begin(); it<rv.end(); it++)
            {
                participants_.push_back(atoi((*it).c_str()));
            }
        }
    }

    else
    {
        if(tokens[0].compare("votereq"))
        {
            vector<string> rv = split(tokens[2], ',');
            for(vector<string>::iterator it=rv.begin(); it<rv.end(); it++)
            {
                participants_.push_back(atoi((*it).c_str()));
            }   
        }
    }

}

int Process::GetCoordinator()
{
    if(CheckCoordinator())
        return pid_;
    else
    {
        string entry = log_[transaction_id_][0];
        vector<string> tokens = split(entry, ' ');
        if(tokens[0].compare("votereq"))
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
    s+= " ";
    s+= to_string(my_coordinator_);
    s+=" ";

    for(int i=0; i<participants_.size(); i++)
    {   
        if(i)
            s+=",";
        s+=to_string(participants_[i]);
    }

    AddToLog(s);
}

void Process::LogStart()
{
    string s = "start";
    s+= " ";
    for(int i=0; i<participants_.size(); i++)
    {   
        if(i)
            s+=",";
        s+=to_string(participants_[i]);
    }
    AddToLog(s);
}



vector<string> split(string s, char delimiter)
{
    stringstream ss(s);
    string temp;
    vector<string> rval;
    while(getline(ss, temp, delimiter))
    {
        rval.push_back(temp);
    }
    return rval;
}

