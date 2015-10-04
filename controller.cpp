#include "process.h"
#include "controller.h"
#include "constants.h"
#include "fstream"
#include "iostream"

int Controller::N;
std::vector<int> Controller::listen_port_;
std::vector<int> Controller::send_port_;
std::map<int, int> Controller::send_port_pid_map_;

void Controller::set_coordinator(int coordinator_id) {
    coordinator_ = coordinator_id;
}

int Controller::get_coordinator() {
    return coordinator_;
}

int Controller::get_listen_port(int process_id) {
    return listen_port_[process_id];
}

int Controller::get_send_port(int process_id) {
    return send_port_[process_id];
}

int Controller::get_send_port_pid_map(int port_num) {
    return send_port_pid_map_[port_num];
}

// reads the config file
// sets value of N
// adds port values to listen_port_ and send_port_
// constructs send_port_pid_map_
bool Controller::ReadConfigFile()
{
    ifstream fin;
    fin.exceptions ( ifstream::failbit | ifstream::badbit );
    try {
        fin.open(kConfigFile.c_str());
        fin >> N;
        int port;
        cout << "N=" << N << endl;
        for (int i = 0; i < N; ++i) {
            fin >> port;
            listen_port_.push_back(port);
        }
        for (int i = 0; i < N; ++i) {
            fin >> port;
            send_port_.push_back(port);
            send_port_pid_map_.insert(make_pair(port, i));
        }
        fin.close();

        return true;

    } catch (ifstream::failure e) {
        cout << e.what() << endl;
        if (fin.is_open()) fin.close();
        return false;
    }
}

bool Controller::CreateProcesses() {
    process_thread_.resize(N);
    // creating N Process objects
    process_.resize(N);
    for (int i = 0; i < N; i++) {
        process_[i].Initialize(i, kLogFile + to_string(i), kPlaylistFile + to_string(i));
        // process_[i].set_pid(i);
        // process_[i].set_log_file(kLogFile + to_string(i));
        // process_[i].set_playlist_file(kPlaylistFile + to_string(i));
        // process_.push_back(Process(
        //                        i,
        //                        kLogFile + to_string(i),
        //                        kPlaylistFile + to_string(i)));
        if (pthread_create(&process_thread_[i], NULL, ThreadEntry, (void *)&process_[i])) {
            cout << "C: ERROR: Unable to create thread for P" << i << endl;
            return false;
        }
    }
    return true;
}

void Controller::WaitForThreadJoins() {
    void *status;
    for (int i = 0; i < N; i++) {
        pthread_join(process_thread_[i], &status);
    }
}

int main() {
    Controller c;
    if (!c.ReadConfigFile()) return 1;
    if (!c.CreateProcesses()) return 1;

    c.WaitForThreadJoins();


    return 0;
}