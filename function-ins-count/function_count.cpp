#include <iostream>
#include "pin.H"
#include <map>
#include <string>
#include <cstdio>

using namespace std;

UINT32 current_index = 0;

map<string,UINT32> func_name_to_index;
map<UINT32,UINT64> func_count;

VOID PIN_FAST_ANALYSIS_CALL docount(UINT32 c,UINT32 index)
{
  //icount+=c;
  if(func_count.find(index)==func_count.end())func_count[index]=c;
  else func_count[index]+=c;
}

VOID Trace(TRACE trace, VOID *v)
{
  
  for(BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl=BBL_Next(bbl))
  {
    RTN tmp = INS_Rtn(BBL_InsHead(bbl));
    string name = "Unknown";
    if(RTN_Valid(tmp))
    {
      name=IMG_Name(SEC_Img(RTN_Sec(tmp)));      
    }
    UINT32 index;
    if(func_name_to_index.find(name)==func_name_to_index.end())
    {
      func_name_to_index[name]=current_index;
      index=current_index;
      current_index++;
    }
    else
    {
      index = func_name_to_index[name];
    }
    BBL_InsertCall(bbl, IPOINT_ANYWHERE, (AFUNPTR)docount, IARG_FAST_ANALYSIS_CALL, IARG_UINT32, BBL_NumIns(bbl), IARG_UINT32, index, IARG_END);
  }
}

VOID Fini(INT32 code, VOID *v)
{
  FILE * fp = fopen("/tmp/func_count.txt", "w");
  for(map<string,UINT32>::iterator it = func_name_to_index.begin(); it != func_name_to_index.end(); it++)
  {
    string name=it->first;
    UINT32 index = it->second;
    fprintf(fp,"%lu %s\n",func_count[index],name.c_str());
    //cerr << name << func_count[index]<<endl;
  }
  fclose(fp);
}

INT32 Usage()
{
  cerr << "This tool counts the number of dynamic instructions executed" << endl;
  cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
  return 1;
}

int main(int argc, char * argv[])
{
  PIN_InitSymbols();
  if (PIN_Init(argc, argv))return Usage();
  PIN_AddFiniFunction(Fini, NULL);
  TRACE_AddInstrumentFunction(Trace, NULL);
  PIN_StartProgram();
  return 0;
}
