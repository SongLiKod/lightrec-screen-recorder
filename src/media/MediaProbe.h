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
    // 角色识别结果：音轨在文件中的序号（0 = 第一条音频流），-1 表示文件里没有这条音轨。
    // 新录像通过音轨 title 元数据识别；旧录像按“第一条=系统声音、第二条=麦克风”的
    // 写入顺序识别，只有一条且无标题时用声道一致性（麦克风为双单声道）判断。
    int systemAudioIndex = -1;
    int micAudioIndex = -1;
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
