#include "process.h"
#include "constants.h"
#include "limits.h"
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

    // TODO: share all internal thread info with controller
    // TODO: think whether we need to add the temporary receive threads as well?
    pthread_t server_thread;
    p->CreateThread(server_thread, server, (void *)p);

    // sleep to make sure server is up and listening
    // usleep(kGeneralSleep);

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
    if(process_id == INT_MAX) return -1;
    int ret;
    pthread_mutex_lock(&fd_lock);
    ret = fd_[process_id];
    pthread_mutex_unlock(&fd_lock);
    return ret;
}

// get socket fd corresponding to process_id's alive connection
int Process::get_alive_fd(int process_id) {
    if(process_id == INT_MAX) return -1;
    int ret;
    pthread_mutex_lock(&alive_fd_lock);
    ret = alive_fd_[process_id];
    pthread_mutex_unlock(&alive_fd_lock);
    return ret;
}

int Process::get_sdr_fd(int process_id) {
    if(process_id == INT_MAX) return -1;
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

void Process::set_my_state(ProcessState state)
{
    my_state_ = state;
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
        RemoveFromUpSet(process_id);
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
void Process::CreateAliveThreads(vector<pthread_t> &receive_alive_threads, pthread_t &send_alive_thread) {

    int n = up_.size();
    // 

    ReceiveAliveThreadArgument **rcv_thread_arg = new ReceiveAliveThreadArgument*[n];
    int i = 0;
    for (auto it = up_.begin(); it != up_.end(); ++it ) {
        rcv_thread_arg[i] = new ReceiveAliveThreadArgument;
        rcv_thread_arg[i]->p = this;
        rcv_thread_arg[i]->pid_from_whom = *it;
        CreateThread(receive_alive_threads[i], ReceiveAlive, (void *)rcv_thread_arg[i]);
        i++;
    }
    // CreateThread(receive_alive_thread, ReceiveAlive, (void *)this);
    CreateThread(send_alive_thread, SendAlive, (void *)this);
}

void Process::CreateSDRThread(int process_id, pthread_t &sdr_receive_thread) {
    ReceiveSDRThreadArgument *rcv_thread_arg = new ReceiveSDRThreadArgument;
    rcv_thread_arg-> p = this;
    rcv_thread_arg->pid_to_whom = process_id;
    CreateThread(sdr_receive_thread, ReceiveStateOrDecReq, (void *)rcv_thread_arg);
}

void Process::AddToLog(string s, bool new_round)
{
    pthread_mutex_lock(&log_lock);

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

    pthread_mutex_unlock(&log_lock);
    outfile.close();
    return;
}

void Process::LoadLogAndPrevDecisions()
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

            if(line=="commit")
                prev_decisions_.push_back(COMMIT);
            else if(line=="abort")
                prev_decisions_.push_back(ABORT);

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


void Process::LoadUp()
{
    vector<string> cur_trans_log = log_[transaction_id_];
    vector<string> temp;
    up_.clear();
    for (vector<string>::reverse_iterator it = cur_trans_log.rbegin(); it != cur_trans_log.rend(); ++it)
    {
        temp = split(*it, ' ');
        if (temp[0]=="up:")
        {   
            temp = split(temp[1], ',');
            for(auto it = temp.begin(); it!=temp.end(); it++)
                up_.insert(atoi((*it).c_str()));
            return;
        }
    }
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
    LoadLogAndPrevDecisions();
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
    string msg;
    string msg_to_send = kDecReq;
    ConstructGeneralMsg(msg_to_send, transaction_id_, msg);
    //if total failure, then init termination protocol with total failure. give arg to TP
    while(!(my_state_==ABORTED || my_state_==COMMITTED))
    {
        SendDecReqToAll(msg);
        WaitForDecisionResponse();
        if(my_state_ == ABORTED)
            LogAbort();
        else if(my_state_ == COMMITTED)
            LogCommit();
        //sleep for some time
        usleep(kDecReqTimeout);
    }
}

void Process::WaitForDecisionResponse() {
    int n = participant_state_map_.size();
    std::vector<pthread_t> receive_thread(n);

    ReceiveDecThreadArgument **rcv_thread_arg = new ReceiveDecThreadArgument*[n];
    int i = 0;
    for (auto it = participant_state_map_.begin(); it != participant_state_map_.end(); ++it ) {
        rcv_thread_arg[i] = new ReceiveDecThreadArgument;
        rcv_thread_arg[i]->p = this;
        rcv_thread_arg[i]->pid = it->first;
        rcv_thread_arg[i]->transaction_id = transaction_id_;
        // rcv_thread_arg[i]->decision;
        
        CreateThread(receive_thread[i], ReceiveDecision, (void *)rcv_thread_arg[i]);
        i++;
    }
    
    void* status;
    
    i = 0;
    for (auto it = participants_.begin(); it != participants_.end(); ++it ) {
        pthread_join(receive_thread[i], &status);
        RemoveThreadFromSet(receive_thread[i]);
        if ((rcv_thread_arg[i]->decision) == COMMIT) 
        {
            my_state_ = COMMITTED;
            return;
        }
        else if((rcv_thread_arg[i]->decision) == ABORT)
        {
            my_state_ = ABORTED;
            return;
        }
        i++;
    }
}

void* ReceiveDecision(void* _rcv_thread_arg)
{
    ReceiveDecThreadArgument *rcv_thread_arg = (ReceiveDecThreadArgument *)_rcv_thread_arg;
    int pid = rcv_thread_arg->pid;
    int tid = rcv_thread_arg->transaction_id;
    Process *p = rcv_thread_arg->p;

    char buf[kMaxDataSize];
    int num_bytes;
    //TODO: write code to extract multiple messages

    fd_set temp_set;
    FD_ZERO(&temp_set);
    FD_SET(p->get_sdr_fd(pid), &temp_set);
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
            cout << "P" << p->get_pid() << ": DecMsg received from P" << pid << ": " << buf <<  endl;

            string extracted_msg;
            int received_tid;
            // in this case, we don't care about the received_tid,
            // because it will surely be for the transaction under consideration
            p->ExtractMsg(string(buf), extracted_msg, received_tid);

            Decision msg = static_cast<Decision>(atoi(extracted_msg.c_str()));
            rcv_thread_arg->decision = msg;
            //assumes that correct message type is sent by participant
        }
    }
    // cout << "P" << p->get_pid() << ": Receive thread exiting for P" << pid << endl;
    return NULL;
}


void Process::SendDecReqToAll(const string &msg) {

    //this only contains operational processes for non timeout cases
    for ( auto it = participants_.begin(); it != participants_.end(); ++it ) {
        // if ((it->first) == get_pid()) continue; // do not send to self
        if (send(get_sdr_fd(*it), msg.c_str(), msg.size(), 0) == -1) {
            cout << "P" << get_pid() << ": ERROR: sending to P" << (*it) << endl;
            RemoveFromUpSet(*it);
        }
        else {
            cout << "P" << get_pid() << ": Msg sent to P" << (*it) << ": " << msg << endl;
        }
    }
}


void Process::TerminationProtocol()
{   //called when a process times out.

    //reset statereq variable
    // state_req_in_progress = true;
    //sets new coord
    cout << "TerminationProtocol by" << get_pid()<< " at "<<time(NULL)%100<<endl;
    bool status = false;
    // while(!status){
        ElectionProtocol();


        // if(my_coordinator_==1)
        //     cout << "P" << pid_ << ": my new coordinator=one"<< endl;
        // else if(my_coordinator_==2)
        //     cout << "P" << pid_ << ": my new coordinator=two"<< endl;

        if (pid_ == my_coordinator_)
        {   //coord case
            //pass on arg saying total failue, then send to all
            void* status;
            CreateThread(newcoord_thread, NewCoordinatorMode, (void *)this);
            pthread_join(newcoord_thread, &status);
            RemoveThreadFromSet(newcoord_thread);
        }
        else
        {
            state_req_in_progress = false;
            status = SendURElected(my_coordinator_);
            usleep(kGeneralTimeout);
            if(state_req_in_progress)
                return;
            TerminationProtocol();

            // WaitForStateRequest();
            //wait for 3 sec
            //check if state req has been received using shared memory


            //then do nothing here, the initial SR thread will see that a SR message is here
            //so that replies state and does all that shit
            //till we get a decision
            // TerminationParticipantMode();
        }
    // }
}
void Process::set_state_req_in_progress(bool val)
{
    state_req_in_progress = val;
}

void Process::ElectionProtocol()
{
    int min = GetNewCoordinator();
    set_my_coordinator(min);

}

bool Process::SendURElected(int recp)
{
    //send it on SR thread only
    string msg;
    string msg_to_send = kURElected;
    ConstructGeneralMsg(msg_to_send, transaction_id_, msg);
    if (send(get_sdr_fd(recp), msg.c_str(), msg.size(), 0) == -1) {
        cout << "P" << get_pid() << ": ERROR: sending to P" << recp << endl;
        RemoveFromUpSet(recp);
        return false;
    }
    else {
        cout << "P" << get_pid() << ": URElected Msg sent to P" << recp << ": " << msg << endl;
        return true;
    }

}

int Process::GetNewCoordinator()
{
    // ofstream ofile("log/selectnewcoord"+to_string(get_pid())+","+to_string(time(NULL)%100));
    int min;
    pthread_mutex_lock(&up_lock);
    unordered_set<int> copy_up(up_);
    pthread_mutex_unlock(&up_lock);

    for ( auto it = copy_up.cbegin(); it != copy_up.cend(); ++it )
    {
        // ofile<<*it;
        if (it == copy_up.cbegin())
            min = (*it);
        else
        {
            if (*it < min)
                min = *it;
        }
    }
    
    if(min>get_pid())
        min = get_pid();
    // ofile<<"min: "<<min<<endl;
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

void Process::LogUp()
{
    string s = "up:";
    s+=" ";
    //TODO:mutex lock up
    pthread_mutex_lock(&up_lock);
    unordered_set<int> copy_up_ = up_;
    pthread_mutex_unlock(&up_lock);

    for ( auto it = copy_up_.begin(); it != copy_up_.end(); ++it )
    {   
        if(it!=copy_up_.begin())
            s+=",";
        s+=to_string(*it);
    }
    AddToLog(s);
}

void Process::SendState(int recp)
{
    string msg;
    string msg_to_send = to_string((int)my_state_);
    ConstructGeneralMsg(msg_to_send, transaction_id_, msg);

    if(recp==INT_MAX)
        return;

    if (send(get_fd(recp), msg.c_str(), msg.size(), 0) == -1) {
        cout << "P" << get_pid() << ": ERROR: sending to P" << recp << endl;
        RemoveFromUpSet(recp);
    }
    else {
        cout << "P" << get_pid() << ": Msg sent to P" << recp << ": " << msg << endl;
    }
}

void Process::SendDecision(int recp)
{
    string msg;
    int code_to_send;
    if (my_state_==COMMITTED)
        code_to_send = COMMIT;
    else if(my_state_==ABORTED)
        code_to_send = ABORT;
    string msg_to_send = to_string(code_to_send);
    ConstructGeneralMsg(msg_to_send, transaction_id_, msg);
    if (send(get_fd(recp), msg.c_str(), msg.size(), 0) == -1) {
        cout << "P" << get_pid() << ": ERROR: sending to P" << recp << endl;
        RemoveFromUpSet(recp);   
    }
    else {
        cout << "P" << get_pid() << ": Msg sent to P" << recp << ": " << msg << endl;
    }
}

void Process::SendPrevDecision(int recp, int tid)
{
    string msg;
    int code_to_send = prev_decisions_[tid];
    string msg_to_send = to_string(code_to_send);
    ConstructGeneralMsg(msg_to_send, transaction_id_, msg);
    if (send(get_fd(recp), msg.c_str(), msg.size(), 0) == -1) {
        cout << "P" << get_pid() << ": ERROR: sending to P" << recp << endl;
        RemoveFromUpSet(recp);
    }
    else {
        cout << "P" << get_pid() << ": Msg sent to P" << recp << ": " << msg << endl;
    }
}

int Process::get_transaction_id()
{
    return transaction_id_;
}

void Process::CloseFDs()
{
    for(auto it = fd_.begin(); it!=fd_.end(); it++)
    {
        if((*it)!=-1)
            close(*it);
    }
}
void Process::CloseAliveFDs()
{

    for(auto it = alive_fd_.begin(); it!=alive_fd_.end(); it++)
    {
        if((*it)!=-1)
            close(*it);
    }
}
void Process::CloseSDRFDs()
{

    for(auto it = sdr_fd_.begin(); it!=sdr_fd_.end(); it++)
    {
        if((*it)!=-1)
            close(*it);
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

