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

/*! @file
 *  This file contains an ISA-portable PIN tool for functional simulation of
 *  instruction+data TLB+cache hierarchies
 */

#include <iostream>

#include "pin.H"

typedef UINT64 CACHE_STATS; // type of cache hit/miss counters
UINT64 total_ins_count=0;
UINT64 hit_miss_count[2]={0,0};
UINT64 prefetch_hit_miss_count[2]={0,0};

#include "pin_cache.H"

#define IP_TRACKER_COUNT 64
#define PREFETCH_DEGREE 1

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
LOCALVAR DTLB::CACHE dtlb("DTLB", DTLB::cacheSize, DTLB::lineSize, DTLB::associativity);

class IP_TRACKER {
  public:
    // the IP we're tracking
    uint64_t ip;

    // the last address accessed by this IP
    uint64_t last_cl_addr;

    // the stride between the last two addresses accessed by this IP
    int64_t last_stride;

    // use LRU to evict old IP trackers
    uint32_t lru;

    IP_TRACKER () {
        ip = 0;
        last_cl_addr = 0;
        last_stride = 0;
        lru = 0;
    };
};

IP_TRACKER trackers[IP_TRACKER_COUNT];

void stride_initialize()
{
    for (int i=0; i<IP_TRACKER_COUNT; i++)
        trackers[i].lru = i;
}

void stride_prefetch(uint64_t addr, uint64_t ip)
{

    // check for a tracker hit
    uint64_t cl_addr = addr / DTLB::lineSize;//>> LOG2_BLOCK_SIZE;

    int index = -1;
    for (index=0; index<IP_TRACKER_COUNT; index++) {
        if (trackers[index].ip == ip)
            break;
    }

    // this is a new IP that doesn't have a tracker yet, so allocate one
    if (index == IP_TRACKER_COUNT) {

        for (index=0; index<IP_TRACKER_COUNT; index++) {
            if (trackers[index].lru == (IP_TRACKER_COUNT-1))
                break;
        }

        trackers[index].ip = ip;
        trackers[index].last_cl_addr = cl_addr;
        trackers[index].last_stride = 0;

        //cout << "[IP_STRIDE] MISS index: " << index << " lru: " << trackers[index].lru << " ip: " << hex << ip << " cl_addr: " << cl_addr << dec << endl;

        for (int i=0; i<IP_TRACKER_COUNT; i++) {
            if (trackers[i].lru < trackers[index].lru)
                trackers[i].lru++;
        }
        trackers[index].lru = 0;

        return;
    }

    // sanity check
    // at this point we should know a matching tracker index
    if (index == -1)
        assert(0);

    // calculate the stride between the current address and the last address
    // this bit appears overly complicated because we're calculating
    // differences between unsigned address variables
    int64_t stride = 0;
    if (cl_addr > trackers[index].last_cl_addr)
        stride = cl_addr - trackers[index].last_cl_addr;
    else {
        stride = trackers[index].last_cl_addr - cl_addr;
        stride *= -1;
    }

    //cout << "[IP_STRIDE] HIT  index: " << index << " lru: " << trackers[index].lru << " ip: " << hex << ip << " cl_addr: " << cl_addr << dec << " stride: " << stride << endl;

    // don't do anything if we somehow saw the same address twice in a row
    if (stride == 0)
        return;

    // only do any prefetching if there's a pattern of seeing the same
    // stride more than once
    if (stride == trackers[index].last_stride) {

        // do some prefetching
        for (int i=0; i<PREFETCH_DEGREE; i++) {
            uint64_t pf_address = (cl_addr + (stride*(i+1))) * DTLB::lineSize;

            /*// only issue a prefetch if the prefetch address is in the same 4 KB page 
            // as the current demand access address
            if ((pf_address >> LOG2_PAGE_SIZE) != (addr >> LOG2_PAGE_SIZE))
                break;*/
	    bool hit = dtlb.AccessSingleLine(pf_address, CACHE_BASE::ACCESS_TYPE_LOAD);
            prefetch_hit_miss_count[hit]++;

            /*// check the MSHR occupancy to decide if we're going to prefetch to the L2 or LLC
            if (MSHR.occupancy < (MSHR.SIZE>>1))
	      prefetch_line(ip, addr, pf_address, FILL_L2, 0);
            else
	      prefetch_line(ip, addr, pf_address, FILL_LLC, 0);*/
        }
    }

    trackers[index].last_cl_addr = cl_addr;
    trackers[index].last_stride = stride;

    for (int i=0; i<IP_TRACKER_COUNT; i++) {
        if (trackers[i].lru < trackers[index].lru)
            trackers[i].lru++;
    }
    trackers[index].lru = 0;

    return;
}


LOCALFUN VOID Fini(int code, VOID * v)
{
    //std::cerr << itlb;
    UINT64 misses =  hit_miss_count[0]; //dtlb.Misses();
    UINT64 prefetch_misses = prefetch_hit_miss_count[0];
    std::cerr << ((1000000.0*misses)/total_ins_count) << std::endl;
    std::cerr << ((1000000.0*prefetch_misses)/total_ins_count) << std::endl;
    /*std::cerr << il1;
    std::cerr << dl1;
    std::cerr << ul2;
    std::cerr << ul3;*/
}

LOCALFUN VOID InsCount()
{
    total_ins_count+=1;
}

LOCALFUN VOID MemRef(ADDRINT addr, UINT32 size, CACHE_BASE::ACCESS_TYPE accessType, ADDRINT ip)
{
    // DTLB
    bool hit = dtlb.AccessSingleLine(addr, CACHE_BASE::ACCESS_TYPE_LOAD);
    hit_miss_count[hit]++;
    if(!hit)
    {
        stride_prefetch(addr, ip);
    }

    // first level D-cache
    //const BOOL dl1Hit = dl1.AccessSingleLine(addr, accessType);

    // second level unified Cache
    //if ( ! dl1Hit) Ul2Access(addr, size, accessType);
}

LOCALFUN VOID Instruction(INS ins, VOID *v)
{
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)InsCount, IARG_END);

    if (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins))
    {
        const AFUNPTR countFun = (AFUNPTR) MemRef;//(size <= 4 ? (AFUNPTR) MemRefSingle : (AFUNPTR) MemRefMulti);

        // only predicated-on memory instructions access D-cache
        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, countFun,
            IARG_MEMORYREAD_EA,
            IARG_MEMORYREAD_SIZE,
            IARG_UINT32, CACHE_BASE::ACCESS_TYPE_LOAD, IARG_INST_PTR,
            IARG_END);
    }

    if (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins))
    {
        const AFUNPTR countFun = (AFUNPTR) MemRef;//(size <= 4 ? (AFUNPTR) MemRefSingle : (AFUNPTR) MemRefMulti);

        // only predicated-on memory instructions access D-cache
        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, countFun,
            IARG_MEMORYWRITE_EA,
            IARG_MEMORYWRITE_SIZE,
            IARG_UINT32, CACHE_BASE::ACCESS_TYPE_STORE, IARG_INST_PTR,
            IARG_END);
    }
}

GLOBALFUN int main(int argc, char *argv[])
{
    PIN_Init(argc, argv);
    stride_initialize();

    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();

    return 0; // make compiler happy
}
