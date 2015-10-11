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
using namespace std;

extern pthread_mutex_t up_fd_lock;


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
            cout << "P" << get_pid() << ": ERROR: sending to P" << (*it) << endl;
        }
        else {
            cout << "P" << get_pid() << ": Up req sent to P" << (*it) << " on "<<get_up_fd(*it)<<": " << msg << endl;
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
            cout << "P" << get_pid() << ": Client1: connect ERROR\n";
            continue;
        }

        break;
    }
    if (l == NULL) {
        fprintf(stderr, "client: failed to connect\n");
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
void* ReceiveUpReq(void* _arg) {
    ReceiveSDRUpThreadArgument* arg = (ReceiveSDRUpThreadArgument*)_arg;
    Process* p = arg->p;
    int for_whom = arg->for_whom;

    char buf[kMaxDataSize];
    int num_bytes;
    pthread_t sr_response_thread;

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
            string type_req, buffer_data;
            int recvd_tid;
            buffer_data = string(buf);
            p->ExtractMsg(buffer_data, type_req, recvd_tid);
            outf << "P" << p->get_pid() << ": UP recevd from P" << for_whom << ": " << buf <<  endl;
            int my_coord = p->get_my_coordinator();
            if (type_req == kUpReq)
            { 
                //upreq sent only in total failure. means everyone is in same trans. other case notpos
                if (recvd_tid == p->get_transaction_id())
                {
                    p->SendMyUp(for_whom);
                    //btw termination protocol can happen.
                }
                else{
                    cout<<"Not total failure"<<endl;
                    //send nothing back. as dec req will talk to this process
                }
            }
        }
        usleep(kGeneralSleep);
    }
}


void Process::WaitForUpResponse() {
    int n = participants_.size();
    std::vector<pthread_t> up_receive_thread(n);
    ReceiveUpThreadArgument **rcv_thread_arg = new ReceiveUpThreadArgument*[n];
    int i = 0;
    for (auto it = participants_.begin(); it != participants_.end(); ++it ) {
        rcv_thread_arg[i] = new ReceiveUpThreadArgument;
        rcv_thread_arg[i]->p = this;
        rcv_thread_arg[i]->pid = *it;
        rcv_thread_arg[i]->transaction_id = transaction_id_;
        // rcv_thread_arg[i]->received_msg_type = ;
        CreateThread(up_receive_thread[i], ReceiveUpSet, (void *)rcv_thread_arg[i]);
        i++;
    }

    void* status;

    i = 0;
    for (auto it = participants_.begin(); it != participants_.end(); ++it ) {
        pthread_join(up_receive_thread[i], &status);
        RemoveThreadFromSet(up_receive_thread[i]);
        if ((rcv_thread_arg[i]->received_msg_type==ERROR) ||  (rcv_thread_arg[i]->received_msg_type==TIMEOUT))
        {
            cout<<"Didn't receive up from "<<(*it)<<endl;
        }
        else
        {
            all_up_sets_[*it] = rcv_thread_arg[i]->up;
            // return;
        }
        i++;
    }
}

void Process::ConstructUpReq(string &msg) {
    msg = kUpReq + " " + to_string(transaction_id_);
    msg = msg + " $";}

void Process::ConstructUpResponse(string &msg) {
    msg = kUpResponse + " " + to_string(transaction_id_);
    msg = msg + " $";
}

void Process::SendMyUp(int pid_other){
    string msg;
    ConstructUpResponse(msg);
    if (send(get_up_fd(pid_other), msg.c_str(), msg.size(), 0) == -1) {
        cout << "P" << get_pid() << ": ERROR: sending my up to P" << pid_other << endl;
        RemoveFromUpSet(pid_other);
    }
    else {
        cout << "P" << get_pid() << ": Up set sent to P" << pid_other << ": " << msg << endl;
    }   
}


void* ReceiveUp(void* _rcv_thread_arg)
{
    ReceiveUpThreadArgument *rcv_thread_arg = (ReceiveUpThreadArgument *)_rcv_thread_arg;
    int pid = rcv_thread_arg->pid;
    int tid = rcv_thread_arg->transaction_id;
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
            cout << "P" << p->get_pid() << ": DecMsg received from P" << pid << ": ";
            if(buf[0]=='0')
                cout<<"Abort"<<endl;
            else if(buf[1]=='1')
                cout<<"Commit"<<endl;
            else
                cout<<buf<<endl;

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

