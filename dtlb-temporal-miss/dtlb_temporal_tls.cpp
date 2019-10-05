/*
 * Copyright 2002-2019 Intel Corporation.
 * 
 * This software is provided to you as Sample Source Code as defined in the accompanying
 * End User License Agreement for the Intel(R) Software Development Products ("Agreement")
 * section 1.L.
 * 
 * This software and the related documents are provided as is, with no express or implied
 * warranties, other than those that are expressly stated in the License.
 */

#include <iostream>
#include <fstream>
#include "pin.H"
using std::ostream;
using std::cout;
using std::cerr;
using std::string;
using std::endl;

typedef UINT64 CACHE_STATS;

#include "pin_cache.H"

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "", "specify output file name");
string prefix="log";

INT32 numThreads = 0;
ostream* OutFile = NULL;

namespace DTLB
{
    // data TLB: 4 kB pages, 32 entries, fully associative
    const UINT32 lineSize = 4*KILO;
    const UINT32 cacheSize = 32 * lineSize;
    const UINT32 associativity = 32;
    const CACHE_ALLOC::STORE_ALLOCATION allocation = CACHE_ALLOC::STORE_ALLOCATE;

    const UINT32 max_sets = cacheSize / (lineSize * associativity);
    const UINT32 max_associativity = associativity;

    typedef CACHE_ROUND_ROBIN(max_sets, max_associativity, allocation) CACHE;
}

// Force each thread's data to be in its own data cache line so that
// multiple threads do not contend for the same data cache line.
// This avoids the false sharing problem.
#define PADSIZE 64  // 64 byte line size: 64-8
#define MAX_DIST 10001

// a running count of the instructions
class thread_data_t
{
  public:
    thread_data_t() : _count(0), _last_dtlb_miss_ins(0) {
    }
    UINT64 _count;
    UINT64 _last_dtlb_miss_ins;
    UINT64 *_miss_dist;//[MAX_DIST+2];
    DTLB::CACHE *dtlb;//("DTLB", DTLB::cacheSize, DTLB::lineSize, DTLB::associativity);
    UINT8 _pad[PADSIZE];
};

// key for accessing TLS storage in the threads. initialized once in main()
static  TLS_KEY tls_key = INVALID_TLS_KEY;

// This function is called before every block
VOID docount(THREADID threadid)
{
    thread_data_t* tdata = static_cast<thread_data_t*>(PIN_GetThreadData(tls_key, threadid));
    tdata->_count += 1;
}

VOID MemRef(ADDRINT addr, THREADID threadid)
{
    thread_data_t* tdata = static_cast<thread_data_t*>(PIN_GetThreadData(tls_key, threadid));
    bool hit = tdata->dtlb->AccessSingleLine(addr, CACHE_BASE::ACCESS_TYPE_LOAD);
    if(!hit)
    {
        UINT64 diff = (tdata->_count) - (tdata->_last_dtlb_miss_ins);
	tdata->_last_dtlb_miss_ins =tdata-> _count;
	if(diff<0)tdata->_miss_dist[MAX_DIST+1]++;
	else if(diff<MAX_DIST)tdata->_miss_dist[diff]++;
	else tdata->_miss_dist[MAX_DIST]++;
    }
}

VOID ThreadStart(THREADID threadid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    numThreads++;
    thread_data_t* tdata = new thread_data_t;
    if (PIN_SetThreadData(tls_key, tdata, threadid) == FALSE)
    {
        cerr << "PIN_SetThreadData failed" << endl;
        PIN_ExitProcess(1);
    }
    tdata->_miss_dist=new UINT64[MAX_DIST+2];
    for(int i=0;i<=MAX_DIST+1;i++)tdata->_miss_dist[i]=0;
    tdata->dtlb=new DTLB::CACHE("DTLB", DTLB::cacheSize, DTLB::lineSize, DTLB::associativity);
}


// Pin calls this function every time a new basic block is encountered.
// It inserts a call to docount.
VOID Instruction(INS ins, VOID *v)
{
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_THREAD_ID, IARG_END);
    if (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins))
    {
        const AFUNPTR countFun = (AFUNPTR) MemRef;
	INS_InsertPredicatedCall( ins, IPOINT_BEFORE, countFun, IARG_MEMORYREAD_EA, IARG_THREAD_ID, IARG_END);
    } 
    if (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins))
    {
        const AFUNPTR countFun = (AFUNPTR) MemRef;
	INS_InsertPredicatedCall( ins, IPOINT_BEFORE, countFun, IARG_MEMORYWRITE_EA, IARG_THREAD_ID, IARG_END);
    }

    // Visit every basic block  in the trace
    /*for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        // Insert a call to docount for every bbl, passing the number of instructions.

        BBL_InsertCall(bbl, IPOINT_ANYWHERE, (AFUNPTR)docount, IARG_FAST_ANALYSIS_CALL,
                       IARG_UINT32, BBL_NumIns(bbl), IARG_THREAD_ID, IARG_END);
    }*/
}

// This function is called when the thread exits
VOID ThreadFini(THREADID threadIndex, const CONTEXT *ctxt, INT32 code, VOID *v)
{
    thread_data_t* tdata = static_cast<thread_data_t*>(PIN_GetThreadData(tls_key, threadIndex));
    string log_file_name= prefix + decstr(threadIndex);
    ostream* thread_outfile = new std::ofstream(log_file_name.c_str());
    *thread_outfile << "Count[" << decstr(threadIndex) << "] = " << tdata->_count << endl;
    for(int i =0; i<=MAX_DIST+1;i++)
    {
        if(tdata->_miss_dist[i]!=0)
	{
	    *thread_outfile << i << "," << tdata->_miss_dist[i] << endl;
	}
    }
    delete [] tdata->_miss_dist;
    delete tdata->dtlb;
    delete tdata;
}

// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{
    *OutFile << "Total number of threads = " << numThreads << endl;
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    cerr << "This tool counts the number of dynamic instructions executed" << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return 1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char * argv[])
{
    // Initialize pin
    PIN_InitSymbols();
    if (PIN_Init(argc, argv))
        return Usage();

    OutFile = KnobOutputFile.Value().empty() ? &cout : new std::ofstream(KnobOutputFile.Value().c_str());
    prefix = KnobOutputFile.Value().empty() ? "thread_log" : KnobOutputFile.Value();

    // Obtain  a key for TLS storage.
    tls_key = PIN_CreateThreadDataKey(NULL);
    if (tls_key == INVALID_TLS_KEY)
    {
        cerr << "number of already allocated keys reached the MAX_CLIENT_TLS_KEYS limit" << endl;
        PIN_ExitProcess(1);
    }

    // Register ThreadStart to be called when a thread starts.
    PIN_AddThreadStartFunction(ThreadStart, NULL);

    // Register Fini to be called when thread exits.
    PIN_AddThreadFiniFunction(ThreadFini, NULL);

    // Register Fini to be called when the application exits.
    PIN_AddFiniFunction(Fini, NULL);

    // Register Instruction to be called to instrument instructions.
    INS_AddInstrumentFunction(Instruction, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 1;
}
