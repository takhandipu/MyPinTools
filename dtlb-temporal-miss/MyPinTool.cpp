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
#include <map>

#include "pin.H"

typedef UINT64 CACHE_STATS; // type of cache hit/miss counters
UINT64 total_ins_count=0;
UINT64 last_dtlb_miss_ins=0;

#include "pin_cache.H"

std::map<UINT64,UINT64> miss_dist;


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

LOCALFUN VOID Fini(int code, VOID * v)
{
    //std::cerr << itlb;
    UINT64 misses = dtlb.Misses();
    //std::cerr << ((1000000.0*misses)/total_ins_count) << std::endl;
    UINT64 running_miss_count = 0;
    for(std::map<UINT64,UINT64>::iterator current = miss_dist.begin(); current != miss_dist.end(); current++)
    {
        running_miss_count += current->second;
	std::cerr << current->first  << "," << 1.0*running_miss_count / misses << "\n"; 
    }
    /*std::cerr << il1;
    std::cerr << dl1;
    std::cerr << ul2;
    std::cerr << ul3;*/
}

LOCALFUN VOID InsCount()
{
    total_ins_count+=1;
}

LOCALFUN VOID MemRef(ADDRINT addr, UINT32 size, CACHE_BASE::ACCESS_TYPE accessType)
{
    // DTLB
    bool hit = dtlb.AccessSingleLine(addr, CACHE_BASE::ACCESS_TYPE_LOAD);
    if(!hit)
    {
        UINT64 diff = total_ins_count - last_dtlb_miss_ins;
	last_dtlb_miss_ins = total_ins_count;
	if(miss_dist.find(diff)==miss_dist.end())miss_dist[diff]=1;
	else miss_dist[diff]+=1;
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
            IARG_UINT32, CACHE_BASE::ACCESS_TYPE_LOAD,
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
            IARG_UINT32, CACHE_BASE::ACCESS_TYPE_STORE,
            IARG_END);
    }
}

GLOBALFUN int main(int argc, char *argv[])
{
    PIN_Init(argc, argv);

    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();

    return 0; // make compiler happy
}
