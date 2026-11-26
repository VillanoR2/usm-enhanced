#include "sony_raw_input.h"
#include <hidsdi.h>
#include <hidpi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <algorithm>
#include <string>
#include <cstring>
#include <cwctype>
#include <new>
namespace sonyraw {
Family classify(unsigned v,unsigned p) { if(v!=0x054c)return Family::Unsupported; switch(p){case 0x05c4:case 0x09cc:return Family::DualShock4;case 0x0ce6:return Family::DualSense;case 0x0df2:return Family::DualSenseEdge;default:return Family::Unsupported;} }
static float unit(const Axis&a){ if(!a.present||a.max<=a.min)return 0;return std::clamp(float(double(a.value)-a.min)/float(double(a.max)-a.min),0.f,1.f); }
static float stick(const Axis&a){ if(!a.present||a.max<=a.min)return 0;const double center=double(a.min)+(double(a.max)-a.min+1)/2;return float(std::clamp((double(a.value)-center)/(a.value>=center?double(a.max)-center:center-a.min),-1.,1.)); }
State decode_sample(const Sample&s){State o;o.valid=s.x.present&&s.y.present&&s.z.present&&s.rz.present&&s.rx.present&&s.ry.present&&s.buttons_present; o.lx=stick(s.x);o.ly=-stick(s.y);o.rx=stick(s.z);o.ry=-stick(s.rz);o.l2=unit(s.rx);o.r2=unit(s.ry);o.buttons=s.buttons&0x3fff;o.hat=s.hat>=0&&s.hat<8?s.hat:-1;return o;}
static Transport transport(const std::wstring&path){
 wchar_t id[MAX_DEVICE_ID_LEN]={};DEVPROPTYPE type=0;ULONG size=sizeof(id);
 if(CM_Get_Device_Interface_PropertyW(path.c_str(),&DEVPKEY_Device_InstanceId,&type,reinterpret_cast<PBYTE>(id),&size,0)!=CR_SUCCESS||type!=DEVPROP_TYPE_STRING)return Transport::Unknown;
 DEVINST node=0;if(CM_Locate_DevNodeW(&node,id,CM_LOCATE_DEVNODE_NORMAL)!=CR_SUCCESS)return Transport::Unknown;
 for(int depth=0;depth<16;++depth){wchar_t name[256]={};size=sizeof(name);if(CM_Get_DevNode_PropertyW(node,&DEVPKEY_Device_EnumeratorName,&type,reinterpret_cast<PBYTE>(name),&size,0)==CR_SUCCESS&&type==DEVPROP_TYPE_STRING){if(!_wcsicmp(name,L"BTHENUM")||!_wcsicmp(name,L"BTHLEDEVICE"))return Transport::Bluetooth;if(!_wcsicmp(name,L"USB"))return Transport::USB;}DEVINST parent=0;if(CM_Get_Parent(&parent,node,0)!=CR_SUCCESS)break;node=parent;}return Transport::Unknown;
}
struct Backend::Impl {
 struct Entry {Device d;std::vector<DWORD> pp;std::vector<HIDP_VALUE_CAPS> vc;std::vector<HIDP_BUTTON_CAPS> bc;};
 std::vector<Entry> entries;
 void add(HANDLE h){for(const auto&e:entries)if(e.d.handle==h)return;RID_DEVICE_INFO info={};info.cbSize=sizeof(info);UINT n=sizeof(info);if(GetRawInputDeviceInfoW(h,RIDI_DEVICEINFO,&info,&n)==UINT(-1)||info.dwType!=RIM_TYPEHID||info.hid.usUsagePage!=1||(info.hid.usUsage!=4&&info.hid.usUsage!=5))return;
 Entry e;e.d.handle=h;e.d.instance_key=uint64_t(reinterpret_cast<uintptr_t>(h));e.d.vid=info.hid.dwVendorId;e.d.pid=info.hid.dwProductId;e.d.family=classify(e.d.vid,e.d.pid);if(e.d.family==Family::Unsupported)return;
 n=0;if(GetRawInputDeviceInfoW(h,RIDI_PREPARSEDDATA,nullptr,&n)==UINT(-1)||n==0||n>1024*1024)return;e.pp.resize((n+3)/4);if(GetRawInputDeviceInfoW(h,RIDI_PREPARSEDDATA,e.pp.data(),&n)==UINT(-1))return;auto p=reinterpret_cast<PHIDP_PREPARSED_DATA>(e.pp.data());HIDP_CAPS caps={};if(HidP_GetCaps(p,&caps)!=HIDP_STATUS_SUCCESS)return;
 USHORT count=caps.NumberInputValueCaps;e.vc.resize(count);if(count&&HidP_GetValueCaps(HidP_Input,e.vc.data(),&count,p)!=HIDP_STATUS_SUCCESS)return;e.vc.resize(count);count=caps.NumberInputButtonCaps;e.bc.resize(count);if(count&&HidP_GetButtonCaps(HidP_Input,e.bc.data(),&count,p)!=HIDP_STATUS_SUCCESS)return;e.bc.resize(count);
 n=0;if(GetRawInputDeviceInfoW(h,RIDI_DEVICENAME,nullptr,&n)!=UINT(-1)&&n&&n<32768){std::vector<wchar_t> path(n+1);if(GetRawInputDeviceInfoW(h,RIDI_DEVICENAME,path.data(),&n)!=UINT(-1)){e.d.transport=transport(path.data());uint64_t hash=14695981039346656037ull;for(size_t i=0;path[i];++i){const unsigned c=unsigned(towlower(path[i]));hash^=c&255;hash*=1099511628211ull;hash^=(c>>8)&255;hash*=1099511628211ull;}e.d.instance_key=hash;e.d.instance_key_stable=true;}}entries.push_back(std::move(e)); }
 void report(Entry&e,char*data,ULONG length){auto p=reinterpret_cast<PHIDP_PREPARSED_DATA>(e.pp.data());Sample s;
 for(const auto&c:e.vc){if(c.UsagePage!=1||c.BitSize==0||c.BitSize>32)continue;USAGE lo=c.IsRange?c.Range.UsageMin:c.NotRange.Usage;USAGE hi=c.IsRange?c.Range.UsageMax:lo;for(unsigned u=lo;u<=hi&&u<=0x39;++u){ULONG value=0;if(HidP_GetUsageValue(HidP_Input,1,c.LinkCollection,USAGE(u),&value,p,data,length)!=HIDP_STATUS_SUCCESS)continue;long v=long(value);if(c.LogicalMin<0&&c.BitSize<32&&(value&(1u<<(c.BitSize-1))))v=long(value|(~0u<<c.BitSize));Axis a{v,c.LogicalMin,c.LogicalMax,true};switch(u){case 0x30:s.x=a;break;case 0x31:s.y=a;break;case 0x32:s.z=a;break;case 0x33:s.rx=a;break;case 0x34:s.ry=a;break;case 0x35:s.rz=a;break;case 0x39:s.hat=v>=c.LogicalMin&&v<=c.LogicalMax?int(v-c.LogicalMin):-1;break;}}}
 for(const auto&c:e.bc){if(c.UsagePage!=9)continue;ULONG count=HidP_MaxUsageListLength(HidP_Input,9,p);if(!count||count>4096)continue;std::vector<USAGE> usages(count);if(HidP_GetUsages(HidP_Input,9,c.LinkCollection,usages.data(),&count,p,data,length)!=HIDP_STATUS_SUCCESS)continue;s.buttons_present=true;for(ULONG i=0;i<count;++i)if(usages[i]>=1&&usages[i]<=14)s.buttons|=1u<<(usages[i]-1);}
 State out=decode_sample(s);if(out.valid){out.sequence=e.d.state.sequence+1;e.d.state=out;}else{++e.d.rejected_reports;const auto sequence=e.d.state.sequence;e.d.state=State{};e.d.state.sequence=sequence;}}
};
Backend::Backend():impl(new Impl){} Backend::~Backend()=default;
bool Backend::attach(HWND w, bool background) {
 if (!IsWindow(w)) return false;
 const DWORD flags = RIDEV_DEVNOTIFY | (background ? RIDEV_INPUTSINK : 0);
 RAWINPUTDEVICE registrations[] = {{1, 4, flags, w}, {1, 5, flags, w}};
 if (!RegisterRawInputDevices(registrations, 2, sizeof(registrations[0]))) return false;
 // Device discovery can race a USB connection. Keep receiving notifications
 // even if the first enumeration fails.
 try { refresh(); } catch (const std::bad_alloc&) {}
 return true;
}

bool Backend::refresh() {
 for (unsigned attempt = 0; attempt < 3; ++attempt) {
  UINT count = 0;
  if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) == UINT(-1)) return false;
  std::vector<RAWINPUTDEVICELIST> devices(count);
  if (count) {
   const UINT received = GetRawInputDeviceList(devices.data(), &count, sizeof(RAWINPUTDEVICELIST));
   if (received == UINT(-1)) {
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) continue;
    return false;
   }
   devices.resize(received);
  }
  auto& entries = impl->entries;
  entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Impl::Entry& entry) {
   return std::none_of(devices.begin(), devices.end(), [&](const RAWINPUTDEVICELIST& present) {
    return entry.d.handle == present.hDevice;
   });
  }), entries.end());
  for (const auto& device : devices)
   if (device.dwType == RIM_TYPEHID) impl->add(device.hDevice);
  return true;
 }
 return false;
}
bool Backend::handle(UINT m,WPARAM w,LPARAM l){if(m==WM_INPUT_DEVICE_CHANGE){if(w==GIDC_REMOVAL){auto&e=impl->entries;e.erase(std::remove_if(e.begin(),e.end(),[&](const Impl::Entry&x){return x.d.handle==reinterpret_cast<HANDLE>(l);}),e.end());}else if(w==GIDC_ARRIVAL)impl->add(reinterpret_cast<HANDLE>(l));return true;}if(m!=WM_INPUT)return false;UINT n=0;if(GetRawInputData(reinterpret_cast<HRAWINPUT>(l),RID_INPUT,nullptr,&n,sizeof(RAWINPUTHEADER))==UINT(-1)||n<sizeof(RAWINPUTHEADER)||n>1024*1024)return false;std::vector<DWORD> bytes((n+3)/4);if(GetRawInputData(reinterpret_cast<HRAWINPUT>(l),RID_INPUT,bytes.data(),&n,sizeof(RAWINPUTHEADER))!=n)return false;auto*r=reinterpret_cast<RAWINPUT*>(bytes.data());if(r->header.dwType!=RIM_TYPEHID)return false;const size_t offset=offsetof(RAWINPUT,data.hid.bRawData);if(n<offset||!r->data.hid.dwSizeHid||r->data.hid.dwCount>(n-offset)/r->data.hid.dwSizeHid)return false;impl->add(r->header.hDevice);for(auto&e:impl->entries)if(e.d.handle==r->header.hDevice){for(DWORD i=0;i<r->data.hid.dwCount;++i)impl->report(e,reinterpret_cast<char*>(r->data.hid.bRawData)+size_t(i)*r->data.hid.dwSizeHid,r->data.hid.dwSizeHid);return true;}return false;}
std::vector<Device> Backend::devices() const {std::vector<Device> out;for(const auto&e:impl->entries)out.push_back(e.d);return out;}
}



