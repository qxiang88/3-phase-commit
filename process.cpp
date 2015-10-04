#include "process.h"
#include "constants.h"
#include "fstream"
#include "iostream"
#include "unistd.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include "sys/time.h"
using namespace std;

pthread_mutex_t fd_lock;
pthread_mutex_t fd_set_lock;

void Process::Initialize(int pid, string log_file, string playlist_file) {
    pid_ = pid;
    log_file_ = log_file_;
    playlist_file_ = playlist_file;
    fd.resize(N, -1);
    // clear the master and temp sets
    FD_ZERO(&master_fds_);
    FD_ZERO(&temp_fds_);
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

void Process::set_fd(int process_id, int new_fd) {
    pthread_mutex_lock(&fd_lock);
    // if (fd[process_id] == -1)
    // {
    fd[process_id] = new_fd;
    // }
    pthread_mutex_unlock(&fd_lock);
}

int Process::get_fd(int process_id) {
    int ret;
    pthread_mutex_lock(&fd_lock);
    ret = fd[process_id];
    pthread_mutex_unlock(&fd_lock);
    return ret;
}

// print function for debugging purposes
void Process::Print() {
    cout << "pid=" << get_pid() << " ";
    for (int i = 0; i < N; ++i) {
        cout << fd[i] << ",";
    }
    cout << endl;
}

// adds the new fd to the master_fd set
// updates fd_max
void Process::AddToFdSet(int add_fd) {
    pthread_mutex_lock(&fd_set_lock);
    FD_SET(add_fd, &master_fds_);
    fd_max_ = max(fd_max_, add_fd);
    pthread_mutex_unlock(&fd_set_lock);
}

void Process::RemoveFromFdSet(int remove_fd) {
    pthread_mutex_lock(&fd_set_lock);
    FD_CLR(remove_fd, &master_fds_);
    // update the fd_max if the removed fd was the max
    // TODO: not sure if this is required or correct
    if (fd_max_ == remove_fd) {
        for (int i = fd_max_ - 1; i >= 0 ; --i) {
            if (FD_ISSET(i, &master_fds_)) {
                fd_max_ = i;
                break;
            }
        }
    }
    pthread_mutex_unlock(&fd_set_lock);
}

void Process::UpdateTempFdSet() {
    pthread_mutex_lock(&fd_set_lock);
    temp_fds_ = master_fds_ ;
    pthread_mutex_unlock(&fd_set_lock);
}

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

// thread for receiving messages from processes
void* receive(void* _p) {
    Process *p = (Process *)_p;
    int pid = p->get_pid();

    // if (select(p->fd_max_ + 1, &p->temp_fds_, NULL, NULL, kSelectTimeout) == -1) {
    //     cout << "P" << p->get_pid() << ": ERROR in select" << endl;
    //     exit(4);
    // }


    pthread_exit(NULL);
}

void Process::InitializeLocks() {
    if (pthread_mutex_init(&fd_lock, NULL) != 0) {
        cout << "P" << get_pid() << ": Mutex init failed" << endl;
        pthread_exit(NULL);
    }

    if (pthread_mutex_init(&fd_set_lock, NULL) != 0) {
        cout << "P" << get_pid() << ": Mutex init failed" << endl;
        pthread_exit(NULL);
    }
}

void* ThreadEntry(void* _p) {
    Process *p = (Process *)_p;
    p->InitializeLocks();

    if (!(p->LoadPlaylist())) {
        cout << "P" << p->get_pid() << ": Exiting" << endl;
        pthread_exit(NULL);
    }

    pthread_t server_thread;
    if (pthread_create(&server_thread, NULL, server, (void *)p)) {
        cout << "P" << p->get_pid() << ": ERROR: Unable to create server thread for P" << p->get_pid() << endl;
        pthread_exit(NULL);
    }
    // sleep for 1 seconds to make sure server is up and listening
    usleep(1000 * 1000);
    p->ConnectToProcess((p->get_pid() + 1) % 2);
    // sleep for 1 seconds to make sure all connections are set up
    usleep(1000 * 1000);
    p->Print();

    //create a receive thread
    pthread_t receive_thread;
    if (pthread_create(&receive_thread, NULL, receive, (void *)p)) {
        cout << "P" << p->get_pid() << ": ERROR: Unable to create receive thread for P" << p->get_pid() << endl;
        pthread_exit(NULL);
    }

    // string msg = "MSG" + to_string(p->get_pid()) + "to" + to_string((p->get_pid() + 1) % 2);
    // if (send(p->get_fd((p->get_pid() + 1) % 2), msg.c_str(), msg.size(), 0) == -1)
    // {
    //     cout << "P" << p->get_pid() << ": ERROR: sending to process P" << (p->get_pid() + 1) % 2 << endl;
    //     exit(1);
    // }

    void* status;
    pthread_join(receive_thread, &status);
    pthread_join(server_thread, &status);
    pthread_exit(NULL);
}
