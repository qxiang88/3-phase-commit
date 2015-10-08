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

const time_t kGeneralSleep = 2000 * 1000;
const time_t kSendAliveInterval = 1000 * 1000;  // MUST be less than the kTimeout, preferably at least 1 sec less
const time_t kAliveTimeout = 900*1000;
const timeval kReceiveAliveInterval = {
    0,          // tv_sec
    900 * 1000  //tv_usec (microsec)
};   // slightly less than sendAliveInterval to avoid race conditions

// timeout for select call (receive timeout)
const timeval kTimeout = {
    3,          // tv_sec
    0  //tv_usec (microsec)
};
const string kURElected = "URELECTED";
const string kAlive = "ALIVE";
const string kVoteReq = "VOTE-REQ";
const string kStateReq = "STATE-REQ";
const string kYes = "YES";
const string kNo = "NO";
const string kAck = "ACK";
const string kAbort = "ABORT";
const string kPreCommit = "PRE-COMMIT";
const string kCommit = "COMMIT";

// const string kCommitted = "COMMITTED";
// const string kCommittable = "COMMITTABLE";
// const string kAborted = "ABORTED";

#endif //CONSTANTS_H
