#include "AudioInputManager.h"
#include "../utils/Logger.h"

#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmreg.h>
#include <propidl.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "ole32.lib")

namespace VirtualOverlay {

// =============================================================================
// AudioResampler
// =============================================================================

void AudioResampler::Reset() {
    m_srcPos = 0.0;
    m_prevSample = 0.0f;
    m_hasPrev = false;
}

static float ReadSampleAsFloat(const uint8_t* ptr, AudioSampleFormat fmt) {
    switch (fmt) {
        case AudioSampleFormat::Float32: {
            float v = 0.0f;
            memcpy(&v, ptr, sizeof(float));
            return std::clamp(v, -1.0f, 1.0f);
        }
        case AudioSampleFormat::Pcm16: {
            int16_t v = 0;
            memcpy(&v, ptr, sizeof(int16_t));
            return std::clamp(static_cast<float>(v) / 32768.0f, -1.0f, 1.0f);
        }
        case AudioSampleFormat::Pcm24: {
            int32_t v = (static_cast<int32_t>(ptr[0]) << 8) |
                        (static_cast<int32_t>(ptr[1]) << 16) |
                        (static_cast<int32_t>(static_cast<int8_t>(ptr[2])) << 24);
            return std::clamp(static_cast<float>(v) / 2147483648.0f, -1.0f, 1.0f);
        }
        case AudioSampleFormat::Pcm32: {
            int32_t v = 0;
            memcpy(&v, ptr, sizeof(int32_t));
            return std::clamp(static_cast<float>(v) / 2147483648.0f, -1.0f, 1.0f);
        }
    }
    return 0.0f;
}

static size_t BytesPerSample(AudioSampleFormat fmt) {
    switch (fmt) {
        case AudioSampleFormat::Float32: return 4;
        case AudioSampleFormat::Pcm16:   return 2;
        case AudioSampleFormat::Pcm24:   return 3;
        case AudioSampleFormat::Pcm32:   return 4;
    }
    return 2;
}

void AudioResampler::ConvertToMonoFloat(
    const uint8_t* rawBytes,
    size_t frameCount,
    int channels,
    AudioSampleFormat format,
    int selectedChannel,
    std::vector<float>& outMonoFloat,
    float& outPeakLevel
) {
    outMonoFloat.resize(frameCount);
    outPeakLevel = 0.0f;

    if (!rawBytes || frameCount == 0 || channels <= 0) {
        return;
    }

    const size_t bps = BytesPerSample(format);
    const size_t frameStride = bps * static_cast<size_t>(channels);
    const int targetCh = (selectedChannel >= 1 && selectedChannel <= channels) ? (selectedChannel - 1) : -1;

    float peak = 0.0f;
    for (size_t f = 0; f < frameCount; ++f) {
        const uint8_t* framePtr = rawBytes + f * frameStride;
        float mono = 0.0f;
        if (targetCh >= 0) {
            mono = ReadSampleAsFloat(framePtr + static_cast<size_t>(targetCh) * bps, format);
        } else {
            float sum = 0.0f;
            for (int c = 0; c < channels; ++c) {
                sum += ReadSampleAsFloat(framePtr + static_cast<size_t>(c) * bps, format);
            }
            mono = sum / static_cast<float>(channels);
        }
        outMonoFloat[f] = mono;
        float absVal = std::fabs(mono);
        if (absVal > peak) {
            peak = absVal;
        }
    }

    outPeakLevel = std::clamp(peak, 0.0f, 1.0f);
}

void AudioResampler::ResampleTo16kPcm16(
    const float* monoSamples,
    size_t sampleCount,
    int sourceSampleRate,
    std::vector<int16_t>& outPcm16
) {
    outPcm16.clear();
    if (!monoSamples || sampleCount == 0 || sourceSampleRate <= 0) {
        return;
    }

    if (sourceSampleRate == kTargetSampleRate) {
        outPcm16.resize(sampleCount);
        for (size_t i = 0; i < sampleCount; ++i) {
            float clamped = std::clamp(monoSamples[i], -1.0f, 1.0f);
            outPcm16[i] = static_cast<int16_t>(clamped * 32767.0f);
        }
        m_prevSample = monoSamples[sampleCount - 1];
        m_hasPrev = true;
        return;
    }

    const double step = static_cast<double>(sourceSampleRate) / static_cast<double>(kTargetSampleRate);
    size_t estimatedOut = static_cast<size_t>(static_cast<double>(sampleCount) / step) + 2;
    outPcm16.reserve(estimatedOut);

    if (!m_hasPrev) {
        m_prevSample = monoSamples[0];
        m_hasPrev = true;
    }

    while (m_srcPos < static_cast<double>(sampleCount)) {
        int idx0 = static_cast<int>(std::floor(m_srcPos));
        double frac = m_srcPos - static_cast<double>(idx0);

        float s0 = (idx0 < 0) ? m_prevSample : monoSamples[static_cast<size_t>(idx0)];
        float s1 = (idx0 + 1 < static_cast<int>(sampleCount))
            ? monoSamples[static_cast<size_t>(idx0 + 1)]
            : monoSamples[sampleCount - 1];

        // Anti-aliased box/linear interpolation when downsampling (e.g. 48k -> 16k)
        float interp = static_cast<float>((1.0 - frac) * s0 + frac * s1);
        if (step >= 2.0 && idx0 >= 0 && (idx0 + 2) < static_cast<int>(sampleCount)) {
            float s2 = monoSamples[static_cast<size_t>(idx0 + 2)];
            interp = 0.25f * s0 + 0.5f * s1 + 0.25f * s2;
        }

        float clamped = std::clamp(interp, -1.0f, 1.0f);
        outPcm16.push_back(static_cast<int16_t>(clamped * 32767.0f));

        m_srcPos += step;
    }

    m_srcPos -= static_cast<double>(sampleCount);
    m_prevSample = monoSamples[sampleCount - 1];
}

// =============================================================================
// IMMNotificationClient implementation
// =============================================================================

class AudioInputManager::DeviceNotificationClient : public IMMNotificationClient {
public:
    explicit DeviceNotificationClient(AudioInputManager* owner)
        : m_refCount(1), m_owner(owner) {}

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&m_refCount);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG ref = InterlockedDecrement(&m_refCount);
        if (ref == 0) {
            delete this;
        }
        return ref;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, VOID** ppvInterface) override {
        if (!ppvInterface) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(IMMNotificationClient)) {
            *ppvInterface = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *ppvInterface = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD) override {
        if (m_owner) m_owner->OnDeviceNotification(pwstrDeviceId, false);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) override {
        if (m_owner) m_owner->OnDeviceNotification(pwstrDeviceId, false);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) override {
        if (m_owner) m_owner->OnDeviceNotification(pwstrDeviceId, false);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override {
        if (flow == eCapture && (role == eCommunications || role == eConsole) && m_owner) {
            m_owner->OnDeviceNotification(pwstrDefaultDeviceId, true);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override {
        return S_OK;
    }

private:
    LONG m_refCount;
    AudioInputManager* m_owner;
};

// =============================================================================
// Helpers to inspect MMDevice
// =============================================================================

static std::wstring QueryDeviceFriendlyName(IMMDevice* device) {
    if (!device) return L"Unknown Microphone";

    Microsoft::WRL::ComPtr<IPropertyStore> props;
    if (FAILED(device->OpenPropertyStore(STGM_READ, props.GetAddressOf())) || !props) {
        return L"Microphone";
    }

    PROPVARIANT varName;
    PropVariantInit(&varName);
    std::wstring result = L"Microphone";
    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &varName))) {
        if (varName.vt == VT_LPWSTR && varName.pwszVal) {
            result = varName.pwszVal;
        }
    }
    PropVariantClear(&varName);
    return result;
}

static void QueryDeviceFormatInfo(IMMDevice* device, int& outChannels, int& outSampleRate) {
    outChannels = 1;
    outSampleRate = 48000;
    if (!device) return;

    Microsoft::WRL::ComPtr<IAudioClient> audioClient;
    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(audioClient.GetAddressOf()))) || !audioClient) {
        return;
    }

    WAVEFORMATEX* pwfx = nullptr;
    if (SUCCEEDED(audioClient->GetMixFormat(&pwfx)) && pwfx) {
        outChannels = std::max(1, static_cast<int>(pwfx->nChannels));
        outSampleRate = static_cast<int>(pwfx->nSamplesPerSec);
        CoTaskMemFree(pwfx);
    }
}

static bool ParseWaveFormat(const WAVEFORMATEX* pwfx, AudioSampleFormat& outFormat) {
    if (!pwfx) return false;

    if (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && pwfx->wBitsPerSample == 32) {
        outFormat = AudioSampleFormat::Float32;
        return true;
    }
    if (pwfx->wFormatTag == WAVE_FORMAT_PCM) {
        if (pwfx->wBitsPerSample == 16) { outFormat = AudioSampleFormat::Pcm16; return true; }
        if (pwfx->wBitsPerSample == 24) { outFormat = AudioSampleFormat::Pcm24; return true; }
        if (pwfx->wBitsPerSample == 32) { outFormat = AudioSampleFormat::Pcm32; return true; }
    }
    if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && pwfx->cbSize >= 22) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(pwfx);
        if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) && pwfx->wBitsPerSample == 32) {
            outFormat = AudioSampleFormat::Float32;
            return true;
        }
        if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
            if (pwfx->wBitsPerSample == 16) { outFormat = AudioSampleFormat::Pcm16; return true; }
            if (pwfx->wBitsPerSample == 24) { outFormat = AudioSampleFormat::Pcm24; return true; }
            if (pwfx->wBitsPerSample == 32) { outFormat = AudioSampleFormat::Pcm32; return true; }
        }
    }
    return false;
}

// =============================================================================
// AudioInputManager
// =============================================================================

AudioInputManager& AudioInputManager::Instance() {
    static AudioInputManager instance;
    return instance;
}

AudioInputManager::AudioInputManager() = default;

AudioInputManager::~AudioInputManager() {
    Shutdown();
}

bool AudioInputManager::Init() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        return true;
    }

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(m_enumerator.GetAddressOf())
    );

    if (FAILED(hr) || !m_enumerator) {
        LOG_ERROR("AudioInputManager: Failed to create IMMDeviceEnumerator (0x%08X)", hr);
        return false;
    }

    m_notifClient.Attach(new DeviceNotificationClient(this));
    m_enumerator->RegisterEndpointNotificationCallback(m_notifClient.Get());

    m_wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_initialized = true;
    LOG_INFO("AudioInputManager initialized");
    return true;
}

void AudioInputManager::Shutdown() {
    StopCapture();

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
        return;
    }

    if (m_enumerator && m_notifClient) {
        m_enumerator->UnregisterEndpointNotificationCallback(m_notifClient.Get());
    }
    m_notifClient.Reset();
    m_enumerator.Reset();

    if (m_wakeEvent) {
        CloseHandle(m_wakeEvent);
        m_wakeEvent = nullptr;
    }

    m_initialized = false;
    LOG_INFO("AudioInputManager shutdown");
}

std::vector<AudioEndpointInfo> AudioInputManager::EnumerateCaptureDevices() const {
    std::vector<AudioEndpointInfo> list;

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(enumerator.GetAddressOf())
    );
    if (FAILED(hr) || !enumerator) {
        AudioEndpointInfo defEntry;
        defEntry.id = L"default";
        defEntry.friendlyName = L"System Default Microphone";
        defEntry.isDefault = true;
        defEntry.isAvailable = false;
        list.push_back(std::move(defEntry));
        return list;
    }

    // Find current default capture endpoint ID
    std::wstring defaultId;
    std::wstring defaultName = L"System Default Microphone";
    int defaultChannels = 1;
    int defaultRate = 48000;
    bool hasDefault = false;

    Microsoft::WRL::ComPtr<IMMDevice> defDevice;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, defDevice.GetAddressOf())) ||
        SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, defDevice.GetAddressOf()))) {
        LPWSTR pwszId = nullptr;
        if (SUCCEEDED(defDevice->GetId(&pwszId)) && pwszId) {
            defaultId = pwszId;
            CoTaskMemFree(pwszId);
        }
        std::wstring actualName = QueryDeviceFriendlyName(defDevice.Get());
        defaultName = L"System Default (" + actualName + L")";
        QueryDeviceFormatInfo(defDevice.Get(), defaultChannels, defaultRate);
        hasDefault = true;
    }

    AudioEndpointInfo defEntry;
    defEntry.id = L"default";
    defEntry.friendlyName = defaultName;
    defEntry.channels = defaultChannels;
    defEntry.sampleRate = defaultRate;
    defEntry.isDefault = true;
    defEntry.isAvailable = hasDefault;
    list.push_back(std::move(defEntry));

    Microsoft::WRL::ComPtr<IMMDeviceCollection> collection;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, collection.GetAddressOf())) && collection) {
        UINT count = 0;
        collection->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            Microsoft::WRL::ComPtr<IMMDevice> dev;
            if (FAILED(collection->Item(i, dev.GetAddressOf())) || !dev) {
                continue;
            }

            LPWSTR pwszId = nullptr;
            if (FAILED(dev->GetId(&pwszId)) || !pwszId) {
                continue;
            }
            std::wstring id(pwszId);
            CoTaskMemFree(pwszId);

            AudioEndpointInfo info;
            info.id = id;
            info.friendlyName = QueryDeviceFriendlyName(dev.Get());
            QueryDeviceFormatInfo(dev.Get(), info.channels, info.sampleRate);
            info.isDefault = (!defaultId.empty() && id == defaultId);
            info.isAvailable = true;
            list.push_back(std::move(info));
        }
    }

    return list;
}

void AudioInputManager::SetSelectedDevice(const std::wstring& deviceId, const std::wstring& friendlyNameHint, int channel) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::wstring newId = deviceId.empty() ? L"default" : deviceId;
        if (m_selectedDeviceId != newId || m_selectedChannel != channel) {
            changed = true;
        }
        m_selectedDeviceId = newId;
        if (!friendlyNameHint.empty()) {
            m_selectedDeviceName = friendlyNameHint;
        }
        m_selectedChannel = std::max(0, channel);
    }

    if (changed && m_wantCapture.load()) {
        m_reopenRequested.store(true);
        if (m_wakeEvent) {
            SetEvent(m_wakeEvent);
        }
    }
}

std::wstring AudioInputManager::GetSelectedDeviceId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_selectedDeviceId;
}

std::wstring AudioInputManager::GetSelectedDeviceName() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_selectedDeviceName;
}

int AudioInputManager::GetSelectedChannel() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_selectedChannel;
}

int AudioInputManager::GetSelectedDeviceChannelCount() const {
    std::wstring selId = GetSelectedDeviceId();
    auto devices = EnumerateCaptureDevices();
    for (const auto& d : devices) {
        if (d.id == selId) {
            return std::max(1, d.channels);
        }
    }
    return 1;
}

std::wstring AudioInputManager::GetStatusMessage() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_statusMessage;
}

void AudioInputManager::SetAudioDataCallback(AudioDataCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_onAudioData = std::move(cb);
}

void AudioInputManager::SetStateChangeCallback(StateChangeCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_onStateChange = std::move(cb);
}

void AudioInputManager::SetDeviceListChangedCallback(DeviceListChangedCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_onDeviceListChanged = std::move(cb);
}

void AudioInputManager::OnDeviceNotification(LPCWSTR deviceId, bool isDefaultCaptureChange) {
    bool shouldReopen = false;
    DeviceListChangedCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb = m_onDeviceListChanged;

        AudioCaptureState curState = m_state.load();
        if (curState != AudioCaptureState::Capturing) {
            // If waiting for a disconnected/unavailable microphone, wake up to retry
            shouldReopen = true;
        } else if (isDefaultCaptureChange) {
            // Only reopen if user selected "default" and the new default differs from active
            if (m_selectedDeviceId == L"default" || m_selectedDeviceId.empty()) {
                if (!deviceId || m_activeDeviceId != deviceId) {
                    shouldReopen = true;
                }
            }
        } else if (deviceId && !m_activeDeviceId.empty() && _wcsicmp(deviceId, m_activeDeviceId.c_str()) == 0) {
            // Active capture device itself changed state or was removed
            shouldReopen = true;
        }
    }

    if (shouldReopen) {
        m_reopenRequested.store(true);
        if (m_wakeEvent) {
            SetEvent(m_wakeEvent);
        }
    }

    if (cb) {
        cb();
    }
}

void AudioInputManager::UpdateState(AudioCaptureState newState, const std::wstring& message) {
    StateChangeCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state.store(newState);
        m_statusMessage = message;
        cb = m_onStateChange;
    }
    if (cb) {
        cb(newState, message);
    }
}

bool AudioInputManager::StartCapture() {
    if (!m_initialized && !Init()) {
        return false;
    }

    if (m_wantCapture.load() && m_captureThread.joinable()) {
        m_reopenRequested.store(true);
        if (m_wakeEvent) SetEvent(m_wakeEvent);
        return true;
    }

    StopCapture();

    m_stopThread.store(false);
    m_wantCapture.store(true);
    m_reopenRequested.store(false);
    m_captureThread = std::thread(&AudioInputManager::CaptureThreadMain, this);
    return true;
}

void AudioInputManager::StopCapture() {
    m_wantCapture.store(false);
    m_stopThread.store(true);
    if (m_wakeEvent) {
        SetEvent(m_wakeEvent);
    }
    if (m_captureThread.joinable()) {
        m_captureThread.join();
    }
    m_liveLevel.store(0.0f);
    UpdateState(AudioCaptureState::Stopped, L"Stopped");
}

void AudioInputManager::CaptureThreadMain() {
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(enumerator.GetAddressOf())
    );

    if (FAILED(hr) || !enumerator) {
        UpdateState(AudioCaptureState::Error, L"Failed to initialize Core Audio enumerator");
        if (SUCCEEDED(hrCo)) CoUninitialize();
        return;
    }

    while (!m_stopThread.load()) {
        m_reopenRequested.store(false);

        std::wstring targetId;
        std::wstring targetNameHint;
        int selectedChannel = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            targetId = m_selectedDeviceId;
            targetNameHint = m_selectedDeviceName;
            selectedChannel = m_selectedChannel;
        }

        Microsoft::WRL::ComPtr<IMMDevice> device;
        bool foundDevice = false;

        if (targetId == L"default" || targetId.empty()) {
            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, device.GetAddressOf())) ||
                SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, device.GetAddressOf()))) {
                foundDevice = (device != nullptr);
            }
        } else {
            // Explicit device selected: first try exact endpoint ID
            if (SUCCEEDED(enumerator->GetDevice(targetId.c_str(), device.GetAddressOf())) && device) {
                DWORD state = 0;
                if (SUCCEEDED(device->GetState(&state)) && state == DEVICE_STATE_ACTIVE) {
                    foundDevice = true;
                } else {
                    device.Reset();
                }
            }

            // If endpoint ID changed after USB re-enumeration, try matching the same logical device by friendlyNameHint
            // (never fall back to an unrelated microphone!)
            if (!foundDevice && !targetNameHint.empty()) {
                Microsoft::WRL::ComPtr<IMMDeviceCollection> col;
                if (SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, col.GetAddressOf())) && col) {
                    UINT count = 0;
                    col->GetCount(&count);
                    for (UINT i = 0; i < count; ++i) {
                        Microsoft::WRL::ComPtr<IMMDevice> candidate;
                        if (SUCCEEDED(col->Item(i, candidate.GetAddressOf())) && candidate) {
                            std::wstring candName = QueryDeviceFriendlyName(candidate.Get());
                            if (_wcsicmp(candName.c_str(), targetNameHint.c_str()) == 0) {
                                device = candidate;
                                foundDevice = true;
                                LPWSTR newId = nullptr;
                                if (SUCCEEDED(candidate->GetId(&newId)) && newId) {
                                    std::lock_guard<std::mutex> lock(m_mutex);
                                    m_selectedDeviceId = newId;
                                    CoTaskMemFree(newId);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (!foundDevice || !device) {
            m_liveLevel.store(0.0f);
            std::wstring msg = (targetId == L"default" || targetId.empty())
                ? L"No recording device available"
                : (L"Microphone disconnected: " + (targetNameHint.empty() ? targetId : targetNameHint));
            UpdateState(AudioCaptureState::DeviceUnavailable, msg);

            // Wait for device notification or stop request (do NOT silently switch to another mic)
            WaitForSingleObject(m_wakeEvent, 1500);
            continue;
        }

        std::wstring activeName = QueryDeviceFriendlyName(device.Get());
        std::wstring openedDeviceId;
        {
            LPWSTR pwszOpenedId = nullptr;
            if (SUCCEEDED(device->GetId(&pwszOpenedId)) && pwszOpenedId) {
                openedDeviceId = pwszOpenedId;
                CoTaskMemFree(pwszOpenedId);
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            m_activeDeviceId = openedDeviceId;
            if (targetId != L"default") {
                m_selectedDeviceName = activeName;
            }
        }

        Microsoft::WRL::ComPtr<IAudioClient> audioClient;
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(audioClient.GetAddressOf()));
        if (FAILED(hr) || !audioClient) {
            UpdateState(AudioCaptureState::DeviceUnavailable, L"Microphone unavailable: " + activeName);
            WaitForSingleObject(m_wakeEvent, 1500);
            continue;
        }

        WAVEFORMATEX* pwfx = nullptr;
        hr = audioClient->GetMixFormat(&pwfx);
        if (FAILED(hr) || !pwfx) {
            UpdateState(AudioCaptureState::Error, L"Failed to query audio format");
            WaitForSingleObject(m_wakeEvent, 1500);
            continue;
        }

        AudioSampleFormat sampleFormat = AudioSampleFormat::Float32;
        if (!ParseWaveFormat(pwfx, sampleFormat)) {
            CoTaskMemFree(pwfx);
            UpdateState(AudioCaptureState::Error, L"Unsupported microphone sample format");
            WaitForSingleObject(m_wakeEvent, 1500);
            continue;
        }

        const int channels = std::max(1, static_cast<int>(pwfx->nChannels));
        const int sampleRate = static_cast<int>(pwfx->nSamplesPerSec);

        // 20 ms buffer in 100-ns units
        constexpr REFERENCE_TIME hnsBufferDuration = 200000;
        HANDLE hAudioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        bool useEventSync = (hAudioEvent != nullptr);

        hr = audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            useEventSync ? AUDCLNT_STREAMFLAGS_EVENTCALLBACK : 0,
            hnsBufferDuration,
            0,
            pwfx,
            nullptr
        );

        if (FAILED(hr) && useEventSync) {
            // Fallback to polling shared mode if driver rejects event callback
            useEventSync = false;
            hr = audioClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                0,
                hnsBufferDuration,
                0,
                pwfx,
                nullptr
            );
        }

        CoTaskMemFree(pwfx);
        pwfx = nullptr;

        if (FAILED(hr)) {
            if (hAudioEvent) CloseHandle(hAudioEvent);
            UpdateState(AudioCaptureState::Error, L"Failed to initialize WASAPI capture");
            WaitForSingleObject(m_wakeEvent, 1500);
            continue;
        }

        if (useEventSync && FAILED(audioClient->SetEventHandle(hAudioEvent))) {
            CloseHandle(hAudioEvent);
            UpdateState(AudioCaptureState::Error, L"Failed to set WASAPI event handle");
            WaitForSingleObject(m_wakeEvent, 1500);
            continue;
        }

        Microsoft::WRL::ComPtr<IAudioCaptureClient> captureClient;
        hr = audioClient->GetService(__uuidof(IAudioCaptureClient),
                                      reinterpret_cast<void**>(captureClient.GetAddressOf()));
        if (FAILED(hr) || !captureClient) {
            if (hAudioEvent) CloseHandle(hAudioEvent);
            UpdateState(AudioCaptureState::Error, L"Failed to obtain IAudioCaptureClient");
            WaitForSingleObject(m_wakeEvent, 1500);
            continue;
        }

        hr = audioClient->Start();
        if (FAILED(hr)) {
            if (hAudioEvent) CloseHandle(hAudioEvent);
            UpdateState(AudioCaptureState::Error, L"Failed to start WASAPI capture");
            WaitForSingleObject(m_wakeEvent, 1500);
            continue;
        }

        UpdateState(AudioCaptureState::Capturing, activeName);
        LOG_INFO("WASAPI capture started on '%ws' (%d Hz, %d ch, selectedCh=%d)",
                 activeName.c_str(), sampleRate, channels, selectedChannel);

        AudioResampler resampler;
        std::vector<float> monoFloat;
        std::vector<int16_t> pcm16;
        bool streamBroken = false;
        float runningAgcGain = 1.8f;

        while (!m_stopThread.load() && !m_reopenRequested.load() && !streamBroken) {
            if (useEventSync) {
                HANDLE waitHandles[2] = { hAudioEvent, m_wakeEvent };
                DWORD waitRes = WaitForMultipleObjects(m_wakeEvent ? 2 : 1, waitHandles, FALSE, 100);
                if (waitRes == WAIT_TIMEOUT) {
                    // Gently decay level meter during silence
                    m_liveLevel.store(m_liveLevel.load() * 0.8f);
                }
            } else {
                WaitForSingleObject(m_wakeEvent, 15);
            }

            if (m_stopThread.load() || m_reopenRequested.load()) {
                break;
            }

            UINT32 packetLength = 0;
            hr = captureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) {
                streamBroken = true;
                break;
            }

            while (packetLength != 0) {
                BYTE* pData = nullptr;
                UINT32 numFramesAvailable = 0;
                DWORD flags = 0;

                hr = captureClient->GetBuffer(&pData, &numFramesAvailable, &flags, nullptr, nullptr);
                if (FAILED(hr)) {
                    streamBroken = true;
                    break;
                }

                if (numFramesAvailable > 0) {
                    int curCh = GetSelectedChannel();
                    float peak = 0.0f;
                    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || !pData) {
                        monoFloat.assign(numFramesAvailable, 0.0f);
                        peak = 0.0f;
                    } else {
                        AudioResampler::ConvertToMonoFloat(
                            pData, numFramesAvailable, channels, sampleFormat, curCh, monoFloat, peak);

                        // On multi-channel USB audio interfaces (e.g. Focusrite iTrack Solo / Scarlett)
                        // where curCh == 0 (mixdown) halves a single active XLR channel, and on quiet
                        // microphones, apply smooth automatic gain normalization so Vosk receives
                        // strong, clear speech levels without clipping.
                        if (peak > 0.008f) {
                            float desiredGain = std::clamp(0.55f / peak, 1.0f, 5.0f);
                            if (desiredGain < runningAgcGain) {
                                runningAgcGain = 0.6f * runningAgcGain + 0.4f * desiredGain;  // Fast attack
                            } else {
                                runningAgcGain = 0.96f * runningAgcGain + 0.04f * desiredGain; // Smooth release
                            }
                        }
                        if (runningAgcGain > 1.02f && peak > 0.001f) {
                            float boostedPeak = 0.0f;
                            for (float& s : monoFloat) {
                                s = std::clamp(s * runningAgcGain, -1.0f, 1.0f);
                                float a = std::fabs(s);
                                if (a > boostedPeak) boostedPeak = a;
                            }
                            peak = boostedPeak;
                        }
                    }

                    // Smooth level meter
                    float prevLvl = m_liveLevel.load();
                    float nextLvl = (peak > prevLvl) ? peak : (prevLvl * 0.85f + peak * 0.15f);
                    m_liveLevel.store(std::clamp(nextLvl, 0.0f, 1.0f));

                    resampler.ResampleTo16kPcm16(monoFloat.data(), monoFloat.size(), sampleRate, pcm16);

                    if (!pcm16.empty()) {
                        AudioDataCallback dataCb;
                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            dataCb = m_onAudioData;
                        }
                        if (dataCb) {
                            dataCb(pcm16.data(), pcm16.size());
                        }
                    }
                }

                captureClient->ReleaseBuffer(numFramesAvailable);
                hr = captureClient->GetNextPacketSize(&packetLength);
                if (FAILED(hr)) {
                    streamBroken = true;
                    break;
                }
            }
        }

        audioClient->Stop();
        if (hAudioEvent) {
            CloseHandle(hAudioEvent);
        }

        if (streamBroken && !m_stopThread.load()) {
            LOG_WARN("WASAPI stream interrupted on '%ws'; will attempt recovery", activeName.c_str());
            m_liveLevel.store(0.0f);
            UpdateState(AudioCaptureState::DeviceUnavailable, L"Microphone disconnected: " + activeName);
            WaitForSingleObject(m_wakeEvent, 500);
        }
    }

    if (SUCCEEDED(hrCo)) {
        CoUninitialize();
    }
}

}  // namespace VirtualOverlay
