#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <vector>
#include <memory>
namespace sonyraw {
enum class Family { Unsupported, DualShock4, DualSense, DualSenseEdge, Xbox };
enum class Transport { Unknown, USB, Bluetooth };
Family classify(unsigned vid, unsigned pid);
// Sony HID button usages 1..14: square,cross,circle,triangle,L1,R1,L2,R2,
// share/create,options,L3,R3,PS,touchpad. Bit n corresponds to usage n+1.
struct State { float lx=0,ly=0,rx=0,ry=0,l2=0,r2=0; uint32_t buttons=0; int hat=-1; bool valid=false; uint64_t sequence=0; };
struct Axis { long value=0,min=0,max=255; bool present=false; };
struct Sample { Axis x,y,z,rz,rx,ry; uint32_t buttons=0; int hat=-1; bool buttons_present=false; };
State decode_sample(const Sample& sample);
struct Device { HANDLE handle=nullptr; unsigned vid=0,pid=0; uint64_t instance_key=0; bool instance_key_stable=false; Family family=Family::Unsupported; Transport transport=Transport::Unknown; State state; uint64_t rejected_reports=0; };
// One backend on its owning window thread. Raw Input registrations are process
// global per usage; caller owns their lifetime. No registration in destructor.
class Backend {
public:
 Backend(); ~Backend(); Backend(const Backend&)=delete; Backend& operator=(const Backend&)=delete;
 bool attach(HWND window,bool background=false);
 bool refresh();
 // Call before DefWindowProc; WM_INPUT must still reach DefWindowProc for cleanup.
 bool handle(UINT message,WPARAM wparam,LPARAM lparam);
 std::vector<Device> devices() const;
private: struct Impl; std::unique_ptr<Impl> impl;
};
}

