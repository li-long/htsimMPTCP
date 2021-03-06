#include "config.h"
#include <sstream>
#include <strstream>
#include <iostream>
#include <string.h>
#include <math.h>
#include "network.h"
#include "randomqueue.h"
#include "subflow_control.h"
#include "shortflows.h"
#include "pipe.h"
#include "eventlist.h"
#include "logfile.h"
#include "loggers.h"
#include "clock.h"
#include "mtcp.h"
#include "tcp.h"
#include "tcp_transfer.h"
#include "cbr.h"
#include "firstfit.h"
#include "topology.h"
#include "connection_matrix.h"
#include "fat_tree_topology.h"
//#include "star_topology.h"
#include <list>

// Simulation params

#define PRINT_PATHS 1

#define PERIODIC 0
#include "main.h"

int RTT = 100;					// Identical RTT microseconds = 0.1 ms
//int N = 512;
int N = 128;

FirstFit *ff = NULL;
unsigned int subflow_count = 1;

string ntoa (double n);
string itoa (uint64_t n);

//#define SWITCH_BUFFER (SERVICE * RTT / 1000)
#define USE_FIRST_FIT 0
#define FIRST_FIT_INTERVAL 100

EventList eventlist;

Logfile *lg;

void
exit_error (char *progr)
{
        cout << "Usage " << progr <<
             " [UNCOUPLED(DEFAULT)|COUPLED_INC|FULLY_COUPLED|COUPLED_EPSILON] [epsilon][COUPLED_SCALABLE_TCP"
             << endl;
        exit (1);
}

void
print_path (std::ofstream & paths, route_t * rt)
{
        for (unsigned int i = 1; i < rt->size () - 1; i += 2) {
                RandomQueue *q = (RandomQueue *) rt->at (i);

                if (q != NULL) {
                        paths << q->str () << " ";
                } else {
                        paths << "NULL ";
                }
        }

        paths << endl;
}

int
main (int argc, char **argv)
{
        eventlist.setEndtime (timeFromSec (200));
        Clock c (timeFromSec (50 / 100.), eventlist);
        int algo = COUPLED_EPSILON;
        double epsilon = 1;
        int param = 0;
        stringstream filename (ios_base::out);
        uint64_t pktperflow = 1048576LL/1000 + 1;
        if (argc > 1) {
                int i = 1;

                if (!strcmp (argv[1], "-o")) {
                        filename << argv[2];
                        i += 2;
                } else {
                        filename << "logout.dat";
                }

                if (argc > i && !strcmp (argv[i], "-sub")) {
                        subflow_count = atoi (argv[i + 1]);
                        i += 2;
                }

                if (argc > i && !strcmp (argv[i], "-flowM")) {
                        pktperflow = atoi (argv[i + 1])*1048576LL/1000;
                        i += 2;
                        cout << "Using subflow count " << subflow_count << endl;
                }
                if (argc > i && !strcmp (argv[i], "-param")) {
                        param = atoi (argv[i + 1]);
                        i += 2;
                        cout << "Using subflow count " << subflow_count << endl;
                }


                if (argc > i) {
                        epsilon = -1;

                        if (!strcmp (argv[i], "UNCOUPLED")) {
                                algo = UNCOUPLED;
                        } else if (!strcmp (argv[i], "COUPLED_INC")) {
                                algo = COUPLED_INC;
                        } else if (!strcmp (argv[i], "FULLY_COUPLED")) {
                                algo = FULLY_COUPLED;
                        } else if (!strcmp (argv[i], "COUPLED_TCP")) {
                                algo = COUPLED_TCP;
                        } else if (!strcmp (argv[i], "COUPLED_SCALABLE_TCP")) {
                                algo = COUPLED_SCALABLE_TCP;
                        } else if (!strcmp (argv[i], "COUPLED_EPSILON")) {
                                algo = COUPLED_EPSILON;

                                if (argc > i + 1) {
                                        epsilon = atof (argv[i + 1]);
                                }

                                printf ("Using epsilon %f\n", epsilon);
                        } else {
                                exit_error (argv[0]);
                        }
                }
        }

        srand (time (NULL));
        cout << "Using algo=" << algo << " epsilon=" << epsilon << endl;
        // prepare the loggers
        cout << "Logging to " << filename.str () << endl;
        //Logfile
        Logfile logfile (filename.str (), eventlist);
#if PRINT_PATHS
        filename << ".paths";
        cout << "Logging path choices to " << filename.str () << endl;
        std::ofstream paths (filename.str ().c_str ());

        if (!paths) {
                cout << "Can't open for writing paths file!" << endl;
                exit (1);
        }

#endif
        int tot_subs = 0;
        int cnt_con = 0;
        lg = &logfile;
        logfile.setStartTime (timeFromSec (0));
        SinkLoggerSampling sinkLogger =
                SinkLoggerSampling (timeFromMs (1000), eventlist);
        logfile.addLogger (sinkLogger);
        //TcpLoggerSimple logTcp;logfile.addLogger(logTcp);
        TcpSrc *tcpSrc;
        TcpSink *tcpSnk;
        //CbrSrc* cbrSrc;
        //CbrSink* cbrSnk;
        route_t *routeout, *routein;
        double extrastarttime;
        TcpRtxTimerScanner tcpRtxScanner (timeFromMs (10), eventlist);
        MultipathTcpSrc *mtcp;
        vector<MultipathTcpSrc*> mptcpVector;
        int dest;
#if USE_FIRST_FIT

        if (subflow_count == 1) {
                ff = new FirstFit (timeFromMs (FIRST_FIT_INTERVAL), eventlist);
        }

#endif
#ifdef FAT_TREE
        FatTreeTopology *top = new FatTreeTopology (&logfile, &eventlist, ff);
#endif
#ifdef OV_FAT_TREE
        OversubscribedFatTreeTopology *top =
                new OversubscribedFatTreeTopology (&logfile, &eventlist, ff);
#endif
#ifdef MH_FAT_TREE
        MultihomedFatTreeTopology *top =
                new MultihomedFatTreeTopology (&logfile, &eventlist, ff);
#endif
#ifdef STAR
        StarTopology *top = new StarTopology (&logfile, &eventlist, ff);
#endif
#ifdef BCUBE
        BCubeTopology *top = new BCubeTopology (&logfile, &eventlist, ff);
        cout << "BCUBE " << K << endl;
#endif
#ifdef VL2
        VL2Topology *top = new VL2Topology (&logfile, &eventlist, ff);
#endif
        vector < route_t * >***net_paths;
        net_paths = new vector < route_t * >**[N];
        int *is_dest = new int[N];

        for (int i = 0; i < N; i++) {
                is_dest[i] = 0;
                net_paths[i] = new vector < route_t * >*[N];

                for (int j = 0; j < N; j++) {
                        net_paths[i][j] = NULL;
                }
        }

        if (ff) {
                ff->net_paths = net_paths;
        }

        vector < int >*destinations;
        // Permutation connections
        ConnectionMatrix *conns = new ConnectionMatrix (N);
        //conns->setLocalTraffic(top);
        //cout << "Running perm with " << param << " connections" << endl;
        //conns->setPermutation(param);
        //conns->setStaggeredPermutation(top,(double)param/100.0);
        //conns->setStaggeredRandom(top,512,1);
//    conns->setHotspot(param,512/param);
        conns->setStride (3,0);
        //conns->setManytoMany(128);
        //conns->setVL2();
        //conns->setRandom(param);
        map < int, vector < int >*>::iterator it;
        int connID = 0;

        for (it = conns->connections.begin (); it != conns->connections.end ();
             it++) {
                int src = (*it).first;
                destinations = (vector < int >*) (*it).second;
                vector < int >subflows_chosen;

                for (unsigned int dst_id = 0; dst_id < destinations->size (); dst_id++) {
                        connID++;
                        dest = destinations->at (dst_id);

                        if (!net_paths[src][dest]) {
                                net_paths[src][dest] = top->get_paths (src, dest);
                        }

                        /*bool cbr = 1;
                           if (cbr){
                           cbrSrc = new CbrSrc(eventlist,speedFromPktps(7999),timeFromMs(0),timeFromMs(0));
                           cbrSnk = new CbrSink();

                           cbrSrc->setName("cbr_" + ntoa(src) + "_" + ntoa(dest)+"_"+ntoa(dst_id));
                           logfile.writeName(*cbrSrc);

                           cbrSnk->setName("cbr_sink_" + ntoa(src) + "_" + ntoa(dest)+"_"+ntoa(dst_id));
                           logfile.writeName(*cbrSnk);

                           // tell it the route
                           if (net_paths[src][dest]->size()==1){
                           choice = 0;
                           }
                           else {
                           choice = rand()%net_paths[src][dest]->size();
                           }

                           routeout = new route_t(*(net_paths[src][dest]->at(choice)));
                           routeout->push_back(cbrSnk);

                           cbrSrc->connect(*routeout, *cbrSnk, timeFromMs(0));
                           } */
                        {
                                //we should create multiple connections. How many?
                                //if (connID%3!=0)
                                //continue;
                                for (int connection = 0; connection < 1; connection++) {
                                        if (algo == COUPLED_EPSILON) {
                                                mtcp = new MultipathTcpSrc (algo, eventlist, NULL, epsilon);
                                        } else {
                                                mtcp = new MultipathTcpSrc (algo, eventlist, NULL);
                                        }

                                        mptcpVector.push_back(mtcp);
                                        //uint64_t bb = generateFlowSize();
                                        //      if (subflow_control)
                                        //subflow_control->add_flow(src,dest,mtcp);
                                        subflows_chosen.clear ();
                                        int it_sub;
                                        int crt_subflow_count = subflow_count;
                                        tot_subs += crt_subflow_count;
                                        cnt_con++;
                                        it_sub =
                                                crt_subflow_count >
                                                net_paths[src][dest]->
                                                size ()? net_paths[src][dest]->size () : crt_subflow_count;
                                        int use_all = it_sub == net_paths[src][dest]->size ();

                                        //if (connID%10!=0)
                                        //it_sub = 1;
                                        uint64_t pktpersubflow = pktperflow / it_sub;

                                        for (int inter = 0; inter < it_sub; inter++) {
                                                //              if (connID%10==0){
                                                tcpSrc = new TcpSrc (NULL, NULL, eventlist);
                                                tcpSrc->set_max_packets(pktpersubflow);
                                                tcpSnk = new TcpSink ();
                                                /*}
                                                   else {
                                                   tcpSrc = new TcpSrcTransfer(NULL,NULL,eventlist,bb,net_paths[src][dest]);
                                                   tcpSnk = new TcpSinkTransfer();
                                                   } */
                                                //if (connection==1)
                                                //tcpSrc->set_app_limit(9000);
                                                tcpSrc->setName ("mtcp_" + ntoa (src) + "_" +
                                                                 ntoa (inter) + "_" + ntoa (dest) + "(" +
                                                                 ntoa (connection) + ")");
                                                logfile.writeName (*tcpSrc);
                                                tcpSnk->setName ("mtcp_sink_" + ntoa (src) + "_" +
                                                                 ntoa (inter) + "_" + ntoa (dest) + "(" +
                                                                 ntoa (connection) + ")");
                                                logfile.writeName (*tcpSnk);
                                                tcpRtxScanner.registerTcp (*tcpSrc);
                                                /*int found;
                                                   do {
                                                   found = 0;

                                                   //if (net_paths[src][dest]->size()==K*K/4 && it_sub <= K/2)
                                                   //choice = rand()%(K/2);
                                                   //else
                                                   choice = rand()%net_paths[src][dest]->size();

                                                   for (unsigned int cnt = 0;cnt<subflows_chosen.size();cnt++){
                                                   if (subflows_chosen.at(cnt)==choice){
                                                   found = 1;
                                                   break;
                                                   }
                                                   }
                                                   }while(found);
                                                   // */
                                                int choice = 0;
#ifdef FAT_TREE
                                                choice = rand () % net_paths[src][dest]->size ();
#endif
#ifdef OV_FAT_TREE
                                                choice = rand () % net_paths[src][dest]->size ();
#endif
#ifdef MH_FAT_TREE

                                                if (use_all) {
                                                        choice = inter;
                                                } else {
                                                        choice = rand () % net_paths[src][dest]->size ();
                                                }

#endif
#ifdef VL2
                                                choice = rand () % net_paths[src][dest]->size ();
#endif
#ifdef STAR
                                                choice = 0;
#endif
#ifdef BCUBE
                                                //choice = inter;
                                                int min = -1, max = -1, minDist = 1000, maxDist = 0;

                                                if (subflow_count == 1) {
                                                        //find shortest and longest path
                                                        for (int dd = 0; dd < net_paths[src][dest]->size ();
                                                             dd++) {
                                                                if (net_paths[src][dest]->at (dd)->size () <
                                                                    minDist) {
                                                                        minDist =
                                                                                net_paths[src][dest]->at (dd)->size ();
                                                                        min = dd;
                                                                }

                                                                if (net_paths[src][dest]->at (dd)->size () >
                                                                    maxDist) {
                                                                        maxDist =
                                                                                net_paths[src][dest]->at (dd)->size ();
                                                                        max = dd;
                                                                }
                                                        }

                                                        choice = min;
                                                } else {
                                                        choice = rand () % net_paths[src][dest]->size ();
                                                }

#endif
                                                //cout << "Choice "<<choice<<" out of "<<net_paths[src][dest]->size();
                                                subflows_chosen.push_back (choice);

                                                /*if (net_paths[src][dest]->size()==K*K/4 && it_sub<=K/2){
                                                   int choice2 = rand()%(K/2); */
                                                if (choice >= net_paths[src][dest]->size ()) {
                                                        printf ("Weird path choice %d out of %u\n", choice,
                                                                net_paths[src][dest]->size ());
                                                        exit (1);
                                                }

#if PRINT_PATHS
                                                paths << "Route from " << ntoa (src) << " to " <<
                                                      ntoa (dest) << "  (" << choice << ") -> ";
                                                print_path (paths, net_paths[src][dest]->at (choice));
#endif
                                                routeout =
                                                        new route_t (*(net_paths[src][dest]->at (choice)));
                                                routeout->push_back (tcpSnk);
                                                routein = new route_t ();
                                                routein->push_back (tcpSrc);
                                                extrastarttime = 50 * drand ();
                                                //join multipath connection
                                                mtcp->addSubflow (tcpSrc);

                                                if (inter == 0) {
                                                        mtcp->setName ("multipath" + ntoa (src) + "_" +
                                                                       ntoa (dest) + "(" + ntoa (connection) +
                                                                       ")");
                                                        logfile.writeName (*mtcp);
                                                }

                                                tcpSrc->connect (*routeout, *routein, *tcpSnk,
                                                                 timeFromMs (extrastarttime));
#ifdef PACKET_SCATTER
                                                tcpSrc->set_paths (net_paths[src][dest]);
                                                cout << "Using PACKET SCATTER!!!!" << endl << end;
#endif

                                                if (ff && !inter) {
                                                        ff->add_flow (src, dest, tcpSrc);
                                                }

                                                sinkLogger.monitorSink (tcpSnk);
                                        }
                                }
                        }
                }
        }

        //ShortFlows* sf = new ShortFlows(2560, eventlist, net_paths,conns,lg, &tcpRtxScanner);
        cout << "Mean number of subflows " << ntoa ((double) tot_subs /
                        cnt_con) << endl;
        // Record the setup
        int pktsize = TcpPacket::DEFAULTDATASIZE;
        logfile.write ("# pktsize=" + ntoa (pktsize) + " bytes");
        logfile.write ("# subflows=" + ntoa (subflow_count));
        logfile.write ("# hostnicrate = " + ntoa (HOST_NIC) + " pkt/sec");
        logfile.write ("# corelinkrate = " + ntoa (HOST_NIC * CORE_TO_HOST) +
                       " pkt/sec");
        //logfile.write("# buffer = " + ntoa((double) (queues_na_ni[0][1]->_maxsize) / ((double) pktsize)) + " pkt");
        double rtt = timeAsSec (timeFromUs (RTT));
        logfile.write ("# rtt =" + ntoa (rtt));
        // GO!
        double last = 0.0;

        while (eventlist.doNextEvent ()) {
                if (timeAsMs(eventlist.now())>last+10.0) {
                        cout << (last =  timeAsMs(eventlist.now()))   << endl;

                        for (vector<MultipathTcpSrc*>::iterator iA = mptcpVector.begin(); iA!=mptcpVector.end(); iA++) {
                                cout << (*iA)->compute_total_bytes()*0.001*0.001 <<" ";
                        }

                        cout << endl;
                }
        }
}

string
ntoa (double n)
{
        stringstream s;
        s << n;
        return s.str ();
}

string
itoa (uint64_t n)
{
        stringstream s;
        s << n;
        return s.str ();
}
