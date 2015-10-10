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

// get port number
int return_port_no(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return (((struct sockaddr_in*)sa)->sin_port);
    }

    return (((struct sockaddr_in6*)sa)->sin6_port);
}

void sigchld_handler(int s) {
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while (waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}

// function to initiate a connect() request to process _pid
// returns true if connection was successfull
// this connection is corresponding to the send connection
bool Process::ConnectToProcess(int process_id) {
    if (get_fd(process_id) != -1) return true;
    // cout << get_pid() << "to" << process_id<<"for"<<get_fd(process_id);
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
    if ((rv = getaddrinfo(NULL, std::to_string(get_send_port(get_pid())).c_str(),
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
        errno = 0;
        if (connect(sockfd, l->ai_addr, l->ai_addrlen) == -1) {
            // cout << sockfd << endl;
            close(sockfd);
            // if (errno == EBADF) cout << errno << endl;
            // cout << "P" << get_pid() << ": Client: connect ERROR\n";
            continue;
        }

        break;
    }
    if (l == NULL) {
        cout << "P" << get_pid() << ": Client: connect ERROR\n";
        exit(1);
    }
    int outgoing_port = ntohs(return_port_no((struct sockaddr *)l->ai_addr));
    // cout << "P" << get_pid() << ": Client: connecting to " << outgoing_port << endl ;
    freeaddrinfo(servinfo); // all done with this structure
    set_fd(process_id, sockfd);
    // cout << "P" << get_pid() << ": Initiating connection to P" << process_id << endl;
    return true;
}

// function where process acts as server
// listens to incoming connect() requests
void* server(void* _p) {
    Process *p = (Process *)_p;

    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *l;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
    if ((rv = getaddrinfo(NULL, std::to_string(p->get_listen_port(p->get_pid())).c_str(),
                          &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit (1);
    }

    // loop through all the results and bind to the first we can
    for (l = servinfo; l != NULL; l = l->ai_next) {
        if ((sockfd = socket(l->ai_family, l->ai_socktype,
                             l->ai_protocol)) == -1) {
            perror("server: socket ERROR");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1) {
            perror("setsockopt ERROR");
            exit(1);
        }

        if (bind(sockfd, l->ai_addr, l->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind ERROR");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (l == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    p->set_server_sockfd(sockfd);

    if (listen(sockfd, kBacklog) == -1) {
        perror("listen ERROR");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    // cout << "P" << p->get_pid() << ": Server: waiting for connections...\n";
    while (1) {
        // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept ERROR");
            continue;
        }

        int incoming_port = ntohs(return_port_no((struct sockaddr *)&their_addr));
        int process_id = p->get_send_port_pid_map(incoming_port);
        if (process_id != -1) {   // incoming connection is from send_port
            p->set_fd(process_id, new_fd);
            // cout << "P" << p->get_pid() << ": Server: accepting connection from P"
            // << p->get_send_port_pid_map(incoming_port) << endl;
        } else {
            process_id = p->get_alive_port_pid_map(incoming_port);
            if (process_id != -1) { // incoming connection is from alive port

                if (setsockopt(new_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&kReceiveAliveTimeoutTimeval,
                               sizeof(struct timeval)) == -1) {
                    perror("setsockopt ERROR");
                    exit(1);
                }

                p->set_alive_fd(process_id, new_fd);
                // cout << "P" << p->get_pid() << ": Server: accepting ALIVE connection from P"
                // << p->get_alive_port_pid_map(incoming_port) << endl;
            } else {
                process_id = p->get_sdr_port_pid_map(incoming_port);
                p->set_sdr_fd(process_id, new_fd);
                // cout << "P" << p->get_pid() << ": Server: accepting SDR connection from P"
                // << p->get_sdr_port_pid_map(incoming_port) << endl;
            }
        }
    }
    pthread_exit(NULL);
}