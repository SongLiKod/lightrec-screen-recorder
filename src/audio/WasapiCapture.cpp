#include "audio/WasapiCapture.h"
#include "audio/AudioProcessor.h"

#include <QDateTime>
#include <QDebug>

#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <objbase.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <future>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace lightrec {

namespace {

class ComInitializer
{
public:
    ComInitializer()
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_shouldUninitialize = SUCCEEDED(hr);
    }
    ~ComInitializer()
    {
        if (m_shouldUninitialize)
            CoUninitialize();
    }
    ComInitializer(const ComInitializer &) = delete;
    ComInitializer &operator=(const ComInitializer &) = delete;

private:
    bool m_shouldUninitialize = false;
};

QString friendlyName(IMMDevice *device)
{
    if (!device)
        return {};
    ComPtr<IPropertyStore> store;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &store)))
        return {};
    PROPVARIANT value;
    PropVariantInit(&value);
    QString name;
    if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR)
        name = QString::fromWCharArray(value.pwszVal);
    PropVariantClear(&value);
    return name;
}

QString deviceId(IMMDevice *device)
{
    if (!device)
        return {};
    LPWSTR id = nullptr;
    QString result;
    if (SUCCEEDED(device->GetId(&id)) && id) {
        result = QString::fromWCharArray(id);
        CoTaskMemFree(id);
    }
    return result;
}

QList<AudioDeviceInfo> enumerateDevices(EDataFlow flow, bool loopback)
{
    ComInitializer com;
    QList<AudioDeviceInfo> result;

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator))))
        return result;

    ComPtr<IMMDevice> defaultDevice;
    enumerator->GetDefaultAudioEndpoint(flow, eConsole, &defaultDevice);
    const QString defaultId = deviceId(defaultDevice.Get());

    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection)))
        return result;

    UINT count = 0;
    collection->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, &device)))
            continue;
        AudioDeviceInfo info;
        info.id = deviceId(device.Get());
        info.name = friendlyName(device.Get());
        info.isDefault = !defaultId.isEmpty() && info.id == defaultId;
        info.loopback = loopback;
        if (info.name.isEmpty())
            info.name = loopback ? QStringLiteral("系统声音设备") : QStringLiteral("麦克风设备");
        result.append(info);
    }
    return result;
}

} // namespace

struct WasapiCapture::Impl
{
    std::thread thread;
    std::atomic<bool> running{ false };
    std::atomic<bool> paused{ false };
    std::atomic<qint64> pauseStartedMs{ 0 };
    std::atomic<qint64> pausedTotalMs{ 0 };
    std::atomic<qint64> startEpochMs{ 0 };
    AudioFrameCallback callback;
    AudioProcessor processor;
    int gainDb = 0;
    bool noiseSuppression = false;
    bool loopback = false;
    int bitsPerSample = 16;
    qint64 fallbackFrames = 0;
    QString deviceName;
    AudioFrameFormat format;
    QString error;

    void run(WasapiCapture *owner, QString requestedDeviceId,
             std::promise<bool> ready, std::shared_ptr<std::atomic<bool>> readyFlag);
};

WasapiCapture::WasapiCapture()
    : m_impl(std::make_unique<Impl>())
{
}

WasapiCapture::~WasapiCapture()
{
    stop();
}

QList<AudioDeviceInfo> WasapiCapture::renderDevices()
{
    return enumerateDevices(eRender, true);
}

QList<AudioDeviceInfo> WasapiCapture::captureDevices()
{
    return enumerateDevices(eCapture, false);
}

void WasapiCapture::setCallback(AudioFrameCallback callback)
{
    m_callback = std::move(callback);
}

bool WasapiCapture::start(bool loopback, const QString &deviceId, QString *error)
{
    stop();

    m_loopback = loopback;
    m_impl->running = true;
    m_impl->paused = false;
    m_impl->pauseStartedMs = 0;
    m_impl->pausedTotalMs = 0;
    m_impl->startEpochMs = 0;
    m_impl->fallbackFrames = 0;
    m_impl->loopback = loopback;
    m_impl->callback = m_callback;
    m_impl->gainDb = m_gainDb;
    m_impl->noiseSuppression = m_noiseSuppression;
    m_impl->error.clear();

    auto ready = std::make_shared<std::promise<bool>>();
    auto readyFlag = std::make_shared<std::atomic<bool>>(false);
    std::future<bool> future = ready->get_future();

    m_impl->thread = std::thread([this, deviceId, ready, readyFlag]() mutable {
        m_impl->run(this, deviceId, std::move(*ready), readyFlag);
    });

    if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
        m_impl->running = false;
        if (m_impl->thread.joinable())
            m_impl->thread.join();
        if (error)
            *error = QStringLiteral("音频设备初始化超时");
        return false;
    }

    if (!future.get()) {
        m_impl->running = false;
        if (m_impl->thread.joinable())
            m_impl->thread.join();
        if (error)
            *error = m_impl->error.isEmpty() ? QStringLiteral("音频设备初始化失败") : m_impl->error;
        return false;
    }

    m_deviceName = m_impl->deviceName;
    m_format = m_impl->format;
    return true;
}

void WasapiCapture::stop()
{
    if (!m_impl)
        return;
    m_impl->running = false;
    if (m_impl->thread.joinable())
        m_impl->thread.join();
}

void WasapiCapture::setPaused(bool paused)
{
    if (!m_impl)
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool wasPaused = m_impl->paused.load();
    if (paused) {
        if (!wasPaused) {
            m_impl->pauseStartedMs = now;
            m_impl->paused = true;
        }
    } else {
        if (wasPaused) {
            const qint64 started = m_impl->pauseStartedMs.load();
            if (started > 0)
                m_impl->pausedTotalMs.fetch_add(std::max<qint64>(0, now - started));
            m_impl->pauseStartedMs = 0;
        }
        m_impl->paused = false;
    }
}

bool WasapiCapture::isRunning() const
{
    return m_impl && m_impl->running;
}

qint64 WasapiCapture::startEpochMs() const
{
    return m_impl ? m_impl->startEpochMs.load() : 0;
}

void WasapiCapture::Impl::run(WasapiCapture *owner, QString requestedDeviceId,
                              std::promise<bool> ready, std::shared_ptr<std::atomic<bool>> readyFlag)
{
    auto finishInit = [&](bool ok) {
        if (!readyFlag->exchange(true))
            ready.set_value(ok);
    };

    ComInitializer com;

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        error = QStringLiteral("无法创建音频设备枚举器");
        finishInit(false);
        return;
    }

    ComPtr<IMMDevice> device;
    if (requestedDeviceId.isEmpty()) {
        hr = enumerator->GetDefaultAudioEndpoint(loopback ? eRender : eCapture, eConsole, &device);
    } else {
        hr = enumerator->GetDevice(reinterpret_cast<LPCWSTR>(requestedDeviceId.utf16()), &device);
        if (FAILED(hr))
            hr = enumerator->GetDefaultAudioEndpoint(loopback ? eRender : eCapture, eConsole, &device);
    }
    if (FAILED(hr) || !device) {
        error = loopback ? QStringLiteral("未找到系统声音输出设备")
                         : QStringLiteral("未找到麦克风设备，请检查设备连接");
        finishInit(false);
        return;
    }

    deviceName = friendlyName(device.Get());

    ComPtr<IAudioClient> audioClient;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void **>(audioClient.GetAddressOf()));
    if (FAILED(hr)) {
        error = QStringLiteral("无法打开音频设备（可能被其他程序独占）");
        finishInit(false);
        return;
    }

    WAVEFORMATEX *mixFormat = nullptr;
    hr = audioClient->GetMixFormat(&mixFormat);
    if (FAILED(hr) || !mixFormat) {
        error = QStringLiteral("无法读取音频格式");
        finishInit(false);
        return;
    }

    static const GUID ieeeFloatSubtype = {
        0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }
    };
    bool isFloat = mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto *extensible = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(mixFormat);
        isFloat = IsEqualGUID(extensible->SubFormat, ieeeFloatSubtype);
    }

    format.sampleRate = static_cast<int>(mixFormat->nSamplesPerSec);
    format.channels = static_cast<int>(mixFormat->nChannels);
    format.isFloat = isFloat;
    bitsPerSample = static_cast<int>(mixFormat->wBitsPerSample);

    DWORD flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if (loopback)
        flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;

    const REFERENCE_TIME duration = 10000000; // 1 second
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, duration, 0, mixFormat, nullptr);
    CoTaskMemFree(mixFormat);
    mixFormat = nullptr;
    if (FAILED(hr)) {
        error = loopback ? QStringLiteral("系统声音采集初始化失败（声卡不支持环回采集）")
                         : QStringLiteral("麦克风采集初始化失败");
        finishInit(false);
        return;
    }

    ComPtr<IAudioCaptureClient> captureClient;
    hr = audioClient->GetService(IID_PPV_ARGS(&captureClient));
    if (FAILED(hr)) {
        error = QStringLiteral("无法获取音频采集接口");
        finishInit(false);
        return;
    }

    processor.setup(format.sampleRate, format.channels);
    processor.setGainDb(gainDb);
    processor.setNoiseSuppression(noiseSuppression);

    hr = audioClient->Start();
    if (FAILED(hr)) {
        error = QStringLiteral("启动音频采集失败");
        finishInit(false);
        return;
    }

    // Wall-clock timestamps keep both audio tracks aligned with the video
    // clock and avoid vendor-specific QPC unit quirks (some capture endpoints
    // report absolute 100 ns values instead of QPC ticks).
    startEpochMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 localStartEpochMs = startEpochMs.load();

    finishInit(true);

    const double gain = std::pow(10.0, gainDb / 20.0);
    std::vector<float> converted;

    while (running.load()) {
        UINT32 packetFrames = 0;
        hr = captureClient->GetNextPacketSize(&packetFrames);
        if (FAILED(hr))
            break;

        while (packetFrames > 0 && running.load()) {
            BYTE *data = nullptr;
            UINT32 frames = 0;
            DWORD bufferFlags = 0;
            UINT64 qpcPosition = 0;
            hr = captureClient->GetBuffer(&data, &frames, &bufferFlags, &qpcPosition, nullptr);
            if (FAILED(hr))
                break;

            if (frames > 0) {
                converted.resize(static_cast<size_t>(frames) * format.channels);
                const bool silent = (bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                if (silent || !data) {
                    std::fill(converted.begin(), converted.end(), 0.0f);
                } else if (isFloat) {
                    memcpy(converted.data(), data, converted.size() * sizeof(float));
                } else {
                    // 16/32-bit PCM -> float
                    if (bitsPerSample <= 16) {
                        const auto *src = reinterpret_cast<const qint16 *>(data);
                        for (size_t i = 0; i < converted.size(); ++i)
                            converted[i] = src[i] / 32768.0f;
                    } else {
                        const auto *src = reinterpret_cast<const qint32 *>(data);
                        for (size_t i = 0; i < converted.size(); ++i)
                            converted[i] = static_cast<float>(src[i] / 2147483648.0);
                    }
                }

                if (loopback) {
                    if (gain != 1.0) {
                        for (float &sample : converted)
                            sample = static_cast<float>(sample * gain);
                    }
                } else {
                    processor.process(converted.data(), static_cast<int>(frames), format.channels);
                }

                qint64 ptsMs = QDateTime::currentMSecsSinceEpoch() - localStartEpochMs
                               - pausedTotalMs.load();
                if (ptsMs < 0)
                    ptsMs = static_cast<qint64>(fallbackFrames * 1000 / format.sampleRate);
                fallbackFrames += frames;
                ptsMs = std::max<qint64>(0, ptsMs);

                if (callback && !paused.load()) {
                    QByteArray buffer(reinterpret_cast<const char *>(converted.data()),
                                      static_cast<int>(converted.size() * sizeof(float)));
                    callback(buffer, format, ptsMs);
                }
            }

            captureClient->ReleaseBuffer(frames);
            if (FAILED(captureClient->GetNextPacketSize(&packetFrames)))
                break;
        }

        Sleep(4);
    }

    audioClient->Stop();
    owner->m_format = format;
}

} // namespace lightrec
