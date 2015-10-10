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

extern pthread_mutex_t new_coord_lock;

bool Process::ConnectToProcessSDR(int process_id) {
    if (get_sdr_fd(process_id) != -1) return true;
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
    if ((rv = getaddrinfo(NULL, std::to_string(get_sdr_port(get_pid())).c_str(),
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
    set_sdr_fd(process_id, sockfd);
    // cout << "P" << get_pid() << ": Initiating SDR connection to P" << process_id << endl;
    return true;
}

// thread for receiving SR/DR messages from one process
void* ReceiveStateOrDecReq(void* _arg) {
    ReceiveSDRThreadArgument* arg = (ReceiveSDRThreadArgument*)_arg;
    Process* p = arg->p;
    int pid = arg->pid_to_whom;
    char buf[kMaxDataSize];
    int num_bytes;
    pthread_t sr_response_thread;

    ofstream outf("log/sdr/" + to_string(p->get_pid()) + "from" + to_string(pid));

    if (!outf.is_open())
        cout << "Failed to open log file for sdr" << endl;

    while (true) {
        // currently, this loop sleeps at the end for kGeneralSleep
        // TODO: confirm whether this is the right amount of sleep
        if ((num_bytes = recv(p->get_sdr_fd(pid), buf, kMaxDataSize - 1, 0)) == -1)
        {
            cout << "P" << p->get_pid() << ": ERROR in receiving SDR for P" << pid << endl;
            p->RemoveFromUpSet(pid);
            // no need to exit even if there is an error. Hopefully in future, pid will recover
            // and SDRconnect to this process which will set the sdr_fd correctly
            // after which recv will not throw an error.
        }
        else if (num_bytes == 0)
        {   //connection closed
            cout << "P" << p->get_pid() << ": SDR connection closed by P" << pid << endl;
            p->RemoveFromUpSet(pid);
            // no need to exit even if there is a timeout. Hopefully in future, pid will recover
            // and SDRconnect to this process which will set the sdr_fd correctly
            // after which recv will not throw an error.
        }
        else
        {
            buf[num_bytes] = '\0';
            string type_req, buffer_data;
            int recvd_tid;
            buffer_data = string(buf);
            p->ExtractMsg(buffer_data, type_req, recvd_tid);
            outf << "P" << p->get_pid() << ": SDR recevd from P" << pid << ": " << buf <<  endl;

            int my_coord = p->get_my_coordinator();
            if (type_req == kStateReq)
            {   //assumes state req has to be current tid

                if (my_coord == p->get_pid())
                {
                    //i am coordinator and have received state req
                    if (pid < (p->get_pid()))
                    {
                        pthread_cancel(p->newcoord_thread);
                        p->RemoveThreadFromSet(p->newcoord_thread);    //TODO: SC added this

                        p->set_my_coordinator(pid);

                        pthread_cancel(sr_response_thread);
                        p->RemoveThreadFromSet(sr_response_thread);    //TODO: SC added this

                        p->CreateThread(sr_response_thread, responder, (void *)p);
                        //whyat shud be my mode now
                    }
                }
                else //i am participant
                {

                    cout<<pid<<" "<<my_coord<<endl;
                    if (pid <= (my_coord))
                    {   //only send to valid coord
                        p->set_state_req_in_progress(true);
                        if (pid < (my_coord))
                            p->set_my_coordinator(pid);

                        pthread_cancel(sr_response_thread);
                        p->RemoveThreadFromSet(sr_response_thread);    //TODO: SC added this
                        //create responder thread
                        p->CreateThread(sr_response_thread, responder, (void *)p);

                    }
                }
                //case1 can coord get State req.ya because later no longer coord
            }
            else if (type_req == kURElected)
            {
                cout<<"I am elected. my coord was "<<my_coord<<", my id is "<<p->get_pid()<<endl;
                if (my_coord == p->get_pid())
                    continue;
                if(my_coord<pid)
                    continue;



                bool templ = false;
                pthread_mutex_lock(&new_coord_lock);
                if (!p->new_coord_thread_made)
                {
                    p->new_coord_thread_made = true;
                    templ = true;
                }

                pthread_mutex_unlock(&new_coord_lock);
                if (templ) {
                    p->CreateThread(p->newcoord_thread, NewCoordinatorMode, (void *)p);
                }

            }
            else { //decreq
                outf << "Dec req received " << p->get_my_state() << endl;
                if (recvd_tid == p->get_transaction_id())
                {
                    if (p->get_my_state() == COMMITTED || p->get_my_state() == ABORTED)
                        p->SendDecision(pid);
                }
                else if (recvd_tid < p->get_transaction_id())
                {
                    // if(my_state_==COMMITTED || my_state_==ABORTED)
                    p->SendPrevDecision(pid, recvd_tid);
                }
            }
        }
        usleep(kGeneralSleep);

    }
}


void* responder(void *_p) {

    Process *p = (Process *)_p;
    p->SendState(p->get_my_coordinator());
    // cout << "sent state to new coord at " << time(NULL) % 100 << endl;
    ProcessState my_st = p->get_my_state();
    if (!(my_st == UNCERTAIN || my_st == COMMITTABLE))
        return NULL;

    if (my_st == UNCERTAIN)
    {
        p->ReceivePreCommitOrAbortFromCoordinator();
        if (p->get_my_state() == UNCERTAIN)
            p->Timeout();
        else if (p->get_my_state() == ABORTED)
            p->LogAbort();
        else {
            p->LogPreCommit();
            p->SendMsgToCoordinator(kAck);
            // cout << p->get_pid() << " sent ack to coord at " << time(NULL) % 100 << endl;
        }
    }
    else {
        p->ReceiveAnythingFromCoordinator();
        if (p->get_my_state() == COMMITTABLE) {
            p->LogPreCommit();
            p->SendMsgToCoordinator(kAck);
            // cout << p->get_pid() << " sent ack to coord at " << time(NULL) % 100 << endl;

            p->ReceiveCommitFromCoordinator();
            if (p->get_my_state() == COMMITTABLE)
                p->Timeout();
            else {
                p->LogCommit();
            }
        } else if (p->get_my_state() == COMMITTED) {
            p->LogCommit();
        } else {
            p->LogAbort();  // should be impossible
        }

    }

    return NULL;
}

// // thread for receiveing SR/DR messages from other processes
// void* ReceiveStateOrDecReq(void *_p) {
//     Process *p = (Process *)_p;

//     char buf[kMaxDataSize];
//     int num_bytes;
//     pthread_t sr_response_thread;


//     fd_set temp_set, partici_set;
//     int fd_max;
//     FD_ZERO(&partici_set);
//     for (auto it = p->participants_.begin(); it != p->participants_.end(); ++it) {
//         FD_SET(p->get_sdr_fd(*it), &partici_set);
//         fd_max = max(fd_max, *it);
//     }


//     while (true)
//     {
//         // no need to lock mutex here since updates to UP are perfomed
//         // by this thread itself at the end.
//         temp_set = partici_set;
//         sleep(1);
//         // int rv = select(fd_max + 1, &temp_set, NULL, NULL, NULL);
//         int rv = 0;
//         // cout<<"Select returns"<<endl;
//         if (rv == -1)
//         {
//             cout << "P" << p->get_pid() << ": ERROR in select() in SD receive" << endl;
//             pthread_exit(NULL);
//         }
//         // else if (rv == 0)
//         // { // timeout. not here as NULL time given, right?
//         //     break;
//         // }
//         else
//         {
//             cout << "something on SDR" << endl;
//             for (auto it = p->participants_.begin(); it != p->participants_.end(); ++it)
//             {
//                 if (*it == p->get_pid()) continue;
//                 // if (FD_ISSET(p->get_sdr_fd(*it), &temp_set))
//                 // { // we got one!!
//                 if ((num_bytes = recv(p->get_sdr_fd(*it), buf, kMaxDataSize - 1, 0)) == -1)
//                 {
//                     cout << "P" << p->get_pid() << ": ERROR in receiving SDR for P" << *it << endl;
//                     p->RemoveFromUpSet(*it);
//                     // pthread_exit(NULL); //TODO: think about whether it should be exit or not
//                 }
//                 else if (num_bytes == 0)
//                 {   //connection closed
//                     cout << "P" << p->get_pid() << ": SDR connection closed by P" << *it << endl;
//                     p->RemoveFromUpSet(*it);
//                 }
//                 else
//                 {
//                     buf[num_bytes] = '\0';
//                     string type_req, buffer_data;
//                     int recvd_tid;
//                     buffer_data = string(buf);
//                     p->ExtractMsg(buffer_data, type_req, recvd_tid);
//                     cout << "P" << p->get_pid() << ": SDR recevd from P" << *it << ": " << buf <<  endl;
//                     if (type_req == kStateReq)
//                     {   //assumes state req has to be current tid

//                         if (p->get_my_coordinator() == p->get_pid())
//                         {
//                             //i am coordinator and have received state req
//                             if ((*it) < (p->get_pid()))
//                             {
//                                 pthread_cancel(p->newcoord_thread);
//                                 p->set_my_coordinator(*it);

//                                 pthread_cancel(sr_response_thread);
//                                 p->CreateThread(sr_response_thread, responder, (void *)p);
//                                 //whyat shud be my mode now
//                             }

//                         }
//                         else //i am participant
//                         {
//                             if ((*it) <= (p->get_my_coordinator()))
//                             {   //only send to valid coord
//                                 if ((*it) < (p->get_my_coordinator()))
//                                     p->set_my_coordinator(*it);

//                                 pthread_cancel(sr_response_thread);
//                                 //create responder thread
//                                 p->CreateThread(sr_response_thread, responder, (void *)p);

//                             }
//                         }
//                         //case1 can coord get State req.ya because later no longer coord
//                     }
//                     else if (type_req == kURElected)
//                     {
//                         if (p->get_my_coordinator() == p->get_pid())
//                             continue;
//                         p->CreateThread(p->newcoord_thread, NewCoordinatorMode, (void *)p);
//                     }
//                     else { //decreq
//                         if (recvd_tid == p->get_transaction_id())
//                         {
//                             if (p->get_my_state() == COMMITTED || p->get_my_state() == ABORTED)
//                                 p->SendDecision((*it));
//                         }
//                         else if (recvd_tid < p->get_transaction_id())
//                         {
//                             // if(my_state_==COMMITTED || my_state_==ABORTED)
//                             p->SendPrevDecision((*it), recvd_tid);
//                         }
//                     }
//                 }
//                 // }
//             }
//         }
//         // p->UpdateUpSet(alive_processes);
//     }
//     return NULL;
// }
