# LightRec（轻录）录屏软件

LightRec 是面向 Windows 10 1903+ / Windows 11（64 位）的轻量化桌面录屏工具，按 `docs/LightRec_PRD.md` 与 `docs/LightRec_TechDoc.md` 实现。

主技术栈：**C++17 + Qt 6 Widgets + FFmpeg**。默认 UI 复刻 EV 录屏的扁平蓝色风格，支持跟随系统 / 浅色 / 深色三套主题与高 DPI 缩放。

---

## 1. 功能实现

| 模块 | 能力 |
| --- | --- |
| 基础录制 | 全屏（多显示器）、窗口（跟随窗口移动）、自定义选区；录制中可拖动调整选区位置/大小；开始/暂停/继续/停止；全局自定义热键 |
| 编码 | H.264 / H.265；NVENC / AMF / QSV 硬件编码优先，失败自动降级 libx264/libx265 并提示；15/24/30/60 fps；MP4 输出 |
| 文件管理 | 文件名、时长、大小、录制时间、分辨率、状态；预览、打开目录、重命名、删除、修复入口；多选后支持批量重命名、批量删除、批量修复、批量水印 |
| 实时 Overlay | 画笔、直线、箭头、矩形、圆形、文字；颜色/粗细/撤销/清空/一键隐藏；按键悬浮（组合键、字号、透明度、位置）；鼠标高亮环与左右键点击特效（大小/颜色/时长）；摄像头画中画（拖动缩放、镜像、透明度） |
| 布局模板 | 单屏 + 画中画、二分屏（屏幕 + 摄像头）、三分屏（屏幕 + 摄像头 + PPT 辅助图片） |
| 音频 | WASAPI 系统 Loopback + 麦克风双轨独立采集，MP4 双音轨；麦克风降噪、人声/系统增益 |
| 视频工具箱 | MP4 修复、0.5x–2.0x 变速、时间裁剪、画面裁切、压缩转码、片段导出 GIF、文字/图片水印、批量水印、水印方案预设 |
| 直播 | RTMP 推流 + 串流存盘；局域网 Web 直播（MJPEG），二维码 + 网页链接，16 个并发观看上限；直播画面同步叠加画板/鼠标/摄像头 |
| 任务与预设 | 定时开始/停止、多任务启停、持久化；整套参数预设保存/应用 |
| 附加能力 | 虚拟摄像头帧桥（共享内存）、崩溃缓存与启动恢复、单实例运行、日志文件 |
| UI / 主题 | 三主题即时切换、监听 Windows 主题变化、100%–200% DPI、Microsoft YaHei UI 字体优先、半透明置顶可拖动悬浮录制条（大小/样式/透明度/显示开关可配置）、EV 风格悬浮录制圆点（默认开启，点击开始/停止） |
| 快捷标注 | 悬浮工具栏内置标注、局部高亮（聚光灯）、局部放大（放大镜）；高亮/放大区域可在屏幕上拖动调整 |

> Overlay 窗口通过 `WDA_EXCLUDEFROMCAPTURE` 从屏幕捕获中排除，再由合成器逐帧写入视频，因此屏幕上的画板不会重复出现在录像里。

### 悬浮工具栏与快捷圆点

- 录音时显示半透明置顶工具栏；可在 **设置 → 外观** 中调整大小（100%–200%）、深色/浅色/跟随主题样式、透明度，以及是否显示。
- 工具栏内置 **标注 / 高亮 / 放大**：画板支持画笔、直线、箭头、矩形、圆形、文字；在画布上按 **1–6**（或 P/L/A/R/O/T）可快速切换工具，标注工具条会跟随悬浮录制工具栏移动。高亮（聚光灯）和放大（放大镜）可直接在屏幕上拖动选框调整区域。
- 默认开启 EV 风格的悬浮录制圆点：拖动可移动，单击开始/停止，双击打开主界面；可在设置中关闭或调整大小。
- 默认全局快捷键：开始/停止 `Ctrl+Alt+R`、暂停 `Ctrl+Alt+P`、画板 `Ctrl+Alt+D`、显示/隐藏叠加 `Ctrl+Alt+H`、摄像头 `Ctrl+Alt+C`、局部高亮 `Ctrl+Alt+S`、局部放大 `Ctrl+Alt+M`、调整选区 `Ctrl+Alt+A`；均可在设置中修改。悬浮工具栏每个按钮下方会直接显示对应快捷键。
- 开始录制前默认显示 5 秒透明倒计时（仅数字与提示文字，无背景），可在 **设置 → 存储** 调整为 0–30 秒；按 Esc 或空格可取消。

---

## 2. 目录结构

```
src/
  core/       配置、主题、热键、路径、日志、单实例
  capture/    WGC（可选）+ DXGI Desktop Duplication 捕获
  audio/      WASAPI 双轨采集、降噪/增益 DSP
  media/      FFmpeg 实时编码/推流、媒体探测、工具箱 CLI 封装
  overlay/    画板模型、合成器、顶层窗口、按键/鼠标/摄像头
  record/     录制控制器、崩溃日志、录像库
  live/       MJPEG 局域网服务、二维码、独立预览采集
  schedule/   定时任务
  preset/     预设序列化
  virtualcam/ 虚拟摄像头共享内存帧桥
  ui/         MainWindow、录制页、文件页、工具箱、直播、任务、预设、设置
resources/    QSS 主题、局域网播放页、图标
tests/        无界面媒体管线自检
```

---

## 3. 构建

### 3.1 依赖

- Visual Studio 2022（推荐）或 Qt 官方 MinGW 13.1 工具链
- Qt 6.7+（Widgets、Network、Multimedia、MultimediaWidgets）
- CMake 3.21+
- Windows SDK 10.0.22621（MSVC）
- FFmpeg **shared** SDK，包含 `libx264`/`libx265` 编码器（GPL 构建，例如 BtbN `ffmpeg-master-latest-win64-gpl-shared.zip`）。仓库已内置完整运行时到 `vendor/ffmpeg`（include/lib/bin），默认构建无需额外下载。

> FFmpeg 是必需依赖。仓库内 `vendor/ffmpeg` 已包含完整 GPL shared 运行时；若删除该目录且未指定 SDK，CMake 会直接报错。仅供 UI 开发时可显式使用 `-DLIGHTREC_WITH_FFMPEG=OFF -DLIGHTREC_ALLOW_NO_FFMPEG=ON`。

### 3.2 MSVC（推荐）

仓库已内置 `vendor/ffmpeg`，默认无需额外参数：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"
cmake --build build --config Release
```

如使用外部 FFmpeg SDK，可追加 `-DFFMPEG_ROOT="C:\ffmpeg" -DFFMPEG_DLL_DIR="C:\ffmpeg\bin"`。

### 3.3 MinGW

```powershell
cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\mingw_64" `
  -DCMAKE_CXX_COMPILER="C:\Qt\Tools\mingw1310_64\bin\g++.exe"
cmake --build build -j 8
```

`vendor/ffmpeg` 同时包含 MSVC `.lib` 和 MinGW `.dll.a`，两种工具链都可直接使用。

构建完成后 CMake 会自动执行 `windeployqt`，把 Qt DLL、插件和 MinGW 运行库复制到 `LightRec.exe` 同目录，因此可以直接双击运行，不会再出现“找不到 Qt6Gui.dll”。

也可手动部署：

```powershell
windeployqt --no-translations --no-opengl-sw build\Release\LightRec.exe
```

### 3.4 FFmpeg 说明

- `FFMPEG_ROOT` 指向包含 `include/` 与 `lib/` 的 SDK；`FFMPEG_DLL_DIR` 指向 `bin/`，CMake 会把 `avcodec*.dll`、`avformat*.dll`、`avutil*.dll`、`swscale*.dll`、`swresample*.dll` 和 `ffmpeg.exe` 复制到程序目录。
- 视频工具箱通过 `ffmpeg.exe` 执行离线滤镜（修复/变速/裁剪/压缩/GIF/水印）；请确保它位于程序目录或 `ffmpeg/ffmpeg.exe`。
- Qt 自带的 FFmpeg 运行库是精简构建，可能不包含 x264/x265 编码器；录制请使用完整的 GPL shared 构建。

---

## 4. 运行与诊断

```powershell
.\build\LightRec.exe
```

> MSVC 多配置生成器的可执行文件位于 `build\Release\LightRec.exe`；MinGW/单配置生成器位于 `build\LightRec.exe`。

- 配置文件、预设、崩溃日志位于 `%LOCALAPPDATA%\LightRec\LightRec`（Qt `AppConfigLocation`）。
- 录像默认保存到系统“视频/LightRec”，可在设置页修改。
- 无界面自检（录制 4 秒后自动退出，用于验证屏幕捕获/双音轨/编码/封装）：

```powershell
.\LightRec.exe --record-test 4
```

结果写入日志，同时生成一个 MP4 文件。

- 文字标注自检（验证文字工具对话框与标注模型写入）：

```powershell
cmake --build build --target LightRecAnnotationTest
.\build\LightRecAnnotationTest.exe
```

- 媒体管线单元自检（需要 FFmpeg 开发库）：

```powershell
cmake -S . -B build -DLIGHTREC_BUILD_TESTS=ON ...
cmake --build build --target LightRecMediaTest
.\build\LightRecMediaTest.exe
```

---

## 5. 打包

```powershell
.\scripts\package.ps1 -BuildDir build\Release -FfmpegDir C:\ffmpeg\bin -Output dist
```

脚本会收集主程序、Qt 运行库/插件、FFmpeg DLL 与 `ffmpeg.exe`，生成 `LightRec-1.1.0-win64.zip`。

PRD 的“30MB 以内”目标在 Qt + 完整 FFmpeg shared 构建下通常需要额外处理（UPX、裁剪未使用 Qt 模块、7z 自解压）。脚本会打印体积；如需极限压缩可对可执行文件和 DLL 使用 UPX，但会牺牲启动速度并可能触发杀软误报。

---

## 6. 已知说明

1. **WGC**：优先使用 Windows Graphics Capture，需要在 MSVC + C++/WinRT 头文件环境编译；MinGW 构建会自动使用 DXGI Desktop Duplication 兜底。运行时 WGC 初始化失败也会自动降级。
2. **虚拟摄像头**：实现了共享内存帧桥（`Local\LightRecVirtualCamera`，头部 5×uint32：magic/width/height/stride/sequence，后接 BGRA 像素）。会议软件要看到 “LightRec Camera” 仍需安装配套虚拟摄像头设备组件；这是 P2 能力，仓库不包含需要管理员注册的驱动 DLL。
3. **RTMP 音轨**：FLV/RTMP 只支持单音轨，推流优先使用麦克风（无麦克风时使用系统声音）；本地录像始终保留双音轨。
4. **局域网直播**：MJPEG 直连，首次使用需在 Windows 防火墙放行专用网络；并发上限 16，超过返回 503 页面。
5. **崩溃恢复**：录制写入分片 MP4 `.part` 缓存并持续更新 JSON 日志；异常退出后，下次启动会自动检测并重封装恢复（可在设置中关闭自动恢复）。

---

## 7. 验收自查

- [x] 全屏 / 窗口 / 选区录制，录制中调整选区
- [x] 暂停 / 继续 / 停止，全局热键
- [x] H.264/H.265，硬件编码优先与软编降级提示
- [x] 双音轨 + 降噪/增益
- [x] 画板、鼠标特效、按键显示、摄像头画中画写入视频
- [x] 单屏/二分屏/三分屏模板
- [x] 文件列表、预览、重命名、删除、打开目录
- [x] RTMP 推流 + 串流存盘
- [x] 局域网 MJPEG 直播 + 二维码 + ≥10 客户端
- [x] 修复、变速、裁剪/裁切、压缩、GIF、文字/图片/批量水印
- [x] 定时任务、预设管理
- [x] 三主题即时切换、跟随系统、高 DPI
- [x] 悬浮录制条（大小/样式/透明度/显示开关）+ EV 风格悬浮录制圆点
- [x] 工具栏标注、局部高亮、局部放大
- [x] 崩溃缓存与恢复
- [x] 虚拟摄像头帧桥（配套设备组件见说明）
