#include "virtualcam/VirtualCamera.h"

#include "core/WindowUtils.h"

#include <QDebug>

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace lightrec {

namespace {
constexpr quint32 kMagic = 0x4C524341; // "LRCA"
constexpr quint32 kMaxWidth = 1280;
constexpr quint32 kMaxHeight = 720;
constexpr quint32 kHeaderSize = sizeof(quint32) * 5;
const wchar_t *kMappingName = L"Local\\LightRecVirtualCamera";

struct FrameHeader {
    quint32 magic;
    quint32 width;
    quint32 height;
    quint32 stride;
    volatile LONG sequence;
};
} // namespace

VirtualCamera::VirtualCamera(QObject *parent)
    : QObject(parent)
{
}

VirtualCamera::~VirtualCamera()
{
    stop();
}

bool VirtualCamera::isPlatformSupported()
{
    return true;
}

QString VirtualCamera::backendDescription()
{
    if (WindowUtils::isWindows11())
        return QStringLiteral("共享内存虚拟摄像头帧桥（Windows 11，需安装 LightRec 虚拟摄像头组件）");
    return QStringLiteral("共享内存虚拟摄像头帧桥（需安装 LightRec 虚拟摄像头组件）");
}

bool VirtualCamera::start(QString *error)
{
    stop();

    m_capacity = kHeaderSize + kMaxWidth * kMaxHeight * 4;
    m_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   m_capacity, kMappingName);
    if (!m_mapping) {
        m_lastError = QStringLiteral("无法创建虚拟摄像头共享内存（错误 %1）").arg(GetLastError());
        if (error)
            *error = m_lastError;
        emit errorOccurred(m_lastError);
        return false;
    }

    m_view = MapViewOfFile(static_cast<HANDLE>(m_mapping), FILE_MAP_ALL_ACCESS, 0, 0, m_capacity);
    if (!m_view) {
        m_lastError = QStringLiteral("无法映射虚拟摄像头共享内存");
        CloseHandle(static_cast<HANDLE>(m_mapping));
        m_mapping = nullptr;
        if (error)
            *error = m_lastError;
        emit errorOccurred(m_lastError);
        return false;
    }

    std::memset(m_view, 0, kHeaderSize);
    auto *header = static_cast<FrameHeader *>(m_view);
    header->magic = kMagic;
    m_running = true;
    m_sequence = 0;
    return true;
}

void VirtualCamera::stop()
{
    m_running = false;
    if (m_view) {
        UnmapViewOfFile(m_view);
        m_view = nullptr;
    }
    if (m_mapping) {
        CloseHandle(static_cast<HANDLE>(m_mapping));
        m_mapping = nullptr;
    }
    m_capacity = 0;
}

void VirtualCamera::pushFrame(const QImage &frame)
{
    if (!m_running || !m_view || frame.isNull())
        return;

    QImage image = frame;
    if (image.width() > static_cast<int>(kMaxWidth) || image.height() > static_cast<int>(kMaxHeight)) {
        image = image.scaled(static_cast<int>(kMaxWidth), static_cast<int>(kMaxHeight),
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (image.format() != QImage::Format_ARGB32)
        image = image.convertToFormat(QImage::Format_ARGB32);

    const quint32 width = static_cast<quint32>(image.width());
    const quint32 height = static_cast<quint32>(image.height());
    const quint32 stride = width * 4;
    if (kHeaderSize + stride * height > m_capacity)
        return;

    auto *header = static_cast<FrameHeader *>(m_view);
    auto *pixels = reinterpret_cast<uchar *>(static_cast<uchar *>(m_view) + kHeaderSize);

    header->width = width;
    header->height = height;
    header->stride = stride;
    InterlockedIncrement(&header->sequence);

    for (quint32 y = 0; y < height; ++y)
        std::memcpy(pixels + y * stride, image.constScanLine(static_cast<int>(y)), stride);

    InterlockedIncrement(&header->sequence);
    ++m_sequence;
}

} // namespace lightrec
