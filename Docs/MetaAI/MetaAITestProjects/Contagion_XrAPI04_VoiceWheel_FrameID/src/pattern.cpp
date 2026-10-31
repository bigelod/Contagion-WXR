
#include "pattern.h"
#include <Psapi.h>
#include <vector>
uintptr_t PatternScan(HMODULE mod, const char* pattern){
    MODULEINFO mi; GetModuleInformation(GetCurrentProcess(), mod, &mi, sizeof(mi));
    uintptr_t base=(uintptr_t)mi.lpBaseOfDll;
    size_t size=mi.SizeOfImage;
    std::vector<int> bytes;
    char* start=const_cast<char*>(pattern);
    char* end=start+strlen(pattern);
    for(char* cur=start;cur<end;++cur){
        if(*cur==' ') continue;
        if(*cur=='?'){ bytes.push_back(-1); if(*(cur+1)=='?') cur++; }
        else { char buf[3]={cur[0],cur[1],0}; bytes.push_back(strtol(buf,nullptr,16)); cur++; }
    }
    for(size_t i=0;i<size-bytes.size();i++){
        bool ok=true;
        for(size_t j=0;j<bytes.size();j++){ if(bytes[j]==-1) continue; if(((unsigned char*)base)[i+j]!=bytes[j]){ ok=false; break; } }
        if(ok) return base+i;
    }
    return 0;
}
