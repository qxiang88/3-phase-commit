#include "process.h"
#include "controller.h"
#include "constants.h"
#include "fstream"
#include "iostream"
#include "sstream"

void Controller::set_coordinator(int coordinator_id) {
    coordinator_ = coordinator_id;
}

int Controller::get_coordinator() {
    return coordinator_;
}

bool Controller::ReadConfigFile()
{
    ifstream fin;
    fin.exceptions ( ifstream::failbit | ifstream::badbit );
    try {
        fin.open(kConfigFile.c_str());
        fin >> N;
        int port;
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
    stringstream ss;

    for (int i = 0; i < N; i++) {
        ss << i;
        process_.push_back(Process(
                               i,
                               kLogFile + ss.str(),
                               kPlaylistFile + ss.str()));
        if (pthread_create(&process_thread_[i], NULL, ThreadEntry, (void *)&process_[i])) {
            cout << "ERROR:unable to create thread for P" << i << endl;
            return false;
        }
    }
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



//     pthread_t p;
//     for (int i = 0; i < N; i++) {
//         if (pthread_create(&p, NULL, some_func, (void *)P) {};
//     }

// if (rv) {
//         cout << "ERROR:unable to create thread to run P" << i << endl;
//         return 1;
//     }
}