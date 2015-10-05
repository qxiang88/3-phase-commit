#ifndef CONSTANTS_H
#define CONSTANTS_H
#include "string"
#include "sys/time.h"
using namespace std;

#define PR(x) cout << #x " = " << x << "\n";

const string kConfigFile = "./config";
const string kLogFile = "./log/log";
const string kPlaylistFile = "./playlist/playlist";
const int kMaxDataSize = 200 ;          // max number of bytes we can get at once
const int kBacklog = 10;                // how many pending connections queue will hold

const string kAdd = "ADD";
const string kRemove = "REMOVE";
const string kEdit = "EDIT";
// timeout for select call.
// must be significantly less than the timeout for messages
// so that temp_fds_ set is periodically updated
// TODO: change its value
const timeval kTimeout = {
    0,          // tv_sec
    2000*1000    //tv_usec (microsec)
};

// sleep for the select loop
// sleeps for some time after releasing the fd_set_lock
// giving chance for AddToFdSet/RemoveFromFdSet to acquire it
// and make changes
// time in microsec
// TODO: change its value
const time_t kSelectSleep = 5000*1000;

const string kVoteReq = "VOTE-REQ";
const string kYes = "YES";
const string kNo = "NO";
const string kAck = "ACK";
const string kAbort = "ABORT";
const string kPreCommit = "PRE-COMMIT";
const string kCommit = "COMMIT";
#endif //CONSTANTS_H
