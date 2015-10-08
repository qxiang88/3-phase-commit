#include "process.h"
#include "constants.h"
#include "iostream"
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

pthread_mutex_t up_lock;

void Process::UpdateUpSet(std::unordered_set<int> &alive_processes) {
    bool change = false;
    pthread_mutex_lock(&up_lock);

    auto it = up_.begin();
    while (it != up_.end()) {
        if (alive_processes.find(*it) == alive_processes.end()) { // process is no longer alive
            change = true;
            cout << "P" << get_pid() << ": Removing P" << *it << " from UP set" << endl;
            it = up_.erase(it);
        } else {
            it++;
        }
    }
    pthread_mutex_unlock(&up_lock);

    if (change) {
        LogUp();
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
            cout << "P" << get_pid() << ": Client: connect ERROR\n";
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
    set_alive_fd(process_id, sockfd);
    cout << "P" << get_pid() << ": Initiating ALIVE connection to P" << process_id << endl;
    return true;
}

// thread for receiveing ALIVE messages from other processes
void* ReceiveAlive(void *_p) {
    Process *p = (Process *)_p;

    char buf[kMaxDataSize];
    int num_bytes;

    fd_set temp_set;
    int fd_max;

    timeval zero = (struct timeval) {0};
    while (true) {
        std::unordered_set<int> alive_processes;

        // no need to lock mutex here since updates to UP are perfomed
        // by this thread itself at the end.
        FD_ZERO(&temp_set);
        for (auto it = p->up_.begin(); it != p->up_.end(); ++it) {
            FD_SET(p->get_alive_fd(*it), &temp_set);
            fd_max = max(fd_max, *it);
        }

        usleep(kReceiveAliveTimeout);

        int rv = select(fd_max + 1, &temp_set, NULL, NULL, &zero);

        if (rv == -1) {
            cout << "P" << p->get_pid() << ": ERROR in select() in Alive receive" << endl;
            pthread_exit(NULL);
        } else if (rv == 0) { // timeout
            break;
        } else {
            for (auto it = p->up_.begin(); it != p->up_.end(); ++it) {
                if (FD_ISSET(p->get_alive_fd(*it), &temp_set)) { // we got one!!
                    if ((num_bytes = recv(p->get_alive_fd(*it), buf, kMaxDataSize - 1, 0)) == -1) {
                        cout << "P" << p->get_pid() << ": ERROR in receiving ALIVE for P" << *it << endl;
                        pthread_exit(NULL); //TODO: think about whether it should be exit or not
                    } else if (num_bytes == 0) {     //connection closed
                        cout << "P" << p->get_pid() << ": ALIVE connection closed by P" << *it << endl;
                    } else {
                        buf[num_bytes] = '\0';
                        // cout << "P" << p->get_pid() << ": Msg received from P" << *it << ": " << buf <<  endl;
                        if (string(buf) == kAlive) {
                            alive_processes.insert(*it);
                        }
                    }
                }
            }
        }
        p->UpdateUpSet(alive_processes);
    }
}

// thread for sending ALIVE messages to up_ processes
//todo: lock
void* SendAlive(void *_p) {
    Process *p = (Process *)_p;
    string msg = kAlive;
    while (true) {

        pthread_mutex_lock(&up_lock);
        unordered_set<int> up_copy(p->up_);
        pthread_mutex_unlock(&up_lock);

        auto it = up_copy.begin();
        while (it != up_copy.end()) {
            if (send(p->get_alive_fd(*it), msg.c_str(), msg.size(), 0) == -1) {
                if (errno == ECONNRESET) {  // connection reset by peer
                    cout << "P" << p->get_pid() << ": ALIVE connection reset by P" << (*it) << ". Removing it from UP set" << endl;
                    // remove this process from up_ set
                    //TODO: Hopefully, receive will timeout soon
                    // and will remove it from UP set
                } else {
                    cout << "P" << p->get_pid() << ": ERROR: sending ALIVE to P" << (*it) << endl;
                    it++;
                }
            }
            else {
                // cout << "P" << p->get_pid() << ": ALIVE sent to P" << (*it) << " at "<< time(NULL)%100 << endl;
                it++;
            }
        }

        usleep(kSendAliveInterval);
    }
    cout << "Exiting" << endl;
}