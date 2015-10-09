#include "process.h"
#include "constants.h"
#include "iostream"
#include "fstream"
#include "unistd.h"
#include "set"
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>
using namespace std;

pthread_mutex_t up_lock;

void Process::RemoveFromUpSet(int k) {
    bool log = false;
    pthread_mutex_lock(&up_lock);
    if (up_.find(k) != up_.end()) {
        up_.erase(k);
        log = true;
    }
    pthread_mutex_unlock(&up_lock);
    if (k == my_coordinator_) {
        my_coordinator_ = INT_MAX;
    }
    if (log)
        LogUp();

    if (k != INT_MAX) {
        reset_fd(k);
        reset_alive_fd(k);
        reset_sdr_fd(k);
    }
}

// function to initiate a connect() request to process _pid
// returns true if connection was successfull
// this connection is corresponding to the alive connection
bool Process::ConnectToProcessAlive(int process_id) {
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
    if ((rv = getaddrinfo(NULL, std::to_string(get_alive_port(get_pid())).c_str(),
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
            // cout << "P" << get_pid() << ": AClient: connect ERROR\n";
            continue;
        }

        break;
    }
    if (l == NULL) {
        cout << "P" << get_pid() << ": AClient: connect ERROR\n";
        // fprintf(stderr, "client: failed to connect\n");
        exit(1);
    }
    int outgoing_port = ntohs(return_port_no((struct sockaddr *)l->ai_addr));
    // cout << "P" << get_pid() << ": Client: connecting to " << outgoing_port << endl ;
    freeaddrinfo(servinfo); // all done with this structure
    set_alive_fd(process_id, sockfd);
    // cout << "P" << get_pid() << ": Initiating ALIVE connection to P" << process_id << endl;
    return true;
}

// thread for receiveing ALIVE messages from other processes
void* ReceiveAlive(void *_rcv_thread_arg) {
    // cout<<"receive alive entered"<<endl;
    ReceiveAliveThreadArgument *rcv_thread_arg = (ReceiveAliveThreadArgument *)_rcv_thread_arg;
    Process *p = rcv_thread_arg->p;
    int pid = rcv_thread_arg->pid_from_whom;

    ofstream outf("log/recalivelog/" + to_string(p->get_pid()) + "from" + to_string(pid));

    if (!outf.is_open())
        cout << "Failed to open log file for recalive" << endl;

    char buf[kMaxDataSize];
    int num_bytes;
    set<string> buffered_alives;

    while (true) {
        string alive_to_check = "ALIVE" + to_string(time(NULL) % 100);
        usleep(kReceiveAliveTimeout);

        // int rv = select(fd_max + 1, &temp_set, NULL, NULL, &zero);
        int rv = 0;
        if (rv == -1) {
            cout << "P" << p->get_pid() << ": ERROR in select() in Alive receive" << endl;
            // pthread_exit(NULL);
        }
        else
        {
            num_bytes = recv(p->get_alive_fd(pid), buf, kMaxDataSize - 1, 0);
            if (num_bytes == -1 || num_bytes == 0)
            {
                // cout << "P" << p->get_pid() << ": ERROR in receiving ALIVE for P" << pid << endl;
                p->RemoveFromUpSet(pid);
                return NULL;
                // pthread_exit(NULL); //TODO: think about whether it should be exit or not
            }
            else
            {
                buf[num_bytes] = '\0';
                outf << "P" << p->get_pid() << ": ALIVE received from P" << pid << ": "  << buf << " at " << time(NULL) % 100 <<  endl;

                string bufstring(buf);

                vector<string> all_msgs = split(bufstring, ' ');
                for (auto iter = all_msgs.begin(); iter != all_msgs.end(); iter++)
                {
                    buffered_alives.insert(*iter);
                }

                // cout<<p->get_pid()<<" looking for "<<alive_to_check<<" from "<<pid<<endl;

                // for(auto xit = buffered_alives.begin(); xit!=buffered_alives.end(); xit++)
                // cout<<*xit<<" ";
                // cout<<endl;

                // set<string>::iterator iter=buffered_alives.find(alive_to_check);
                // if(iter!=buffered_alives.end())
                // {

                // }
                //     // alive_processes.insert(pid);
                // else
                // {
                //     cout<<"didnt find "<<alive_to_check<<" for "<<pid<<endl;
                //     p->RemoveFromUpSet(pid);
                //     pthread_mutex_lock(&up_lock);
                //     unordered_set <int> copy_up(p->up_);
                //     pthread_mutex_unlock(&up_lock);
                //     PrintUpSet(p->get_pid(), copy_up);
                // }
            }
        }


    }
}
// }
// }
// p->UpdateUpSet(alive_processes);


// thread for sending ALIVE messages to up_ processes
//todo: lock
void* SendAlive(void *_p) {
    Process *p = (Process *)_p;
    // if(p->get_my_coordinator()==p->get_pid())
    //     usleep(kGeneralSleep);
    ofstream outf("log/sendalivelog/" + to_string(p->get_pid()));
    if (!outf.is_open())
        cout << "Failed to open log file for send alive" << endl;
    while (true) {
        string msg = kAlive;
        msg += to_string(time(NULL) % 100);
        msg += " ";
        pthread_mutex_lock(&up_lock);
        unordered_set<int> up_copy(p->up_);
        pthread_mutex_unlock(&up_lock);

        auto it = up_copy.begin();
        while (it != up_copy.end()) {
            if (send(p->get_alive_fd(*it), msg.c_str(), msg.size(), 0) == -1) {
                if (errno == ECONNRESET) {  // connection reset by peer
                    // cout << "P" << p->get_pid() << ": ALIVE connection reset by P" << (*it) << ". Removing it from UP set" << endl;
                    // remove this process from up_ set
                    //TODO: Hopefully, receive will timeout soon
                    // and will remove it from UP set
                } else {
                    cout << "P" << p->get_pid() << ": ERROR: sending ALIVE to P" << (*it) << endl;
                    it++;
                }
            }
            else {
                outf << "P" << p->get_pid() << ": ALIVE sent to P" << (*it) << " at " << time(NULL) % 100 << endl;
                it++;
            }
        }

        usleep(kSendAliveInterval);
    }
    cout << "Exiting" << endl;
}

void PrintUpSet(int whose, unordered_set<int> up_)
{
    ofstream outf("log/up" + to_string(whose), fstream::app);
    outf << whose << "_up at " << time(NULL) % 100 << " : ";
    for (auto it = up_.begin(); it != up_.end(); it++ )
        outf << (*it) << " ";
    outf << endl;
}