#ifndef shortflows
#define shortflows

#include "main.h"
#include "tcp.h"
#include "randomqueue.h"
#include "logfile.h"
#include "eventlist.h"
#include "tcp_transfer.h"
#include <list>
#include <map>
#include "connection_matrix.h"
extern int N;

struct ShortFlow{
  TcpSrcTransfer* src;
  TcpSinkTransfer* snk; 
};

class ShortFlows: public EventSource{
 public:
  ShortFlows(double l, EventList& eventlist,vector<route_t*>*** np,ConnectionMatrix* c,Logfile* logfile,TcpRtxTimerScanner* r);
  void doNextEvent();

  void run();


  ShortFlow* createConnection(int src, int dst, simtime_picosec starttime);
  vector<route_t*>*** net_paths;
 private:
  vector<ShortFlow*> connections[1024][1024];
  vector<connection*>* _traffic_matrix;
  Logfile* logfile;

  //create connections periodically
  //change path
  //start
  //log finish time when it's done
  
  double _lambda;  
  TcpRtxTimerScanner* tcpRtxScanner;
};

#endif
