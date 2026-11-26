#pragma once
#include <dinput.h>
#include "sony_raw_input.h"
#include "sony_layout.hpp"
#include "xinput_pads.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <new>

namespace sony_di {

inline GUID instance_guid(const sonyraw::Device& device) {
    const uint64_t key = device.instance_key;
    return {static_cast<DWORD>(key), static_cast<WORD>(key >> 32),
            static_cast<WORD>(key >> 48), {0x55,0x53,0x4d,0x50,0x41,0x44,0x01,0x00}};
}

inline const char* device_name(sonyraw::Family family) {
    switch (family) {
    case sonyraw::Family::DualSense: return "DualSense Wireless Controller";
    case sonyraw::Family::DualSenseEdge: return "DualSense Edge Wireless Controller";
    case sonyraw::Family::Xbox: return "Xbox Controller";
    default: return "DUALSHOCK 4 Wireless Controller";
    }
}

inline DIDEVICEINSTANCEA instance(const sonyraw::Device& device) {
    DIDEVICEINSTANCEA value = {};
    value.dwSize = sizeof(value);
    value.guidInstance = instance_guid(device);
    value.guidProduct = {static_cast<DWORD>(device.vid | (device.pid << 16)), 0, 0,
                         {0,0,'P','I','D','V','I','D'}};
    value.dwDevType = DI8DEVTYPE_GAMEPAD | (DI8DEVTYPEGAMEPAD_STANDARD << 8);
    strcpy_s(value.tszInstanceName, device_name(device.family));
    strcpy_s(value.tszProductName, value.tszInstanceName);
    value.wUsagePage = 1;
    value.wUsage = 5;
    return value;
}

class Hub {
    sonyraw::Backend backend_;
    xinput_pads::Pads xinput_;
    std::mutex mutex_;
    HANDLE ready_ = nullptr;
    HANDLE thread_ = nullptr;
    DWORD thread_id_ = 0;
    bool attached_ = false;
    ULONGLONG next_rescan_ = 0;

    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* hub = reinterpret_cast<Hub*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        // A message-only window needs INPUTSINK to receive reports while the
        // game's main window is foreground. Drop all reports from other apps.
        DWORD foreground_pid = 0;
        if (message == WM_INPUT) GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
        if (hub && (message == WM_INPUT_DEVICE_CHANGE ||
                    (message == WM_INPUT && foreground_pid == GetCurrentProcessId()))) {
            std::lock_guard<std::mutex> guard(hub->mutex_);
            try { hub->backend_.handle(message, wparam, lparam); } catch (const std::bad_alloc&) {}
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    static DWORD WINAPI run(void* context) {
        auto& hub = *static_cast<Hub*>(context);
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        const wchar_t name[] = L"USMPrivateSonyInput";
        WNDCLASSW window_class = {};
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = instance;
        window_class.lpszClassName = name;
        const ATOM atom = RegisterClassW(&window_class);
        const HWND window = atom ? CreateWindowExW(0, name, L"", 0, 0, 0, 0, 0,
                                                    HWND_MESSAGE, nullptr, instance, &hub) : nullptr;
        bool attached = false;
        if (window) {
            std::lock_guard<std::mutex> guard(hub.mutex_);
            try {
                attached = hub.backend_.attach(window, true);
                hub.attached_ = attached;
            } catch (const std::bad_alloc&) {}
        }
        SetEvent(hub.ready_);
        if (attached) {
            MSG msg = {};
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
            RAWINPUTDEVICE remove[] = {{1, 4, RIDEV_REMOVE, nullptr}, {1, 5, RIDEV_REMOVE, nullptr}};
            RegisterRawInputDevices(remove, 2, sizeof(remove[0]));
        }
        if (window) DestroyWindow(window);
        if (atom) UnregisterClassW(name, instance);
        return 0;
    }

public:
    Hub() {
        ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ready_) return;
        thread_ = CreateThread(nullptr, 0, run, this, 0, &thread_id_);
        if (thread_) WaitForSingleObject(ready_, 3000);
    }
    ~Hub() {
        if (thread_) {
            WaitForSingleObject(ready_, INFINITE);
            PostThreadMessageW(thread_id_, WM_QUIT, 0, 0);
            WaitForSingleObject(thread_, INFINITE);
            CloseHandle(thread_);
        }
        if (ready_) CloseHandle(ready_);
    }
    std::vector<sonyraw::Device> devices(bool rescan = false) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (rescan && attached_) {
            const auto now = GetTickCount64();
            if (now >= next_rescan_) {
                next_rescan_ = now + 500;
                backend_.refresh();
            }
        }
        auto list = backend_.devices();
        // XInput pads only when no Sony pad is present: tools such as
        // DS4Windows show a DualShock as both, and the Sony path is richer.
        if (list.empty()) xinput_.append(list);
        return list;
    }
    bool find(REFGUID id, sonyraw::Device& device) {
        auto available = devices();
        for (const auto& entry : available) {
            if (instance_guid(entry) == id) {
                device = entry;
                return true;
            }
        }
        return false;
    }
};

inline std::mutex hub_lock;
inline std::weak_ptr<Hub> shared_hub;

inline std::shared_ptr<Hub> acquire_hub() {
    std::lock_guard<std::mutex> guard(hub_lock);
    auto hub = shared_hub.lock();
    if (!hub) {
        hub = std::make_shared<Hub>();
        shared_hub = hub;
    }
    return hub;
}

struct AxisRange { LONG minimum = -1000; LONG maximum = 1000; };

inline void remove_stick_dead_zone(float& x, float& y) {
    const float radius = std::sqrt(x * x + y * y);
    constexpr float dead_zone = 0.15f;
    if (radius <= dead_zone) { x = 0; y = 0; return; }
    const float scale = (std::min(radius, 1.0f) - dead_zone) / ((1.0f - dead_zone) * radius);
    x *= scale;
    y *= scale;
}

inline LONG map_axis(float value, AxisRange range) {
    const double fraction = (static_cast<double>(value) + 1.0) / 2.0;
    return static_cast<LONG>(std::lround(range.minimum + fraction * (double(range.maximum) - range.minimum)));
}

inline DIJOYSTATE2 state(const sonyraw::State& source, const std::array<AxisRange, 6>& ranges) {
    DIJOYSTATE2 result = {};
    for (DWORD& pov : result.rgdwPOV) pov = 0xffffffff;
    if (!source.valid) return result;
    float left_x = source.lx, left_y = source.ly;
    float right_x = source.rx, right_y = source.ry;
    remove_stick_dead_zone(left_x, left_y);
    remove_stick_dead_zone(right_x, right_y);
    if (left_x == 0 && left_y == 0 && source.hat >= 0 && source.hat < 8) {
        constexpr float d = .70710678f;
        constexpr float hat_x[] = {0, d, 1, d, 0, -d, -1, -d};
        constexpr float hat_y[] = {1, d, 0, -d, -1, -d, 0, d};
        left_x = hat_x[source.hat];
        left_y = hat_y[source.hat];
    }
    result.lX = map_axis(left_x, ranges[0]);
    result.lY = map_axis(-left_y, ranges[1]);
    result.lZ = map_axis(source.l2, ranges[2]);
    result.lRx = map_axis(right_x, ranges[3]);
    result.lRy = map_axis(-right_y, ranges[4]);
    result.lRz = map_axis(source.r2, ranges[5]);
    if (source.hat >= 0 && source.hat < 8) result.rgdwPOV[0] = static_cast<DWORD>(source.hat * 4500);
    for (unsigned button = 0; button < sony_layout::buttons.size(); ++button)
        result.rgbButtons[button] = (source.buttons & (1u << sony_layout::buttons[button].hid_bit)) ? 0x80 : 0;
    // A trigger can report an analog value before its separate HID button.
    // Represent both triggers independently, including simultaneous presses.
    constexpr float trigger_press = .20f;
    if (source.l2 >= trigger_press) result.rgbButtons[sony_layout::left_trigger - 21] = 0x80;
    if (source.r2 >= trigger_press) result.rgbButtons[sony_layout::right_trigger - 21] = 0x80;
    return result;
}

}
