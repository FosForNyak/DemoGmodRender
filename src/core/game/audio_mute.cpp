#include "audio_mute.hpp"

#include "../util/file_util.hpp"
#include "../util/i18n.hpp"

#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#endif

namespace gmdr::game {

namespace {
fs::path marker_path() { return app_data_dir() / "gmod_audio_muted.flag"; }

#ifdef _WIN32
template <class T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() {
        if (p) p->Release();
    }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

int mute_on_device(IMMDevice* dev, DWORD pid, bool mute) {
    ComPtr<IAudioSessionManager2> mgr;
    if (FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&mgr))))
        return 0;
    ComPtr<IAudioSessionEnumerator> list;
    if (FAILED(mgr->GetSessionEnumerator(&list))) return 0;
    int count = 0, changed = 0;
    list->GetCount(&count);
    for (int i = 0; i < count; ++i) {
        ComPtr<IAudioSessionControl> ctl;
        if (FAILED(list->GetSession(i, &ctl))) continue;
        ComPtr<IAudioSessionControl2> ctl2;
        if (FAILED(ctl->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast<void**>(&ctl2)))) continue;
        DWORD spid = 0;
        if (FAILED(ctl2->GetProcessId(&spid)) || spid != pid) continue;
        ComPtr<ISimpleAudioVolume> vol;
        if (FAILED(ctl->QueryInterface(__uuidof(ISimpleAudioVolume), reinterpret_cast<void**>(&vol)))) continue;
        if (SUCCEEDED(vol->SetMute(mute ? TRUE : FALSE, nullptr))) ++changed;
    }
    return changed;
}
#endif
} // namespace

int set_process_audio_mute(uint32_t pid, bool mute) {
#ifdef _WIN32
    // COM у поточному потоці (якщо вже ініціалізовано в іншому режимі — теж працює)
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    int changed = 0;
    {
        ComPtr<IMMDeviceEnumerator> en;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                       reinterpret_cast<void**>(&en)))) {
            ComPtr<IMMDeviceCollection> devs;
            if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devs))) {
                UINT n = 0;
                devs->GetCount(&n);
                for (UINT i = 0; i < n; ++i) {
                    ComPtr<IMMDevice> dev;
                    if (SUCCEEDED(devs->Item(i, &dev))) changed += mute_on_device(dev.p, pid, mute);
                }
            }
        }
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return changed;
#else
    (void)pid;
    (void)mute;
    return 0;
#endif
}

void mark_game_audio_muted(bool muted) {
    if (muted) write_file_text(marker_path(), tr("GMod Demo Render вимкнув звук Garry's Mod у мікшері Windows\n"), nullptr);
    else remove_file_quiet(marker_path());
}

bool game_audio_mute_pending() {
    std::error_code ec;
    return fs::exists(marker_path(), ec);
}

} // namespace gmdr::game
