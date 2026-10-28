#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace VirtualOverlay {

struct AudioEndpointInfo {
    std::wstring id;            // "default" or Windows MMDevice endpoint ID
    std::wstring friendlyName;  // Human-readable name (e.g. "Microphone (Maono PD200X)")
    int channels = 1;           // Native channel count
    int sampleRate = 48000;     // Native sample rate
    bool isDefault = false;     // True if this is the current system default capture endpoint
    bool isAvailable = true;    // True if currently active/connected
};

enum class AudioCaptureState {
    Stopped,
    Capturing,
    DeviceUnavailable,
    Error
};

enum class AudioSampleFormat {
    Float32,
    Pcm16,
    Pcm24,
    Pcm32
};

// Pure format conversion & resampler state (exposed for deterministic unit testing)
class AudioResampler {
public:
    static constexpr int kTargetSampleRate = 16000;

    void Reset();

    // Converts interleaved input frames to mono float samples [-1.0, 1.0].
    // selectedChannel: 0 = mixdown all channels; 1..channels = specific 1-based channel.
    static void ConvertToMonoFloat(
        const uint8_t* rawBytes,
        size_t frameCount,
        int channels,
        AudioSampleFormat format,
        int selectedChannel,
        std::vector<float>& outMonoFloat,
        float& outPeakLevel
    );

    // Resamples mono float samples from sourceSampleRate to 16000 Hz PCM16.
    void ResampleTo16kPcm16(
        const float* monoSamples,
        size_t sampleCount,
        int sourceSampleRate,
        std::vector<int16_t>& outPcm16
    );

private:
    double m_srcPos = 0.0;
    float m_prevSample = 0.0f;
    bool m_hasPrev = false;
};

class AudioInputManager {
public:
    using AudioDataCallback = std::function<void(const int16_t* samples16kMono, size_t sampleCount)>;
    using StateChangeCallback = std::function<void(AudioCaptureState state, const std::wstring& message)>;
    using DeviceListChangedCallback = std::function<void()>;

    static AudioInputManager& Instance();

    bool Init();
    void Shutdown();

    // Enumerate all active capture devices (first entry is always "default")
    std::vector<AudioEndpointInfo> EnumerateCaptureDevices() const;

    // Select device by endpoint ID ("default" or specific MMDevice ID) and optional friendlyName for recovery
    void SetSelectedDevice(const std::wstring& deviceId, const std::wstring& friendlyNameHint = L"", int channel = 0);
    std::wstring GetSelectedDeviceId() const;
    std::wstring GetSelectedDeviceName() const;
    int GetSelectedChannel() const;

    // Query channel count of currently selected device
    int GetSelectedDeviceChannelCount() const;

    // Start / stop WASAPI capture
    bool StartCapture();
    void StopCapture();

    AudioCaptureState GetState() const { return m_state.load(); }
    std::wstring GetStatusMessage() const;
    float GetLiveLevel() const { return m_liveLevel.load(); }

    void SetAudioDataCallback(AudioDataCallback cb);
    void SetStateChangeCallback(StateChangeCallback cb);
    void SetDeviceListChangedCallback(DeviceListChangedCallback cb);

    // Internal notification handler invoked by MMNotificationClient
    void OnDeviceNotification(LPCWSTR deviceId = nullptr, bool isDefaultCaptureChange = false);

private:
    AudioInputManager();
    ~AudioInputManager();
    AudioInputManager(const AudioInputManager&) = delete;
    AudioInputManager& operator=(const AudioInputManager&) = delete;

    void CaptureThreadMain();
    void UpdateState(AudioCaptureState newState, const std::wstring& message);

    class DeviceNotificationClient;

    mutable std::mutex m_mutex;
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> m_enumerator;
    Microsoft::WRL::ComPtr<IMMNotificationClient> m_notifClient;
    bool m_initialized = false;

    std::wstring m_selectedDeviceId = L"default";
    std::wstring m_selectedDeviceName;
    std::wstring m_activeDeviceId;
    int m_selectedChannel = 0;  // 0 = all channels (mono mixdown), 1+ = 1-based channel

    std::atomic<AudioCaptureState> m_state{ AudioCaptureState::Stopped };
    std::wstring m_statusMessage;
    std::atomic<float> m_liveLevel{ 0.0f };

    std::atomic<bool> m_wantCapture{ false };
    std::atomic<bool> m_stopThread{ false };
    std::atomic<bool> m_reopenRequested{ false };
    HANDLE m_wakeEvent = nullptr;
    std::thread m_captureThread;

    AudioDataCallback m_onAudioData;
    StateChangeCallback m_onStateChange;
    DeviceListChangedCallback m_onDeviceListChanged;
};

}  // namespace VirtualOverlay
