// Own compatibility backend. No original game bytes, no system DirectInput calls.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <new>
#include <cstring>
#include <initializer_list>
#include <climits>
#include "sony_di.hpp"
#include "sony_prompts.hpp"
#include <cstdio>
#include "character_lighting.hpp"
#include "test_pulse.hpp"

static_assert(sizeof(void*)==4,"x86 COM ABI required");
static volatile LONG live_objects=0;
static HRESULT unsupported(){return DIERR_UNSUPPORTED;}
static bool own_foreground(HWND window){
 DWORD pid=0; HWND fg=GetForegroundWindow();
 if(!fg||(window&&!IsWindow(window))) return false;
 GetWindowThreadProcessId(fg,&pid);
 return pid==GetCurrentProcessId() && (!window||GetAncestor(fg,GA_ROOT)==GetAncestor(window,GA_ROOT));
}
static void instance(DIDEVICEINSTANCEA& i,bool keyboard){
 i={}; i.dwSize=sizeof(i); i.guidInstance=i.guidProduct=keyboard?GUID_SysKeyboard:GUID_SysMouse;
 i.dwDevType=keyboard?DI8DEVTYPE_KEYBOARD:DI8DEVTYPE_MOUSE;
 strcpy_s(i.tszInstanceName,keyboard?"USM User32 keyboard":"USM User32 mouse");
 strcpy_s(i.tszProductName,i.tszInstanceName);
}

class Device:public IDirectInputDevice8A {
 LONG refs_=1; bool keyboard_,format_=false; volatile LONG acquired_=0;
 HWND window_=nullptr; DWORD coop_=0;
 HANDLE thread_=nullptr,ready_=nullptr; HWND receiver_=nullptr; DWORD receiver_error_=0;
 CRITICAL_SECTION lock_; LONG x_=0,y_=0,z_=0; bool raw_ready_=false;
 static LRESULT CALLBACK window_proc(HWND h,UINT m,WPARAM w,LPARAM l){
  auto self=reinterpret_cast<Device*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(m==WM_NCCREATE){self=static_cast<Device*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
  if(m==WM_INPUT && self){
   // Never inspect raw input while another process/window owns the foreground.
   if(InterlockedCompareExchange(&self->acquired_,0,0) && own_foreground(self->window_)){
    RAWINPUT input={}; UINT bytes=sizeof(input);
    if(GetRawInputData(reinterpret_cast<HRAWINPUT>(l),RID_INPUT,&input,&bytes,sizeof(RAWINPUTHEADER))!=UINT(-1) && input.header.dwType==RIM_TYPEMOUSE){
     const auto& mouse=input.data.mouse;
     if(!(mouse.usFlags&MOUSE_MOVE_ABSOLUTE)){
      EnterCriticalSection(&self->lock_);
      // Saturate accumulator; no undefined signed overflow for delayed consumers.
      auto add=[](LONG a,LONG b){LONGLONG v=static_cast<LONGLONG>(a)+b;return LONG(v>LONG_MAX?LONG_MAX:v<LONG_MIN?LONG_MIN:v);};
      self->x_=add(self->x_,mouse.lLastX);self->y_=add(self->y_,mouse.lLastY);
      if(mouse.usButtonFlags&RI_MOUSE_WHEEL)self->z_=add(self->z_,static_cast<SHORT>(mouse.usButtonData));
      LeaveCriticalSection(&self->lock_);
     }
    }
   } else {EnterCriticalSection(&self->lock_);self->x_=self->y_=self->z_=0;LeaveCriticalSection(&self->lock_);}
   return DefWindowProcW(h,m,w,l);
  }
  if(m==WM_CLOSE){DestroyWindow(h);return 0;}
  if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
  return DefWindowProcW(h,m,w,l);
 }
 static DWORD WINAPI receiver_thread(void* arg){
  auto self=static_cast<Device*>(arg); HINSTANCE module=GetModuleHandleW(nullptr);
  // A private class and message-only window, no subclassing/hooks/capture.
  WNDCLASSW wc={};wc.lpfnWndProc=window_proc;wc.hInstance=module;wc.lpszClassName=L"USMNativeInputRawMouseV1";
  ATOM atom=RegisterClassW(&wc);
  if(!atom){self->receiver_error_=GetLastError();SetEvent(self->ready_);return 1;}
  HWND window=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,module,self);
  self->receiver_=window;
  if(!window)self->receiver_error_=GetLastError();
  else {
   RAWINPUTDEVICE raw={0x01,0x02,RIDEV_INPUTSINK,window};
   if(!RegisterRawInputDevices(&raw,1,sizeof(raw)))self->receiver_error_=GetLastError();
   else self->raw_ready_=true;
  }
  SetEvent(self->ready_);
  if(self->raw_ready_){MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}}
  if(self->raw_ready_){RAWINPUTDEVICE raw={0x01,0x02,RIDEV_REMOVE,nullptr};RegisterRawInputDevices(&raw,1,sizeof(raw));}
  if(IsWindow(window))DestroyWindow(window);
  UnregisterClassW(wc.lpszClassName,module);return 0;
 }
 HRESULT active(){
  if(!InterlockedCompareExchange(&acquired_,0,0))return DIERR_NOTACQUIRED;
  if(!own_foreground(window_)){InterlockedExchange(&acquired_,0);clear();return DIERR_INPUTLOST;}
  return DI_OK;
 }
 void clear(){EnterCriticalSection(&lock_);x_=y_=z_=0;LeaveCriticalSection(&lock_);}
public:
 explicit Device(bool key):keyboard_(key){InitializeCriticalSection(&lock_);InterlockedIncrement(&live_objects);}
 virtual ~Device(){Unacquire();if(thread_){if(receiver_)PostMessageW(receiver_,WM_CLOSE,0,0);WaitForSingleObject(thread_,INFINITE);CloseHandle(thread_);}if(ready_)CloseHandle(ready_);DeleteCriticalSection(&lock_);InterlockedDecrement(&live_objects);}
 HRESULT start(){
  if(keyboard_)return DI_OK;
  ready_=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!ready_)return HRESULT_FROM_WIN32(GetLastError());
  thread_=CreateThread(nullptr,0,receiver_thread,this,0,nullptr);if(!thread_)return HRESULT_FROM_WIN32(GetLastError());
  if(WaitForSingleObject(ready_,5000)!=WAIT_OBJECT_0)return E_FAIL;
  return raw_ready_?DI_OK:HRESULT_FROM_WIN32(receiver_error_?receiver_error_:ERROR_NOT_READY);
 }
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown&&iid!=IID_IDirectInputDevice8A)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
 ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs_);}
 ULONG STDMETHODCALLTYPE Release()override{LONG n=InterlockedDecrement(&refs_);if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS caps)override{if(!caps||caps->dwSize!=sizeof(*caps))return DIERR_INVALIDPARAM;*caps={};caps->dwSize=sizeof(*caps);caps->dwFlags=DIDC_ATTACHED|DIDC_EMULATED;caps->dwDevType=keyboard_?DI8DEVTYPE_KEYBOARD:DI8DEVTYPE_MOUSE;caps->dwAxes=keyboard_?0:3;caps->dwButtons=keyboard_?256:5;return DI_OK;}
 HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA,LPVOID,DWORD)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE GetProperty(REFGUID,LPDIPROPHEADER)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE SetProperty(REFGUID,LPCDIPROPHEADER)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE Acquire()override{if(!format_)return DIERR_INVALIDPARAM;if(!own_foreground(window_))return DIERR_OTHERAPPHASPRIO;if(InterlockedExchange(&acquired_,1))return S_FALSE;clear();return DI_OK;}
 HRESULT STDMETHODCALLTYPE Unacquire()override{InterlockedExchange(&acquired_,0);clear();return DI_OK;}
 HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD size,LPVOID data)override{
  if(!data||size!=(keyboard_?256:sizeof(DIMOUSESTATE2)))return DIERR_INVALIDPARAM;
  HRESULT hr=active();if(FAILED(hr))return hr;
  if(keyboard_){
   BYTE state[256]={}; HKL layout=GetKeyboardLayout(0);
   for(UINT scan=1;scan<256;++scan){
    UINT vk=scan==DIK_PAUSE?VK_PAUSE:scan==DIK_SYSRQ?VK_SNAPSHOT:MapVirtualKeyExW(scan<128?scan:(0xe000|(scan&0x7f)),MAPVK_VSC_TO_VK_EX,layout);
    if(vk && (GetAsyncKeyState(static_cast<int>(vk))&0x8000))state[scan]=0x80;
   }
   test_pulse::overlay(state);
   // Recheck after gathering. Never deliver data after a focus change.
   if(!own_foreground(window_)){Unacquire();return DIERR_INPUTLOST;}
   memcpy(data,state,sizeof(state));
  }else{
   DIMOUSESTATE2 state={};EnterCriticalSection(&lock_);state.lX=x_;state.lY=y_;state.lZ=z_;x_=y_=z_=0;LeaveCriticalSection(&lock_);
   const int buttons[]={VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2};
   for(int i=0;i<5;++i)if(GetAsyncKeyState(buttons[i])&0x8000)state.rgbButtons[i]=0x80;
   if(!own_foreground(window_)){Unacquire();return DIERR_INPUTLOST;}
   memcpy(data,&state,sizeof(state));
  }
  return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT f)override{
  if(InterlockedCompareExchange(&acquired_,0,0))return DIERR_ACQUIRED;
  if(!f||f->dwSize!=sizeof(*f)||f->dwObjSize!=sizeof(DIOBJECTDATAFORMAT)||f->dwFlags!=DIDF_RELAXIS||!f->rgodf)return DIERR_INVALIDPARAM;
  if(f->dwDataSize!=(keyboard_?256:20)||f->dwNumObjs!=(keyboard_?256:11))return DIERR_UNSUPPORTED;
  for(DWORD i=0;i<f->dwNumObjs;++i){const auto& o=f->rgodf[i];
   if(keyboard_){if(!o.pguid||*o.pguid!=GUID_Key||o.dwOfs!=i||o.dwType!=(0x80000000u|DIDFT_BUTTON|DIDFT_MAKEINSTANCE(i))||o.dwFlags)return DIERR_UNSUPPORTED;}
   else {DWORD type=(i<3?DIDFT_AXIS:DIDFT_BUTTON)|DIDFT_ANYINSTANCE;if(i==2||i>=5)type|=0x80000000u;
    const GUID* guid=i==0?&GUID_XAxis:i==1?&GUID_YAxis:i==2?&GUID_ZAxis:nullptr;
    if(o.dwOfs!=(i<3?i*4:i+9)||o.dwType!=type||o.dwFlags||(guid?(!o.pguid||*o.pguid!=*guid):o.pguid!=nullptr))return DIERR_UNSUPPORTED;}
  }
  format_=true;return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND window,DWORD flags)override{
  DWORD pid=0;if(!window||!IsWindow(window))return DIERR_INVALIDPARAM;GetWindowThreadProcessId(window,&pid);
  if(pid!=GetCurrentProcessId()||GetAncestor(window,GA_ROOT)!=window)return DIERR_INVALIDPARAM;
  if(flags!=(DISCL_FOREGROUND|DISCL_NONEXCLUSIVE))return DIERR_UNSUPPORTED;
  if(InterlockedCompareExchange(&acquired_,0,0))return DIERR_ACQUIRED;
  window_=window;coop_=flags;return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA,DWORD,DWORD)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEA info)override{if(!info||info->dwSize!=sizeof(*info))return DIERR_INVALIDPARAM;instance(*info,keyboard_);return DI_OK;}
 HRESULT STDMETHODCALLTYPE RunControlPanel(HWND,DWORD)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE,DWORD,REFGUID)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID,LPCDIEFFECT,LPDIRECTINPUTEFFECT* out,LPUNKNOWN)override{if(out)*out=nullptr;return unsupported();}
 HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKA,LPVOID,DWORD)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOA,REFGUID)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK,LPVOID,DWORD)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE Poll()override{return active();}
 HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD,LPCDIDEVICEOBJECTDATA,LPDWORD,DWORD)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCSTR,LPDIENUMEFFECTSINFILECALLBACK,LPVOID,DWORD)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCSTR,DWORD,LPDIFILEEFFECT,DWORD)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATA,LPCSTR,DWORD)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATA,LPCSTR,DWORD)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA)override{return unsupported();}
};

#include "sony_device.hpp"

class Input final:public IDirectInput8A{
 LONG refs_=1;
 std::shared_ptr<sony_di::Hub> sony_;
public:
 Input():sony_(sony_di::acquire_hub()){InterlockedIncrement(&live_objects);}~Input(){InterlockedDecrement(&live_objects);}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown&&iid!=IID_IDirectInput8A)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
 ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs_);}
 ULONG STDMETHODCALLTYPE Release()override{LONG n=InterlockedDecrement(&refs_);if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID guid,LPDIRECTINPUTDEVICE8A* out,LPUNKNOWN outer)override{
  if(!out)return E_POINTER;*out=nullptr;if(outer)return CLASS_E_NOAGGREGATION;
  if(guid!=GUID_SysKeyboard&&guid!=GUID_SysMouse){
   try {
    sonyraw::Device sony;
    if(!sony_->find(guid,sony)) return DIERR_DEVICENOTREG;
    *out=new(std::nothrow) SonyDevice(sony_,sony);
    return *out?DI_OK:E_OUTOFMEMORY;
   } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
  }
  auto dev=new(std::nothrow) Device(guid==GUID_SysKeyboard);if(!dev)return E_OUTOFMEMORY;
  HRESULT hr=dev->start();if(FAILED(hr)){dev->Release();return hr;}*out=dev;return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE EnumDevices(DWORD type,LPDIENUMDEVICESCALLBACKA callback,LPVOID context,DWORD flags)override{
  if(!callback||flags&~(DIEDFL_ATTACHEDONLY|DIEDFL_INCLUDEALIASES|DIEDFL_INCLUDEPHANTOMS|DIEDFL_FORCEFEEDBACK))return DIERR_INVALIDPARAM;
  if(flags&DIEDFL_FORCEFEEDBACK)return DI_OK;
  if(type!=DI8DEVCLASS_ALL&&type!=DI8DEVCLASS_DEVICE&&type!=DI8DEVCLASS_POINTER&&type!=DI8DEVCLASS_KEYBOARD&&type!=DI8DEVCLASS_GAMECTRL&&type!=DI8DEVTYPE_MOUSE&&type!=DI8DEVTYPE_KEYBOARD)return DIERR_INVALIDPARAM;
  for(bool key:{true,false}){if(type==DI8DEVCLASS_GAMECTRL)continue;if((type==DI8DEVCLASS_KEYBOARD||type==DI8DEVTYPE_KEYBOARD)&&!key)continue;if((type==DI8DEVCLASS_POINTER||type==DI8DEVTYPE_MOUSE)&&key)continue;
   DIDEVICEINSTANCEA i;instance(i,key);if(callback(&i,context)==DIENUM_STOP)return DI_OK;}
  if(type==DI8DEVCLASS_ALL||type==DI8DEVCLASS_GAMECTRL){
   try {
    for(const auto& sony:sony_->devices(true)){
     const auto info=sony_di::instance(sony);
     if(callback(&info,context)==DIENUM_STOP)break;
    }
   } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
  }
  return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID guid)override{
  if(guid==GUID_SysKeyboard||guid==GUID_SysMouse) return DI_OK;
  try { sonyraw::Device sony; return sony_->find(guid,sony)?DI_OK:DIERR_DEVICENOTREG; }
  catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 }
 HRESULT STDMETHODCALLTYPE RunControlPanel(HWND,DWORD)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE,DWORD)override{return DIERR_ALREADYINITIALIZED;}
 HRESULT STDMETHODCALLTYPE FindDevice(REFGUID,LPCSTR,LPGUID)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(LPCSTR,LPDIACTIONFORMATA,LPDIENUMDEVICESBYSEMANTICSCBA,LPVOID,DWORD)override{return unsupported();}
 HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK,LPDICONFIGUREDEVICESPARAMSA,DWORD,LPVOID)override{return unsupported();}
};
extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE module,DWORD version,REFIID iid,void** out,LPUNKNOWN outer){
 if(!out)return E_POINTER;*out=nullptr;if(outer)return CLASS_E_NOAGGREGATION;if(!module)return DIERR_INVALIDPARAM;
 if(version!=0x0800)return version>0x0800?DIERR_OLDDIRECTINPUTVERSION:DIERR_BETADIRECTINPUTVERSION;
 if(iid!=IID_IDirectInput8A)return E_NOINTERFACE;
 try { *out=new Input;sony_prompts::install();character_lighting::install();return DI_OK; }
 catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
}

