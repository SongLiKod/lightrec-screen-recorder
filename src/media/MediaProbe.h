#pragma once

#include "core/Types.h"

#include <QString>

namespace lightrec {

struct MediaInfo {
    bool valid = false;
    qint64 durationMs = 0;
    int width = 0;
    int height = 0;
    bool hasAudio = false;
    int audioTracks = 0;
    QString videoCodec;
    QString audioCodec;
    QString error;
};

class MediaProbe
{
public:
    static MediaInfo probe(const QString &filePath);
    static bool isPlayable(const QString &filePath);
    static RecordingFileInfo fileInfo(const QString &filePath);
};

} // namespace lightrec
