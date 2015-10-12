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
#include "execinfo.h"
#include "cxxabi.h"
using namespace std;

pthread_mutex_t up_lock;


string getname(string s) {
    int i=0;
    string ret;
    while(s[i]!='(')
    {
        i++;
    }
    i++;
    while(s[i]!='+') {
        ret.push_back(s[i]);
        i++;
    }
    return ret;

}

void Process::my_backtrace() {
    int size = 20;
    void *buffer[size];
    ofstream backout("log/backout/"+to_string(get_pid()), fstream::app);
    if(!backout.is_open())cout<<"error opening file"<<endl;

    int ret = backtrace(buffer, size);
    char **ptr = backtrace_symbols( buffer, ret );

    int status = -1;
    for (int i = 0; i < ret; ++i)
    {
        char *demangledName = abi::__cxa_demangle( getname(string(ptr[i])).c_str(), NULL, NULL, &status );
        if ( status == 0 )
        {
            backout << demangledName << std::endl;
        }
        free( demangledName );
    }
    
    backout.close();

    free(ptr);
}

void Process::RemoveFromUpSet(int k) {

    if (get_my_status() == RECOVERY || get_my_status() == DYING)
        return;
    ofstream backout("log/backout/"+to_string(get_pid()), fstream::app);
    if(!backout.is_open())cout<<"error opening file"<<endl;
    backout<<"I am P"<<get_pid()<<". Removing"<< k<<endl;
    my_backtrace();

    bool log = false;
    pthread_mutex_lock(&up_lock);
    if (up_.find(k) != up_.end()) {
        // cout<<"~~~~"<<endl;
        up_.erase(k);
        log = true;
    }
    pthread_mutex_unlock(&up_lock);
    if (k == get_my_coordinator()) {
        set_my_coordinator(INT_MAX);
    }
    if (log)
        LogUp();

    if (k != INT_MAX) {
        // cout << "~~~~" << endl;
        reset_fd(k);
        reset_alive_fd(k);
        reset_sdr_fd(k);
        reset_up_fd(k);

    }
    backout.close();
}

// function to initiate a connect() request to process _pid
// returns true if connection was successfull
// this connection is corresponding to the alive connection
bool Process::ConnectToProcessAlive(int process_id) {
    if (get_alive_fd(process_id) != -1) return true;
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
        // cout << "P" << get_pid() << ": AClient: connect ERROR\n";
        // fprintf(stderr, "client: failed to connect\n");
        return false;
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

    outf << "P" << p->get_pid() << "Alive thread up for P" << pid << " at " << time(NULL) % 100 << endl;

    int start_time = time(NULL) % 100;
    while (true) {
        pthread_testcancel();
        if(p->get_my_status() == DYING)
            break;

        string alive_to_check = "ALIVE" + to_string(start_time);
        timeval beforetime, aftertime;
        // errno = 0;
        gettimeofday(&beforetime, NULL);
        // outf << "before receive t=" << beforetime.tv_sec << "," << beforetime.tv_usec << endl;
        num_bytes = recv(p->get_alive_fd(pid), buf, kMaxDataSize - 1, 0);
        gettimeofday(&aftertime, NULL);
        if (num_bytes == -1 )
        {
            // cout << "P" << p->get_pid() << ": ERROR in receiving ALIVE -1 for P" << pid << " " << endl;
            // outf << "P" << p->get_pid() << ": ERROR in receiving ALIVE -1 for P" << pid << " " << p->get_alive_fd(pid) << "errno=" << (errno) << " t=" << aftertime.tv_sec << "," << aftertime.tv_usec << endl;
            // if (errno == EAGAIN) outf << "found" << endl;
            p->RemoveFromUpSet(pid);
            p->RemoveThreadFromSetAlive(pthread_self());
            return NULL;
        }
        else if ( num_bytes == 0)
        {
            // cout << "P" << p->get_pid() << ": ERROR in receiving ALIVE 0 for P" << pid << " " << endl;
            // outf << "P" << p->get_pid() << ": ERROR in receiving ALIVE 0 for P" << pid << " " << p->get_alive_fd(pid) << " t=" << aftertime.tv_sec << "," << aftertime.tv_usec << endl;
            p->RemoveFromUpSet(pid);
            p->RemoveThreadFromSetAlive(pthread_self());
            return NULL;
            // pthread_exit(NULL); //TODO: think about whether it should be exit or not
        }
        else
        {
            pthread_testcancel();
            buf[num_bytes] = '\0';
            outf << "P" << p->get_pid() << ": ALIVE received from P" << pid << ": "  << buf << " at " << time(NULL) % 100 << endl;
            // outf << "P" << p->get_pid() << ": ALIVE received from P" << pid << ": "  << buf << " at " << time(NULL) % 100 <<  " t=" << aftertime.tv_sec << "," << aftertime.tv_usec << endl;

            string bufstring(buf);

            vector<string> all_msgs = split(bufstring, '$');
            // outf << "P:" << p->get_pid() << " All msgs size=" << all_msgs.size() << endl;
            for (auto iter = all_msgs.begin(); iter != all_msgs.end(); iter++)
            {
                buffered_alives.insert(*iter);
                // cout<<*iter<<endl;
            }
        }

        // outf << p->get_pid() << " looking for " << alive_to_check << " from " << pid << endl;

        // for (auto xit = buffered_alives.begin(); xit != buffered_alives.end(); xit++)
        //     outf << *xit << " ";
        // outf << endl;

        set<string>::iterator iter = buffered_alives.find(alive_to_check);
        if (iter == buffered_alives.end())
        {
            outf << "didnt find " << alive_to_check << " for " << pid << endl;
            p->RemoveFromUpSet(pid);
            pthread_mutex_lock(&up_lock);
            set <int> copy_up(p->up_);
            pthread_mutex_unlock(&up_lock);
            PrintUpSet(p->get_pid(), copy_up);
            p->RemoveThreadFromSetAlive(pthread_self());
            return NULL;
        }
        else
        {
            buffered_alives.erase(iter);
        }

        start_time = (start_time + kReceiveAliveTimeoutTimeval.tv_sec) % 100;
        sleep((beforetime.tv_sec + kReceiveAliveTimeoutTimeval.tv_sec) - aftertime.tv_sec);
        pthread_testcancel();

    }
    return NULL;
}

// thread for sending ALIVE messages to up_ processes
//todo: lock
void* SendAlive(void *_p) {
    Process *p = (Process *)_p;
    // if(p->get_my_coordinator()==p->get_pid())
    //     usleep(kGeneralSleep);
    ofstream outf("log/sendalivelog/" + to_string(p->get_pid()));
    if (!outf.is_open())
        cout << "Failed to open log file for send alive" << endl;

    int* oltemp;

    while (true) {
        pthread_testcancel();
        if(p->get_my_status() == DYING)
            break;

        string msg = kAlive;
        msg += to_string(time(NULL) % 100);
        msg += "$";
        pthread_mutex_lock(&up_lock);
        set<int> up_copy(p->up_);
        pthread_mutex_unlock(&up_lock);

        auto it = up_copy.begin();
        while (it != up_copy.end()) {
            if(*it==p->get_pid()) continue;
            if (send(p->get_alive_fd(*it), msg.c_str(), msg.size(), 0) == -1) {
                // if (errno == ECONNRESET) {  // connection reset by peer
                //     // cout << "P" << p->get_pid() << ": ALIVE connection reset by P" << (*it) << ". Removing it from UP set" << endl;
                //     // remove this process from up_ set
                //     //TODO: Hopefully, receive will timeout soon
                //     // and will remove it from UP set
                // } else {
                // cout << "P" << p->get_pid() << ": ERROR: sending ALIVE to P" << (*it) << endl;
                p->RemoveFromUpSet(*it);
                it++;
                // }
            }
            else {
                timeval timeofday;
                gettimeofday(&timeofday, NULL);
                outf << "P" << p->get_pid() << ": ALIVE sent to P" << (*it) << " at " << time(NULL) % 100 << endl;
                // outf << "P" << p->get_pid() << ": ALIVE sent to P" << (*it) << " at " << time(NULL) % 100 << " t=" << timeofday.tv_sec << "," << timeofday.tv_usec << endl;
                it++;
            }
        }
        pthread_testcancel();
        usleep(kSendAliveInterval);
    }
    cout << "AliveExiting" << endl;

    return NULL;
}

void PrintUpSet(int whose, set<int> up_)
{
    ofstream outf("log/up" + to_string(whose), fstream::app);
    outf << whose << "_up at " << time(NULL) % 100 << " : ";
    for (auto it = up_.begin(); it != up_.end(); it++ )
        outf << (*it) << " ";
    outf << endl;
}