#include "process.h"
#include "controller.h"
#include "constants.h"
#include "fstream"
#include "iostream"
#include "unistd.h"

int Controller::N;
int Controller::coordinator_;
std::vector<int> Controller::listen_port_;
std::vector<int> Controller::send_port_;
std::vector<int> Controller::alive_port_;
std::vector<int> Controller::sdr_port_;
std::map<int, int> Controller::send_port_pid_map_;
std::map<int, int> Controller::alive_port_pid_map_;
std::map<int, int> Controller::sdr_port_pid_map_;
std::vector<string> Controller::transaction_;

pthread_mutex_t coordinator_lock;

void Controller::set_coordinator(int coordinator_id) {
    pthread_mutex_lock(&coordinator_lock);
    coordinator_ = coordinator_id;
    pthread_mutex_unlock(&coordinator_lock);
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

int Controller::get_alive_port(int process_id) {
    return alive_port_[process_id];
}

int Controller::get_sdr_port(int process_id) {
    return sdr_port_[process_id];
}
// returns -1 if there is no entry for port_num in send_port_pid_map_
// otherwise returns the pid
int Controller::get_send_port_pid_map(int port_num) {
    if (send_port_pid_map_.find(port_num) == send_port_pid_map_.end())
        return -1;
    else
        return send_port_pid_map_[port_num];
}

// returns -1 if there is no entry for port_num in alive_port_pid_map_
// otherwise returns the pid
int Controller::get_alive_port_pid_map(int port_num) {
    if (alive_port_pid_map_.find(port_num) == alive_port_pid_map_.end())
        return -1;
    else
        return alive_port_pid_map_[port_num];
}

int Controller::get_sdr_port_pid_map(int port_num) {
    if (sdr_port_pid_map_.find(port_num) == sdr_port_pid_map_.end())
        return -1;
    else
        return sdr_port_pid_map_[port_num];
}

// reads the config file
// sets value of N
// adds port values to listen_port_ , send_port_, alive_port_
// constructs send_port_pid_map_
// constructs alive_port_pid_map_
bool Controller::ReadConfigFile() {
    int count;
    ifstream temp;
    temp.open("configs/count");
    temp>>count;
    temp.close();

    ofstream tempw;
    tempw.open("configs/count");
    tempw<<(count+1)%5;
    tempw.close();





    ifstream fin;
    fin.exceptions ( ifstream::failbit | ifstream::badbit );
    try {
        fin.open((kConfigFile+to_string(count)).c_str());
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
        for (int i = 0; i < N; ++i) {
            fin >> port;
            alive_port_.push_back(port);
            alive_port_pid_map_.insert(make_pair(port, i));
        }
        for (int i = 0; i < N; ++i) {
            fin >> port;
            sdr_port_.push_back(port);
            sdr_port_pid_map_.insert(make_pair(port, i));
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

// transaction format (without quotes):
// "ADD <songName> <songURL>"
// "REMOVE <songName>"
// "EDIT <songName> <newSongName> <newSongURL>"
void Controller::CreateTransactions() {
    transaction_.push_back(kAdd + " song19 http11");
    // transaction_.push_back(kRemove + " song01");
    // transaction_.push_back(kEdit + " song02 song55 http55");
}

// returns transaction string if transaction_id is valid
// else returns the string "NULL"
string Controller::get_transaction(int transaction_id) {
    if (transaction_id < transaction_.size()) return transaction_[transaction_id];
    else return "NULL";
}

// cancels all threads created by the process
// then, cancels the thread_entry thread for that process
void Controller::KillProcess(int process_id) {
    process_[process_id].CloseFDs();
    process_[process_id].CloseSDRFDs();
    process_[process_id].CloseAliveFDs();
    for(const auto &th: process_[process_id].thread_set) {
        pthread_cancel(th);
    }
    pthread_cancel(process_thread_[process_id]);

}

bool InitializeLocks() {
    if (pthread_mutex_init(&coordinator_lock, NULL) != 0) {
        cout << "C: Mutex init failed" << endl;
        return false;
    }
    return true;
}

int main() {
    Controller c;
    if (!InitializeLocks()) return 1;
    if (!c.ReadConfigFile()) return 1;
    c.CreateTransactions();
    if (!c.CreateProcesses()) return 1;
    // sleep(4);
    // c.KillProcess(0);
    // sleep(4);
    // c.KillProcess(1);
    // sleep(4);
    // c.KillProcess(2);
    c.WaitForThreadJoins();


    return 0;
}