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
    // cout << get_pid() << " " << process_id << endl;
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
    set_sdr_fd(process_id, sockfd);
    // cout << "P" << get_pid() << ": Initiating SDR connection to P" << process_id << endl;
    return true;
}

// thread for receiving SR/DR messages from one process
void* ReceiveStateOrDecReq(void* _arg) {
    ReceiveSDRUpThreadArgument* arg = (ReceiveSDRUpThreadArgument*)_arg;
    Process* p = arg->p;
    int pid = arg->for_whom;
    char buf[kMaxDataSize];
    int num_bytes;
    pthread_t sr_response_thread;

    ofstream outf("log/sdr/" + to_string(p->get_pid()) + "from" + to_string(pid));

    if (!outf.is_open())
        cout << "Failed to open log file for sdr" << endl;

    while (true) {
        // currently, this loop sleeps at the end for kGeneralSleep
        // TODO: confirm whether this is the right amount of sleep
        // cout<<"$$$$P"<<p->get_pid()<<"sdr_fd"<<pid<<p->get_sdr_fd(pid)<<endl;
        while (p->get_sdr_fd(pid) == -1) {
            usleep(kMiniSleep);
        }

        if ((num_bytes = recv(p->get_sdr_fd(pid), buf, kMaxDataSize - 1, 0)) == -1)
        {
            timeval aftertime;
            gettimeofday(&aftertime, NULL);
            cout << "P" << p->get_pid() << ": ERROR in receiving SDR for P" << pid << " t=" << aftertime.tv_sec << "," << aftertime.tv_usec << endl;
            p->RemoveFromUpSet(pid);
            // no need to exit even if there is an error. Hopefully in future, pid will recover
            // and SDRconnect to this process which will set the sdr_fd correctly
            // after which recv will not throw an error.
        }
        else if (num_bytes == 0)
        {   //connection closed
            // cout << "P" << p->get_pid() << ": SDR connection closed by P" << pid << endl;
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

            timeval temptime;
            gettimeofday(&temptime, NULL);

            outf << "P" << p->get_pid() << ": SDR recevd from P" << pid << ": " << buf << "at " << temptime.tv_sec << ", " << temptime.tv_usec << endl;

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
                outf << "I am elected. my coord was " << my_coord << ", my id is " << p->get_pid() << "at " << temptime.tv_sec << ", " << temptime.tv_usec << endl;
                if (my_coord == p->get_pid())
                    continue;
                if (my_coord < pid)
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
                    cout<<p->get_pid()<<"creating newcoord_thread"<<endl;

                    p->CreateThread(p->newcoord_thread, NewCoordinatorMode, (void *)p);
                }

            }
            else { //decreq
                timeval t;
                gettimeofday(&t, NULL);
                outf << "Dec req received; alive= " << p->get_alive_fd(pid) << " sdr=" << p->get_sdr_fd(pid) << "fd=" << p->get_fd(pid) << "at" << t.tv_sec << "," << t.tv_usec << endl;
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
    if (!(my_st == UNCERTAIN || my_st == COMMITTABLE)) {
        p->RemoveThreadFromSet(pthread_self());
        return NULL;
    }

    if (my_st == UNCERTAIN)
    {
        p->ReceivePreCommitOrAbortFromCoordinator();
        if (p->get_my_state() == UNCERTAIN) {
            p->set_state_req_in_progress(false);
            p->Timeout();
        }
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
            if (p->get_my_state() == COMMITTABLE) {
                p->set_state_req_in_progress(false);
                p->Timeout();
            }
            else {
                p->LogCommit();
            }
        } else if (p->get_my_state() == COMMITTED) {
            p->LogCommit();
        } else {
            p->LogAbort();  // should be impossible
        }

    }
    p->RemoveThreadFromSet(pthread_self());
    return NULL;
}
