#pragma once

namespace same_draw {

inline IDirect3DSurface9* surfaces[3] = {};
inline bool clear = true;

inline bool prepare(IDirect3DDevice9* device, IDirect3DSurface9* original, unsigned count) {
    D3DSURFACE_DESC d = {};
    if (FAILED(original->GetDesc(&d)) || d.MultiSampleType != D3DMULTISAMPLE_NONE) return false;
    for (unsigned i = 0; i < count; ++i) {
        auto*& surface = surfaces[i];
        if (surface) {
            D3DSURFACE_DESC old = {};
            if (SUCCEEDED(surface->GetDesc(&old)) &&
                old.Width == d.Width && old.Height == d.Height && old.Format == d.Format) continue;
            surface->Release();
            surface = nullptr;
        }
        if (FAILED(device->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0,
                                               FALSE, &surface, nullptr))) return false;
    }
    return true;
}

template<class Call>
bool replay(IDirect3DDevice9* d, IDirect3DBaseTexture9* a, IDirect3DBaseTexture9* b, Call original,
            IDirect3DBaseTexture9* c = nullptr) {
    IDirect3DSurface9* rt = nullptr;
    if (!b || FAILED(d->GetRenderTarget(0, &rt)) || !rt) return false;
    DWORD depth = 0;
    DWORD stencil = 0;
    D3DVIEWPORT9 viewport = {};
    RECT scissor = {};
    const unsigned count = c ? 3 : 2;
    const bool ready = prepare(d, rt, count) &&
                       SUCCEEDED(d->GetRenderState(D3DRS_ZENABLE, &depth)) &&
                       SUCCEEDED(d->GetRenderState(D3DRS_STENCILENABLE, &stencil)) &&
                       SUCCEEDED(d->GetViewport(&viewport)) &&
                       SUCCEEDED(d->GetScissorRect(&scissor));
    if (!ready) { rt->Release(); return false; }
    bool ok = SUCCEEDED(d->SetRenderState(D3DRS_ZENABLE, FALSE));
    ok &= SUCCEEDED(d->SetRenderState(D3DRS_STENCILENABLE, FALSE));
    IDirect3DBaseTexture9* const textures[] = {a,b,c};
    for (unsigned i = 0; ok && i < count; ++i) {
        ok = SUCCEEDED(d->SetRenderTarget(0, surfaces[i]));
        if (ok && clear) ok = SUCCEEDED(d->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff000000, 1.f, 0));
        if (ok) ok = SUCCEEDED(d->SetViewport(&viewport)) && SUCCEEDED(d->SetScissorRect(&scissor));
        if (ok) ok = SUCCEEDED(d->SetTexture(1, textures[i])) && SUCCEEDED(original());
    }
    clear = false;
    ok &= SUCCEEDED(d->SetTexture(1, a));
    ok &= SUCCEEDED(d->SetRenderTarget(0, rt));
    ok &= SUCCEEDED(d->SetViewport(&viewport));
    ok &= SUCCEEDED(d->SetScissorRect(&scissor));
    ok &= SUCCEEDED(d->SetRenderState(D3DRS_ZENABLE, depth));
    ok &= SUCCEEDED(d->SetRenderState(D3DRS_STENCILENABLE, stencil));
    rt->Release();
    return ok;
}

}
