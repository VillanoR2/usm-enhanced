#pragma once

// Extends only the SDK methods needed by the game's joystick path. The
// unsupported methods and COM lifetime are inherited from the input adapter.
class SonyDevice final : public Device {
    static constexpr unsigned axis_count = 6;
    struct AxisObject { const GUID* guid; DWORD offset; const char* label; };
    static inline const AxisObject axes_[axis_count] = {
        {&GUID_XAxis, DIJOFS_X, "Left stick X"},
        {&GUID_YAxis, DIJOFS_Y, "Left stick Y"},
        {&GUID_ZAxis, DIJOFS_Z, "L2"},
        {&GUID_RxAxis, DIJOFS_RX, "Right stick X"},
        {&GUID_RyAxis, DIJOFS_RY, "Right stick Y"},
        {&GUID_RzAxis, DIJOFS_RZ, "R2"},
    };

    std::shared_ptr<sony_di::Hub> hub_;
    sonyraw::Device details_;
    std::array<sony_di::AxisRange, axis_count> ranges_ = {};
    bool format_ready_ = false;
    bool acquired_ = false;
    HWND focus_ = nullptr;

    HRESULT active(sonyraw::Device& device) {
        if (!acquired_) return DIERR_NOTACQUIRED;
        if (!own_foreground(focus_) || !hub_->find(sony_di::instance_guid(details_), device)) {
            acquired_ = false;
            return DIERR_INPUTLOST;
        }
        return DI_OK;
    }

    static DWORD axis_type(unsigned axis) { return DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(axis); }

public:
    SonyDevice(std::shared_ptr<sony_di::Hub> hub, const sonyraw::Device& device)
        : Device(true), hub_(std::move(hub)), details_(device) {}

    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS caps) override {
        if (!caps || caps->dwSize != sizeof(*caps)) return DIERR_INVALIDPARAM;
        *caps = {};
        caps->dwSize = sizeof(*caps);
        caps->dwFlags = DIDC_ATTACHED | DIDC_EMULATED;
        caps->dwDevType = sony_di::instance(details_).dwDevType;
        caps->dwAxes = axis_count;
        caps->dwButtons = static_cast<DWORD>(sony_layout::buttons.size());
        caps->dwPOVs = 1;
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEA info) override {
        if (!info || info->dwSize != sizeof(*info)) return DIERR_INVALIDPARAM;
        *info = sony_di::instance(details_);
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA callback,
                                           LPVOID context, DWORD flags) override {
        if (!callback) return DIERR_INVALIDPARAM;
        const auto emit = [&](const GUID& guid, DWORD offset, DWORD type, const char* name) {
            DIDEVICEOBJECTINSTANCEA object = {};
            object.dwSize = sizeof(object);
            object.guidType = guid;
            object.dwOfs = offset;
            object.dwType = type;
            strcpy_s(object.tszName, name);
            return callback(&object, context) != DIENUM_STOP;
        };
        if (flags == DIDFT_ALL || (flags & DIDFT_AXIS)) {
            for (unsigned axis = 0; axis < axis_count; ++axis)
                if (!emit(*axes_[axis].guid, axes_[axis].offset, axis_type(axis), axes_[axis].label))
                    return DI_OK;
        }
        if (flags == DIDFT_ALL || (flags & DIDFT_POV))
            if (!emit(GUID_POV, DIJOFS_POV(0), DIDFT_POV, "Directional pad")) return DI_OK;
        if (flags == DIDFT_ALL || (flags & DIDFT_BUTTON)) {
            for (unsigned button = 0; button < sony_layout::buttons.size(); ++button)
                if (!emit(GUID_Button, DIJOFS_BUTTON(button), DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(button),
                          sony_layout::buttons[button].name))
                    return DI_OK;
        }
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT format) override {
        if (acquired_) return DIERR_ACQUIRED;
        if (!format || format->dwSize != sizeof(*format) || format->dwObjSize != sizeof(DIOBJECTDATAFORMAT)
            || !format->rgodf) return DIERR_INVALIDPARAM;
        if (format->dwFlags != DIDF_ABSAXIS || format->dwDataSize != sizeof(DIJOYSTATE2)
            || format->dwNumObjs != 164) return DIERR_UNSUPPORTED;
        for (unsigned axis = 0; axis < axis_count; ++axis) {
            const auto& row = format->rgodf[axis];
            if (!row.pguid || *row.pguid != *axes_[axis].guid || row.dwOfs != axes_[axis].offset
                || (DIDFT_GETTYPE(row.dwType) & DIDFT_AXIS) == 0) return DIERR_UNSUPPORTED;
        }
        if (format->rgodf[8].dwOfs != DIJOFS_POV(0)) return DIERR_UNSUPPORTED;
        for (unsigned button = 0; button < sony_layout::buttons.size(); ++button) {
            const auto& row = format->rgodf[12 + button];
            if (row.dwOfs != DIJOFS_BUTTON(button) || (DIDFT_GETTYPE(row.dwType) & DIDFT_BUTTON) == 0)
                return DIERR_UNSUPPORTED;
        }
        format_ready_ = true;
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID property, LPCDIPROPHEADER header) override {
        if (reinterpret_cast<ULONG_PTR>(&property) != reinterpret_cast<ULONG_PTR>(&DIPROP_RANGE))
            return DIERR_UNSUPPORTED;
        if (!header || header->dwSize != sizeof(DIPROPRANGE) || header->dwHeaderSize != sizeof(DIPROPHEADER)
            || header->dwHow != DIPH_BYID) return DIERR_INVALIDPARAM;
        const auto* range = reinterpret_cast<const DIPROPRANGE*>(header);
        if (range->lMin >= range->lMax) return DIERR_INVALIDPARAM;
        for (unsigned axis = 0; axis < axis_count; ++axis) {
            if (header->dwObj == axis_type(axis)) {
                ranges_[axis] = {range->lMin, range->lMax};
                return DI_OK;
            }
        }
        return DIERR_OBJECTNOTFOUND;
    }

    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND focus, DWORD flags) override {
        DWORD owner = 0;
        if (!focus || !GetWindowThreadProcessId(focus, &owner) || owner != GetCurrentProcessId()
            || GetAncestor(focus, GA_ROOT) != focus) return DIERR_INVALIDPARAM;
        if (acquired_) return DIERR_ACQUIRED;
        // This backend guarantees foreground-only delivery. Device exclusivity
        // across processes is not available through the Raw Input registration.
        if (flags != (DISCL_FOREGROUND | DISCL_NONEXCLUSIVE)) return DIERR_UNSUPPORTED;
        focus_ = focus;
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE Acquire() override {
        if (!format_ready_) return DIERR_INVALIDPARAM;
        if (!own_foreground(focus_)) return DIERR_OTHERAPPHASPRIO;
        try {
            sonyraw::Device device;
            if (!hub_->find(sony_di::instance_guid(details_), device)) return DIERR_INPUTLOST;
            if (acquired_) return S_FALSE;
            acquired_ = true;
            return DI_OK;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    }
    HRESULT STDMETHODCALLTYPE Unacquire() override { acquired_ = false; return DI_OK; }
    HRESULT STDMETHODCALLTYPE Poll() override {
        try { sonyraw::Device device; return active(device); }
        catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    }
    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD bytes, LPVOID buffer) override {
        if (!buffer || bytes != sizeof(DIJOYSTATE2)) return DIERR_INVALIDPARAM;
        try {
            sonyraw::Device device;
            const HRESULT result = active(device);
            if (FAILED(result)) return result;
            const auto state = sony_di::state(device.state, ranges_);
            if (!own_foreground(focus_)) { acquired_ = false; return DIERR_INPUTLOST; }
            if (device.state.valid && GetWindowThreadProcessId(GetForegroundWindow(), nullptr) == GetCurrentThreadId())
                sony_prompts::apply_controller_defaults();
            memcpy(buffer, &state, sizeof(state));
            return DI_OK;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    }
};
