// 音轨导出参数构造器（FFmpegTool::audioExportArguments）的纯逻辑测试：
// 覆盖"只导出系统声音 / 只导出麦克风 / 两者混合 / 什么都不选"四种输入，
// 以及预览缓存（faststart=false）与非法音轨序号的降级行为。
#include "media/FFmpegTool.h"

#include <QStringList>

#include <cstdio>

using lightrec::FFmpegTool;

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        ++failures;
        // 使用 stderr 输出，控制台子系统与重定向都能看到
        fprintf(stderr, "FAIL: %s\n", what);
    }
}

bool has(const QStringList &arguments, const QString &flag, const QString &value)
{
    for (int i = 0; i + 1 < arguments.size(); ++i) {
        if (arguments.at(i) == flag && arguments.at(i + 1) == value)
            return true;
    }
    return false;
}

bool contains(const QStringList &arguments, const QString &needle)
{
    for (const QString &argument : arguments) {
        if (argument.contains(needle))
            return true;
    }
    return false;
}

} // namespace

int main()
{
    const QString input = QStringLiteral("C:/in/source.mp4");
    const QString output = QStringLiteral("C:/out/result.mp4");

    // 1. 没有勾选任何音轨 → 返回空参数，调用方据此跳过任务。
    const QStringList none = FFmpegTool::audioExportArguments(input, output, false, false, 0, 1);
    check(none.isEmpty(), "no selection returns empty arguments");

    // 2. 只导出系统声音（音轨 0）→ 单流挑选 + 视频/音频都复制。
    const QStringList system = FFmpegTool::audioExportArguments(input, output, true, false, 0, 1);
    check(has(system, QStringLiteral("-i"), input), "system keeps input");
    check(has(system, QStringLiteral("-map"), QStringLiteral("0:a:0")), "system maps first audio track");
    check(has(system, QStringLiteral("-map"), QStringLiteral("0:v?")), "system keeps optional video");
    check(has(system, QStringLiteral("-c:v"), QStringLiteral("copy")), "system copies video");
    check(has(system, QStringLiteral("-c:a"), QStringLiteral("copy")), "system copies audio");
    check(!contains(system, QStringLiteral("-filter_complex")), "system has no mix filter");
    check(has(system, QStringLiteral("-movflags"), QStringLiteral("+faststart")),
          "system keeps faststart");
    check(system.last() == output, "system ends with output");

    // 3. 只导出麦克风（音轨 1）。
    const QStringList mic = FFmpegTool::audioExportArguments(input, output, false, true, 0, 1);
    check(has(mic, QStringLiteral("-map"), QStringLiteral("0:a:1")), "mic maps second audio track");
    check(!has(mic, QStringLiteral("-map"), QStringLiteral("0:a:0")), "mic skips system track");
    check(has(mic, QStringLiteral("-c:a"), QStringLiteral("copy")), "mic copies audio");
    check(!contains(mic, QStringLiteral("-filter_complex")), "mic has no mix filter");

    // 4. 同时导出 → 混合成一条：系统 100% + 麦克风 -6 dB，输出 AAC，视频复制。
    const QStringList mixed = FFmpegTool::audioExportArguments(input, output, true, true, 0, 1);
    check(contains(mixed, QStringLiteral("-filter_complex")), "mixed uses filter_complex");
    check(contains(mixed, QStringLiteral("[0:a:0]volume=1.0[sys]")), "mixed keeps system at unity gain");
    check(contains(mixed, QStringLiteral("[0:a:1]volume=-6dB[mic]")), "mixed lowers mic by 6 dB");
    check(contains(mixed, QStringLiteral("amix=inputs=2:duration=first:normalize=0")),
          "mixed sums both inputs without normalizing");
    check(has(mixed, QStringLiteral("-map"), QStringLiteral("[aout]")), "mixed maps mix output");
    check(has(mixed, QStringLiteral("-c:v"), QStringLiteral("copy")), "mixed copies video");
    check(has(mixed, QStringLiteral("-c:a"), QStringLiteral("aac")), "mixed re-encodes audio");
    check(has(mixed, QStringLiteral("-b:a"), QStringLiteral("192k")), "mixed uses 192k audio");
    check(mixed.last() == output, "mixed ends with output");

    // 5. 预览缓存不需要 faststart（省去 moov 重排的整文件二次写入）。
    const QStringList preview =
        FFmpegTool::audioExportArguments(input, output, true, false, 0, 1, false);
    check(!contains(preview, QStringLiteral("+faststart")), "preview skips faststart");

    // 6. 非法音轨序号（-1 = 该音轨不存在）按降级规则处理。
    check(FFmpegTool::audioExportArguments(input, output, false, false, -1, -1).isEmpty(),
          "missing tracks return empty arguments");
    const QStringList onlyMicLeft =
        FFmpegTool::audioExportArguments(input, output, true, true, -1, 1);
    check(has(onlyMicLeft, QStringLiteral("-map"), QStringLiteral("0:a:1")),
          "missing system track falls back to mic only");
    check(!contains(onlyMicLeft, QStringLiteral("-filter_complex")),
          "missing system track skips mixing");

    // 7. 麦克风缺失时保留系统音轨。
    const QStringList onlySystemLeft =
        FFmpegTool::audioExportArguments(input, output, true, true, 0, -1);
    check(has(onlySystemLeft, QStringLiteral("-map"), QStringLiteral("0:a:0")),
          "missing mic track falls back to system only");

    if (failures == 0)
        fprintf(stderr, "AudioExportTest: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
