#include "process.h"
#include "controller.h"
#include "constants.h"
#include "fstream"
#include "iostream"
#include "unistd.h"
#include "limits.h"

int Controller::N;
int Controller::coordinator_;
std::unordered_set<int> Controller::alive_process_ids_;
std::vector<int> Controller::listen_port_;
std::vector<int> Controller::send_port_;
std::vector<int> Controller::alive_port_;
std::vector<int> Controller::sdr_port_;
std::vector<int> Controller::up_port_;

std::map<int, int> Controller::send_port_pid_map_;
std::map<int, int> Controller::alive_port_pid_map_;
std::map<int, int> Controller::sdr_port_pid_map_;
std::map<int, int> Controller::up_port_pid_map_;

std::vector<string> Controller::transaction_;

pthread_mutex_t coordinator_lock;

void Controller::set_coordinator(int coordinator_id) {
    pthread_mutex_lock(&coordinator_lock);
    coordinator_ = coordinator_id;
    pthread_mutex_unlock(&coordinator_lock);
}

int Controller::get_coordinator() {
    int c;
    pthread_mutex_lock(&coordinator_lock);
    c = coordinator_;
    pthread_mutex_unlock(&coordinator_lock);
    return c;
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
int Controller::get_up_port(int process_id) {
    return up_port_[process_id];
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

int Controller::get_up_port_pid_map(int port_num) {
    if (up_port_pid_map_.find(port_num) == up_port_pid_map_.end())
        return -1;
    else
        return up_port_pid_map_[port_num];
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
// sdr_port_ and up_port_
// constructs corresponding port_pid_maps
bool Controller::ReadConfigFile() {
    int count;
    ifstream temp;
    temp.open("configs/count");
    temp >> count;
    temp.close();
    cout << "Using config: count " << count << endl;
    ofstream tempw;
    tempw.open("configs/count");
    tempw << (count + 1) % 5;
    tempw.close();





    ifstream fin;
    fin.exceptions ( ifstream::failbit | ifstream::badbit );
    try {
        fin.open((kConfigFile + to_string(count)).c_str());
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
        for (int i = 0; i < N; ++i) {
            fin >> port;
            up_port_.push_back(port);
            up_port_pid_map_.insert(make_pair(port, i));
        }
        fin.close();

        return true;

    } catch (ifstream::failure e) {
        cout << e.what() << endl;
        if (fin.is_open()) fin.close();
        return false;
    }
}

// creates N Process objects
// calls Initialize for each process
// creates a thread for each process
bool Controller::CreateProcesses() {
    process_thread_.resize(N);
    // creating N Process objects
    process_.resize(N);
    for (int i = 0; i < N; i++) {
        process_[i] = new Process;
        process_[i]->Initialize(i,
                               kLogFile + to_string(i),
                               kPlaylistFile + to_string(i),
                               -1,
                               DONE);
        if (pthread_create(&process_thread_[i], NULL, ThreadEntry, (void *)process_[i])) {
            cout << "C: ERROR: Unable to create thread for P" << i << endl;
            return false;
        }
        AddToAliveProcessIds(i);
    }
    return true;
}

// increments all port values for a resurrecting process by 1
// done to prevent the resurrecting process from attempting to
// use ports it bound to in last transaction
// those ports might still be hogged
void Controller::IncrementPorts(int p) {
    int old_send = send_port_[p];
    int old_alive = alive_port_[p];
    int old_sdr = sdr_port_[p];
    int old_up = up_port_[p];


    auto it1 = send_port_pid_map_.find(old_send);
    send_port_pid_map_.erase(it1);
    send_port_pid_map_.insert(make_pair(old_send + 1, p));

    auto it2 = alive_port_pid_map_.find(old_alive);
    alive_port_pid_map_.erase(it2);
    alive_port_pid_map_.insert(make_pair(old_alive + 1, p));

    auto it3 = sdr_port_pid_map_.find(old_sdr);
    sdr_port_pid_map_.erase(it3);
    sdr_port_pid_map_.insert(make_pair(old_sdr + 1, p));

    auto it4 = up_port_pid_map_.find(old_up);
    up_port_pid_map_.erase(it4);
    up_port_pid_map_.insert(make_pair(old_up + 1, p));

    listen_port_[p]++;
    send_port_[p]++;
    alive_port_[p]++;
    sdr_port_[p]++;
    up_port_[p]++;
}

void Controller::FreeProcessMemory(int process_id) {
    delete process_[process_id];
}
// revives a dead process by calling its Initializer
// and creating a thread with ThreadEntry
// adds the process to alive_process_ids_ set
bool Controller::ResurrectProcess(int process_id) {
    IncrementPorts(process_id);
    process_[process_id] = new Process;
    process_[process_id]->Initialize(process_id,
                                    kLogFile + to_string(process_id),
                                    kPlaylistFile + to_string(process_id),
                                    INT_MAX,
                                    RECOVERY);
    if (pthread_create(&process_thread_[process_id], NULL, ThreadEntry, (void *)process_[process_id])) {
        cout << "C: ERROR: Unable to create thread for P" << process_id << endl;
        return false;
    }
    AddToAliveProcessIds(process_id);
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
    transaction_.push_back(kAdd + " song119 http111");
    transaction_.push_back(kRemove + " song01");
    // transaction_.push_back(kEdit + " song02 song55 http55");
}

// returns transaction string if transaction_id is valid
// else returns the string "NULL"
string Controller::get_transaction(int transaction_id) {
    if (transaction_id < transaction_.size()) return transaction_[transaction_id];
    else return "NULL";
}

// closes all FDs opened by the process
// closes server_sockfd
// cancels all threads created by the process
// then, cancels the thread_entry thread for that process
void Controller::KillProcess(int process_id) {
    process_[process_id]->CloseFDs();
    process_[process_id]->CloseSDRFDs();
    process_[process_id]->CloseUpFDs();
    process_[process_id]->CloseAliveFDs();
    process_[process_id]->Close_server_sockfd();
    for (const auto &th : process_[process_id]->thread_set) {
        pthread_cancel(th);
    }
    for (const auto &th : process_[process_id]->thread_set_alive_) {
        pthread_cancel(th);
    }
    pthread_cancel(process_thread_[process_id]);
    FreeProcessMemory(process_id);
    RemoveFromAliveProcessIds(process_id);
}

void Controller::AddToAliveProcessIds(int process_id) {
    alive_process_ids_.insert(process_id);
}

// from the set of alive_process_ids_ it selects the process
// with least id whose state is DONE
int Controller::ChooseCoordinator() {
    int coord_id = INT_MAX;
    for (const auto &p : alive_process_ids_) {
        if (process_[p]->get_my_status() == DONE) {
            coord_id = std::min(coord_id, p);
        }
    }
    cout << "C: Coordinator=" << coord_id << endl;
    return coord_id;
}

// sets handshake of all alive processes to EXPECTING
// indicates to processes that it is time to enter ThreadEntry function
void Controller::SetHandshakeToExpecting() {
    for (const auto &p : alive_process_ids_) {
        if (process_[p]->get_my_status() == DONE) {
            process_[p]->set_handshake(EXPECTING);
        }
    }
}

// waits till handskake of all alive processess is READY
void Controller::WaitTillHandshakeReady() {
    for (const auto &p : alive_process_ids_) {
        if (process_[p]->get_handshake() == READY) {
            continue;
        } else {
            usleep(kMiniSleep);
        }
    }
}

// sets coordinator's handshake value to INIT3PC
void Controller::SetCoordHandshakeToInit3PC() {
    process_[get_coordinator()]->set_handshake(INIT3PC);
}

// removes specified process from the alive_process_ids_ set
void Controller::RemoveFromAliveProcessIds(int process_id) {
    if (alive_process_ids_.find(process_id) != alive_process_ids_.end()) {
        alive_process_ids_.erase(process_id);
        FreeProcessMemory(process_id);
    }
}

// sets cooridinator's transaction_id
void Controller::InformCoordinatorOfNewTransaction(int coord_id, int tid) {
    process_[coord_id]->set_transaction_id(tid);
}

//Constructs Coordinator's participant_state_map_
void Controller::InformCoordiantorOfParticipants(int coord_id) {
    for (const auto &p : alive_process_ids_) {
        if (p == coord_id) continue;
        if (process_[p]->get_my_status() == DONE ) {
            process_[coord_id]->participant_state_map_.insert(make_pair(p, UNINITIALIZED));
        } else {
            continue;
        }
    }
}

// resets only those processes whose states are DONE
// also kills the alive threads of those processes
void Controller::ResetProcesses(int coord_id) {
    for (const auto &p : alive_process_ids_) {
        if (process_[p]->get_my_status() == DONE ) {
            process_[p]->KillAliveThreads();
            process_[p]->Reset(coord_id);
        } else {
            continue;
        }
    }
}

// kills all alive processes
// removes them from alive_process_ids_ set
void Controller::KillAllProcesses() {
    for (const auto &p : alive_process_ids_) {
        process_[p]->CloseFDs();
        process_[p]->CloseSDRFDs();
        process_[p]->CloseAliveFDs();
        process_[p]->Close_server_sockfd();
        for (const auto &th : process_[p]->thread_set) {
            pthread_cancel(th);
        }
        for (const auto &th : process_[p]->thread_set_alive_) {
            pthread_cancel(th);
        }
        pthread_cancel(process_thread_[p]);
    }

    for (const auto &p : alive_process_ids_) {
        FreeProcessMemory(p);
        RemoveFromAliveProcessIds(p);
    }
}

// kills the current coordinator_
void Controller::KillLeader() {
    KillProcess(get_coordinator());
}

// resurrects all dead processes
void Controller::ResurrectAll() {
    for (int i = 0; i < N; ++i) {
        // if process is NOT alive
        if(alive_process_ids_.find(i) == alive_process_ids_.end()) {
            ResurrectProcess(i);
        }
    }
}

// sets num_messages_ value for the given process
void Controller::SetMessageCount(int process_id, float num_messages) {
    process_[process_id]->set_num_messages(num_messages);
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


    int t = 0;
    while (c.get_transaction(t) != "NULL") {
        cout << "----------------TRANSACTION-" << t << "----------------" << endl;
        int coord_id = c.ChooseCoordinator();
        if (coord_id == INT_MAX) {
            cout << "C: No process can be made coordinator. Sleeping for some time." << endl;
            usleep(kGeneralSleep);
            continue;

        } else {
            c.set_coordinator(coord_id);
            c.ResetProcesses(coord_id);

            // sets transaction_id_ value for chosen coordinator
            c.InformCoordinatorOfNewTransaction(coord_id, t);
            c.InformCoordiantorOfParticipants(coord_id);

            c.SetMessageCount(0,8);
            c.SetMessageCount(1,2);
            c.SetMessageCount(2,1);
            c.SetMessageCount(3,1);
            c.SetMessageCount(4,1);

            c.SetHandshakeToExpecting();
            c.WaitTillHandshakeReady();
            c.SetCoordHandshakeToInit3PC();

            sleep(7);
            if (!c.ResurrectProcess(2)) return 1;
            if (!c.ResurrectProcess(3)) return 1;
            if (!c.ResurrectProcess(4)) return 1;
            sleep(7);
            if (!c.ResurrectProcess(1)) return 1;
            sleep(5);
            if (!c.ResurrectProcess(0)) return 1;
            
            // if (!c.ResurrectProcess(0)) return 1;
            
        }
        usleep(kTransactionSleep);
        t++;
    }

    c.WaitForThreadJoins();
    return 0;
}