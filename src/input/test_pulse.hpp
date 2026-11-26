#pragma once
#include <windows.h>
#include <cstdio>
#include <cstring>
// Explicit opt-in, process-local DI state only. No OS keyboard injection.
namespace test_pulse {
struct Pulse {
 unsigned long long sequence=0, until=0; unsigned char keys[256]={};
 bool accept(const char* text,unsigned long long now) {
  unsigned long long id=0; unsigned duration=0; int consumed=0;
  if(sscanf_s(text,"%llu %u %n",&id,&duration,&consumed)!=2 || !id || id<=sequence || duration>10000) return false;
  unsigned char next[256]={}; const char* cursor=text+consumed; unsigned count=0;
  while(*cursor) {
   while(*cursor==' '||*cursor=='\n'||*cursor=='\r'||*cursor=='\t') ++cursor;
   if(!*cursor) break;
   unsigned scan=0; int used=0;
   if(sscanf_s(cursor,"%u%n",&scan,&used)!=1 || !used || !scan || scan>255 || ++count>16) return false;
   next[scan]=0x80; cursor+=used;
  }
  sequence=id; until=now+duration; memcpy(keys,next,256); return true;
 }
 void apply(unsigned char* state,unsigned long long now) const {
  if(now<until) for(unsigned i=0;i<256;++i) state[i]|=keys[i];
 }
};
inline void overlay(unsigned char* state) {
 static SRWLOCK lock=SRWLOCK_INIT; static Pulse pulse;
 static wchar_t path[32768]={}; static bool initialized=false;
 static unsigned long long next_read=0;
 AcquireSRWLockExclusive(&lock);
 if(!initialized) {
  const DWORD n=GetEnvironmentVariableW(L"USM_TEST_PULSE_FILE",path,32768);
  if(!n || n>=32768 || !(path[0] && path[1]==L':' && (path[2]==L'\\'||path[2]==L'/'))) path[0]=0;
  initialized=true;
 }
 const auto now=GetTickCount64();
 if(path[0] && now>=next_read) {
  next_read=now+20;
  HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file!=INVALID_HANDLE_VALUE) {
   char text[512]={}; DWORD got=0;
   if(GetFileSize(file,nullptr)<sizeof(text) && ReadFile(file,text,sizeof(text)-1,&got,nullptr) && got) pulse.accept(text,now);
   CloseHandle(file);
  }
 }
 pulse.apply(state,now); ReleaseSRWLockExclusive(&lock);
}
}
