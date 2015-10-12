#include "process.h"
#include "constants.h"
#include "iostream"
#include "fstream"
#include "unistd.h"
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
using namespace std;

extern pthread_mutex_t up_fd_lock;
extern pthread_mutex_t previous_up_lock;

string ConvertSetToString(set<int> a)
{
    string rval;
    for(auto it=a.begin(); it!=a.end(); it++)
        {
            if(it!=a.begin())
                rval+=".";
            rval+=to_string(*it);
        }
    return rval;
}

set<int> ConvertStringToSet(string s){
    vector<string> ids = split(s,'.');
    set<int> rv;
    for(auto it=ids.begin(); it!=ids.end(); it++)
        rv.insert(atoi((*it).c_str()));
    // cout<<"string to set: "<<rv.size()<<endl;
    return rv;
}


void Process::SendUpReqToAll() {
    string msg;
    ConstructUpReq(msg);
    // cout<<"msg : "<<msg<<" :";
    //this only contains operational processes for non timeout cases
    // cout<<participants_.size()<<endl;
    for ( auto it = participants_.begin(); it != participants_.end(); ++it ) {
        // cout<<"trying to send to "<<*it<<endl;
        if ((*it) == get_pid()) continue; // do not send to self
        if (send(get_up_fd(*it), msg.c_str(), msg.size(), 0) == -1) {
            // cout << "P" << get_pid() << ": ERROR: sending up req to P" << (*it) << endl;
        }
        else {
            // cout << "P" << get_pid() << ": Up req sent to P" << (*it) << " on "<<get_up_fd(*it)<<": " << msg << endl;
        }
    }
}

bool Process::ConnectToProcessUp(int process_id) {
    if (get_up_fd(process_id) != -1) return true;
    int sockfd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *clientinfo, *l;

    struct sigaction sa;
    int yes = 1;
    int rv;

    // set up addrinfo for client (i.e. self)
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
    if ((rv = getaddrinfo(NULL, std::to_string(get_up_port(get_pid())).c_str(),
                          &hints, &clientinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit (1);
    }

    // loop through all the results and bind to the first we can
    for (l = clientinfo; l != NULL; l = l->ai_next)
    {
        if ((sockfd = socket(l->ai_family, l->ai_socktype,
                             l->ai_protocol)) == -1) {
            perror("client: socket ERROR");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1) {
            perror("setsockopt ERROR");
            exit(1);
        }

        if (bind(sockfd, l->ai_addr, l->ai_addrlen) == -1) {
            close(sockfd);
            perror("client: bind ERROR");
            continue;
        }

        break;
    }
    freeaddrinfo(clientinfo); // all done with this structure
    if (l == NULL)  {
        fprintf(stderr, "client: failed to bind\n");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }


    // set up addrinfo for server
    int numbytes;
    struct addrinfo *servinfo;
    char s[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, std::to_string(get_listen_port(process_id)).c_str(),
                          &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return false;
    }

    // loop through all the results and connect to the first we can
    for (l = servinfo; l != NULL; l = l->ai_next)
    {
        if (connect(sockfd, l->ai_addr, l->ai_addrlen) == -1) {
            close(sockfd);
            // cout << "P" << get_pid() << ": Client1: connect ERROR"<<endl;
            continue;
        }

        break;
    }
    if (l == NULL) {
        // fprintf(stderr, "client: failed to connect\n");
        return false;
    }
    int outgoing_port = ntohs(return_port_no((struct sockaddr *)l->ai_addr));
    // cout << "P" << get_pid() << ": Client: connecting to " << outgoing_port << endl ;
    freeaddrinfo(servinfo); // all done with this structure
    set_up_fd(process_id, sockfd);
    // cout << "P" << get_pid() << ": Initiating Up connection to P" << process_id << endl;
    return true;
}

// thread for receiving SR/DR messages from one process
void* ReceiveUp(void* _arg) {
    ReceiveSDRUpThreadArgument* arg = (ReceiveSDRUpThreadArgument*)_arg;
    Process* p = arg->p;
    int for_whom = arg->for_whom;

    char buf[kMaxDataSize];
    int num_bytes;
    pthread_t sr_response_thread;

    set<string> many_messages;

    ofstream outf("log/up/" + to_string(p->get_pid()) + "from" + to_string(for_whom));

    if (!outf.is_open())
        cout << "Failed to open log file for up" << endl;
    outf<<"All set to receive"<<endl;
    while (true) {
        // cout<<"in "<<p->get_pid()<<"; "<<for_whom<<" fd is "<<p->get_up_fd(for_whom)<<endl;
        if ((num_bytes = recv(p->get_up_fd(for_whom), buf, kMaxDataSize - 1, 0)) == -1)
        {
            // cout << "P" << p->get_pid() << ": ERROR in receiving UP for P" << for_whom << endl;
            // p->RemoveFromUpSet(for_whom);
        }
        else if (num_bytes == 0)
        {   //connection closed
            // cout << "P" << p->get_pid() << ": UP connection closed by P" << for_whom << endl;
            // p->RemoveFromUpSet(for_whom);
        }
        else
        {
            buf[num_bytes] = '\0';
            string bufstring(buf);
            vector<string> all_msgs = split(bufstring, '$');
            for (auto iter = all_msgs.begin(); iter != all_msgs.end(); iter++)
            {
                many_messages.insert(*iter);
            }
            string extracted_msg;
            int recvd_tid;

            for(auto iter = many_messages.begin(); iter!=many_messages.end(); iter++)
            {
                p->ExtractMsg(*iter, extracted_msg, recvd_tid);
                outf << "P" << p->get_pid() << ": UP recevd from P" << for_whom << ": " << *iter <<  endl;
                if (extracted_msg == kUpReq && recvd_tid == p->get_transaction_id())
                { 
                    //upreq sent only in total failure. means everyone is in same trans.
                    //if lower upreq got, ignore
                    p->SendMyUp(for_whom);
                }
                else 
                {//up set received
                    // outf<<"Up Set received "<<extracted_msg<<endl;
                    p->all_up_sets_[for_whom] = ConvertStringToSet(extracted_msg);
                    outf<<"up set assigned "<< ConvertSetToString(p->all_up_sets_[for_whom])<<endl;
                }
            }

        }
        usleep(kGeneralSleep);
    }
}

// void Process::WaitForUpResponse() {
//     int n = participants_.size();
//     std::vector<pthread_t> up_receive_thread(n);
//     ReceiveUpThreadArgument **rcv_thread_arg = new ReceiveUpThreadArgument*[n];
//     int i = 0;
//     for (auto it = participants_.begin(); it != participants_.end(); ++it ) { 
//         if(*it==get_pid())continue;
//         rcv_thread_arg[i] = new ReceiveUpThreadArgument;
//         rcv_thread_arg[i]->p = this;
//         rcv_thread_arg[i]->pid = *it;
//         rcv_thread_arg[i]->transaction_id = transaction_id_;
//         rcv_thread_arg[i]->received_msg_type = YES;
//         CreateThread(up_receive_thread[i], ReceiveUpSet, (void *)rcv_thread_arg[i]);
//         i++;
//     }

//     void* status;

//     i = 0;
//     for (auto it = participants_.begin(); it != participants_.end(); ++it ) {
//         if(*it==get_pid())continue;
//         pthread_join(up_receive_thread[i], &status);
//         RemoveThreadFromSet(up_receive_thread[i]);
//         if ((rcv_thread_arg[i]->received_msg_type==ERROR) ||  (rcv_thread_arg[i]->received_msg_type==TIMEOUT))
//         {
//             cout<<"Didn't receive up from "<<(*it)<<endl;
//         }
//         else
//         {
//             // cout<<"assigned up set"<<endl;
//             all_up_sets_[*it] = rcv_thread_arg[i]->up;
//             // return;
//         }
//         i++;
//     }
// }

void Process::ConstructUpReq(string &msg) {
    msg = kUpReq + " " + to_string(transaction_id_);
    msg = msg + " $";
}

void Process::ConstructUpResponse(string &msg) {
    msg = ConvertUpSetToString() + " ";
    msg += to_string(transaction_id_);
    msg = msg + " $";
}


string Process::ConvertUpSetToString()
{
    //adds self if i have not failed
    pthread_mutex_lock(&previous_up_lock);
    set<int> up_copy(previous_up_);
    pthread_mutex_unlock(&previous_up_lock);
    

    string rval;
    for(auto it=up_copy.begin(); it!=up_copy.end(); it++)
        {
            if(it!=up_copy.begin())
                rval+=".";
            rval+=to_string(*it);
        }

        //check HERE

    // if(get_my_status()==RECOVERY)
    // {
    //     rval += ".";
    //     rval += to_string(get_pid());
    // }
    return rval;
}

void Process::SendMyUp(int pid_other){
    string msg;
    ConstructUpResponse(msg);
    ofstream outer("log/sendup"+to_string(get_pid()), fstream::app);

    if (send(get_up_fd(pid_other), msg.c_str(), msg.size(), 0) == -1) {
        outer << "P" << get_pid() << ": ERROR: sending my up to P" << pid_other << endl;
        RemoveFromUpSet(pid_other);
    }
    else {
        outer << "P" << get_pid() << ": Up set sent to P" << pid_other<<endl;
         // << ": " << msg << endl;
    }   
    outer.close();
}

// void* ReceiveUpSet(void* _rcv_thread_arg)
// {
//     ReceiveUpThreadArgument *rcv_thread_arg = (ReceiveUpThreadArgument *)_rcv_thread_arg;
//     int pid = rcv_thread_arg->pid;
//     int tid = rcv_thread_arg->transaction_id;
//     Process *p = rcv_thread_arg->p;


//     ofstream outf("log/upresponse/" + to_string(p->get_pid()) + "from" + to_string(pid));

//     if (!outf.is_open())
//         cout << "Failed to open log file for upresponse" << endl;

//     char buf[kMaxDataSize];
//     int num_bytes;
//     //TODO: write code to extract multiple messages

//     fd_set temp_set;
//     FD_ZERO(&temp_set);
//     FD_SET(p->get_up_fd(pid), &temp_set);
//     int fd_max = p->get_up_fd(pid);
//     int rv;

//     rv = select(fd_max + 1, &temp_set, NULL, NULL, (timeval*)&kTimeout);
//     if (rv == -1) { //error in select
//         cout << "P" << p->get_pid() << ": ERROR in select() for P" << pid << endl;
//         rcv_thread_arg->received_msg_type = ERROR;
//     } else if (rv == 0) {   //timeout
//         cout<<"timedout"<<endl;
//         rcv_thread_arg->received_msg_type = TIMEOUT;
//     } else {    // activity happened on the socket
//         if ((num_bytes = recv(p->get_up_fd(pid), buf, kMaxDataSize - 1, 0)) == -1) {
//             cout << "P" << p->get_pid() << ": ERROR in receiving for P" << pid << endl;
//             rcv_thread_arg->received_msg_type = ERROR;
//         } else if (num_bytes == 0) {     //connection closed
//             cout << "P" << p->get_pid() << ": Connection closed by P" << pid << endl;
//             rcv_thread_arg->received_msg_type = TIMEOUT;
//         } else {
//             buf[num_bytes] = '\0';
//             outf << "P" << p->get_pid() << ": Up set received from P" << pid << endl;//": "<<buf<<endl;
//             string extracted_msg;
//             int received_tid;
//             p->ExtractMsg(string(buf), extracted_msg, received_tid);            
//             rcv_thread_arg->up = convertStringToSet(extracted_msg);
//         }
//     }
//     // cout << "P" << p->get_pid() << ": Receive thread exiting for P" << pid << endl;
//     return NULL;
// }

void* SendUpReq(void *_p) {
    Process *p = (Process *)_p;
    p->TotalFailure();
    return NULL;
}
