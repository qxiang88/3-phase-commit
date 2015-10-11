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

bool Process::LoadLogAndPrevDecisions()
{
    string line;
    vector<string> trans_log;
    int round_id;
    bool round_assigned = false;
    string last_uninserted_up;
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

                if(!round_assigned && !last_uninserted_up.empty()){
                log_[round_id].push_back(last_uninserted_up);                    
                }

                round_assigned = true;;
            }
            else if(round_assigned)
            {
                log_[round_id].push_back(line);

                if (line == "commit")
                    prev_decisions_.push_back(COMMIT);
                else if (line == "abort")
                    prev_decisions_.push_back(ABORT);
            }
            else{
                last_uninserted_up = line;
            }

        }
        myfile.close();
        return true;
    }
    else
    {
        cout << "Failed to load log file" << endl;
        return false;
    }
}

void Process::LoadTransactionId()
{
    if (!log_.empty())
        transaction_id_ = log_.rbegin()->first;
    // cout<<transaction_id_<<endl;}

    else
        cout << "Error. Log empty" << endl;
}

// bool Process::CheckCoordinator()
// {
//     transaction_id_ = (log_.rbegin())->first;
//     size_t found;
//     for(auto it= log_[transaction_id_].begin(); it!=log_[transaction_id_].end(); it++)
//         {
//             found = log_[transaction_id_][*it].find("start");
//             if (found != string::npos)
//                 return true;
    
//         }
//     return false;
// }

void Process::LoadUp()
{
    vector<string> cur_trans_log = log_[transaction_id_];
    vector<string> temp;
    up_.clear();
    for (vector<string>::reverse_iterator it = cur_trans_log.rbegin(); it != cur_trans_log.rend(); ++it)
    {
        temp = split(*it, ' ');
        if (temp[0] == "up:")
        {
            temp = split(temp[1], ',');
            for (auto it = temp.begin(); it != temp.end(); it++)
                up_.insert(atoi((*it).c_str()));
            break;
        }
    }
    up_.insert(get_pid());
    return;
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
    string entry;
    vector<string> tokens;

    //assumes first entry in round will have participants. change if not
    for (auto i = log_[transaction_id_].begin(); i != log_[transaction_id_].end(); i++)
    {
        entry = *i;
        tokens = split(entry, ' ');
        if(tokens[0]!="up:")
            break;
    }
    // vector<string> tokens = split(entry, ' ');

    participants_.clear();

    vector<string> rv = split(tokens[2], ',');
    for (vector<string>::iterator it = rv.begin(); it < rv.end(); it++)
    {
        participants_.insert(atoi((*it).c_str()));
    }

    participants_.insert(atoi(tokens[1].c_str()));


}

// int Process::GetCoordinator()
// {

//     transaction_id_ = (log_.rbegin())->first;
//     size_t found,found2;
//     for(auto it= log_[transaction_id_].begin(); it!=log_[transaction_id_].end(); it++)
//         {
//             found = log_[transaction_id_][*it].find("start");
//             if (found != string::npos)
//                 return *it;

//             found2 = log_[transaction_id_][*it].find("votereq");
//             if(found2!=string::npos)
//                 {
//                     entry = log_[transaction_id_][*it];
//                     vector<string> tokens = split(entry, ' ');
//                     return atoi(tokens[1].c_str());
//                 }
//         }
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

void Process::LogVoteReq(int c_id)
{

    string s = "votereq";
    s += " ";
    s += to_string(c_id);
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
    s += to_string(pid_);
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
    s += " ";
    //TODO:mutex lock up
    pthread_mutex_lock(&up_lock);
    set<int> copy_up_ = up_;
    pthread_mutex_unlock(&up_lock);

    for ( auto it = copy_up_.begin(); it != copy_up_.end(); ++it )
    {
        if (it != copy_up_.begin())
            s += ",";
        s += to_string(*it);
    }
    AddToLog(s);
}


void Process::LogCommitOrAbort()
{
    cout << "Decided. My state is ";
    if (get_my_state() == ABORTED)
    {
        LogAbort();
        cout << "Aborted" << endl;
    }
    else if (get_my_state() == COMMITTED)
    {
        cout << "Commited" << endl;
        LogCommit();
    }
}