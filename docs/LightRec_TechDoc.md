# LightRec（轻录）录屏软件 技术实现文档
版本：V1.1
日期：2026-10-22
目标平台：Windows10 1903+ / Windows11 64位
主技术栈：C++17 + Qt6 (Qt Widgets) + FFmpeg
备选方案：C# .NET9 + WPF + HandyControl + MicaWPF
排除方案说明：Flutter、Electron、WinForms
> 选型理由：EV原生为C++ Qt，该技术栈对录屏、全局Overlay、FFmpeg集成生态成熟，复刻EV交互阻力最小。

## 目录
1. 选型论证 & 各方案对比
2. 整体架构设计
3. 模块详细设计
    3.1 UI层（Qt Widgets）
    3.2 屏幕捕获模块
    3.3 音频采集模块
    3.4 Overlay顶层绘制模块（画板/鼠标特效/按键/摄像头）
    3.5 FFmpeg音视频编码 & 后处理模块
    3.6 RTMP推流模块
    3.7 局域网直播Web服务模块
    3.8 定时任务 & 预设管理模块
4. 主题与DPI适配方案
5. 依赖库清单、编译、打包方案
6. 备选技术栈（WPF方案）简介
7. 不选用Flutter的原因
8. 开发注意事项、坑点与兜底策略

---

## 1. 选型论证 & 方案对比
| 方案 | UI还原EV能力 | 全局Overlay画板难度 | Win11 Fluent/Mica | 录屏生态成熟度 | 推荐等级 |
| ---- | ---- | ---- | ---- | ---- | ---- |
| C++ Qt6 Widgets | ⭐⭐⭐⭐⭐（EV同源，完美复刻EV界面） | 中等，大量开源参考 | 仅可模拟，无原生Mica | ⭐⭐⭐⭐⭐国内录屏主流 | ✅【主方案】 |
| C# WPF + HandyControl | ⭐⭐⭐⭐，可复刻EV交互布局 | 低，Direct2D成熟 | ✅支持MicaWPF云母材质 | ⭐⭐⭐⭐ | ⭐备选方案 |
| C# WinUI3 | ⭐⭐⭐ | 高，全局悬浮窗实现复杂 | ✅原生最强Fluent | ⭐⭐ | 不推荐 |
| Dart Flutter | ⭐⭐⭐⭐⭐UI绘制自由 | ❌极高，必须额外写C++原生插件做全局悬浮、抓屏 | ❌只能模拟材质 | ⭐冷门，缺少成熟轮子 | ❌不推荐 |
| Electron | ⭐⭐⭐⭐ | 全局Overlay很难做，包体积巨大 | ❌web质感违和 | ⭐⭐⭐⭐ | ❌不推荐 |

### 为什么不选择Flutter（重点）
1. Flutter内置Overlay仅支持App窗口内部叠加，**无法跨窗口全局置顶画板、悬浮录制条**；想要全局Overlay必须单独开发一套C++ Win32原生窗口，两套渲染引擎，工作量翻倍，调试困难
2. Windows Graphics Capture（WGC）没有成熟Flutter插件，屏幕捕获、音频采集插件需要从零开发
3. 录屏对音视频时序、低延迟、硬件编码要求高，Dart层转发音视频流容易引入延迟丢帧
4. 打包体积偏大，达不到EV轻量化的体验
5. 桌面录屏开源案例极少，踩坑无参考

> 本项目主路线：C++ Qt6 Widgets，和EV保持一致底层路线，最大限度复用同类项目成熟代码。

---

## 2. 整体架构设计（分层）
```
──────────────────── UI 层（Qt Widgets） ────────────────────┐
│ 主控制面板、悬浮录制条、设置页、媒体工具箱、直播面板、弹窗  │
│ 主题管理（浅色 / 深色 / 跟随系统）、热键管理、预设管理          │
└───────────────────────────┬───────────────────────────────┘
│ Qt 信号槽通信
┌────────────────────业务逻辑层──────────────────────────────┐
│ 录制控制器、直播控制器、定时任务调度、水印管理、配置持久化   │
└───────────────────────────┬───────────────────────────────┘
│
┌───────底层能力层──────────┴──────────────────────────────┐
│ 屏幕捕获（WGC / DesktopDuplication）                     │
│ WASAPI 音频采集（系统音 + 麦克风双轨）                       │
│ 全局 Overlay 绘制（Direct2D）：画板、鼠标特效、按键、摄像头 │
│ FFmpeg 封装：编码、转码、修复、变速、GIF、RTMP 推流        │
│ 内网 Web 服务（Qt HttpServer）局域网直播                   │
└──────────────────────────────────────────────────────────┘
```
- 配置持久化：Qt QSettings，保存主题、热键、水印、预设、保存路径
- 日志：Qt自带日志模块，可选输出日志文件用于排查崩溃

---

## 3. 模块详细设计
### 3.1 UI层 Qt Widgets
1. 主窗口布局：对齐EV布局，左侧功能导航，右侧主操作面板
2. 悬浮录制条：独立无边框置顶Qt窗口，支持拖动，鼠标穿透控制
3. 主题实现：自定义QSS样式表，三套样式（EV默认蓝主题浅色、深色、跟随系统）
    - 跟随系统：调用Win32 API读取Windows注册表系统主题配置，监听主题变更消息，自动切换QSS
4. 控件：QPushButton、QComboBox、QTableWidget（文件列表）、QDialog弹窗；自定义绘制颜色板、画板工具栏
5. 全局热键：Qt GlobalHotkey第三方组件 / Win32 RegisterHotKey

### 3.2 屏幕捕获模块
优先：Windows Graphics Capture（WGC，Win10 1903+/Win11现代捕获API）
兜底：DXGI DesktopDuplication（兼容旧显卡、兼容WGC失效场景）
输出：RGB / NV12帧，交给Overlay层叠加画面，送入FFmpeg编码器

### 3.3 音频采集模块
WASAPI 独占/共享模式采集
- 两路独立采集：系统Loopback音频、麦克风输入音频
- 音频预处理：降噪、增益（基于FFmpeg滤镜或者自定义音频处理）
- 输出两路PCM音频，独立音轨封装进MP4

### 3.4 Overlay顶层绘制模块（核心难点，对标EV画板）
> 独立顶层窗口，覆盖全部桌面窗口，录制时把绘制内容混合到视频帧
方案：Direct2D 绘制
绘制内容：
1. 画板图形：直线、矩形、圆形、箭头、文字
2. 鼠标高亮环、点击特效
3. 悬浮按键文字面板
4. 摄像头画面（MediaFoundation采集摄像头，转纹理叠加）
逻辑：
- 用户在悬浮画板上操作 → 记录绘制指令
- 每一帧视频捕获之后，把所有Overlay元素渲染到视频帧上，再送入编码器
- 支持一键隐藏所有Overlay元素，不停止录制

### 3.5 FFmpeg音视频编码 & 后处理模块
FFmpeg作为核心音视频引擎，动态链接（ffmpeg.dll）
能力清单：
- 实时编码：H.264 / H.265 硬件编码 NVENC / AMF / QSV；软编x264/x265兜底
- 后处理工具：
  - mp4修复（moov修复，处理异常中断录屏）
  - 视频变速滤镜 setpts
  - 画面裁剪、缩放、压缩
  - gif滤镜输出GIF
  - 水印叠加（文字/图片水印）、批量水印
- 同步音视频时间戳，保证音画同步

### 3.6 RTMP推流模块
两种实现二选一：
1. FFmpeg avformat 直接推RTMP（推荐，稳定）
2. Qt + librtmp
能力：推流同时，复制一份流本地保存（串流存盘）

### 3.7 局域网直播Web服务模块
Qt HttpServer搭建轻量web服务
- 提供简单html播放器页面（video标签播放HLS或者MJPEG流）
- 生成访问地址、二维码
- 在线连接计数，上限控制≥10客户端
⚠️ 注意：Windows防火墙会拦截，增加提示，引导用户放行

### 3.8 定时任务 & 预设管理
- 定时任务：QTimer + 系统时间比对；持久化保存任务列表，开机可加载任务
- 预设：序列化全部录制参数到json文件，保存到软件配置目录，一键加载

---

## 4. 主题与DPI适配
1. DPI：Qt开启高DPI感知 `Qt::AA_EnableHighDpiScaling`，`Qt::AA_UseHighDpiPixmaps`
2. 主题切换逻辑：
   - 浅色：EV原版蓝色扁平风格
   - 深色：深色背景，保留品牌蓝色强调色
   - 跟随系统：Win32读取注册表 `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize` AppsUseLighting值，并且监听WM_SETTINGCHANGE消息，动态切换QSS，无需重启
> 限制：Qt Widgets原生不支持Mica云母材质，只能做样式模拟；想要原生Mica效果切换为WPF备选方案

---

## 5. 依赖库清单、编译、打包
### 依赖列表
- Qt 6.7+（Qt Widgets、Qt HttpServer、Qt Multimedia）
- FFmpeg 6.x（shared dll，avcodec avformat avfilter swscale）
- Direct2D SDK（Windows SDK自带）
- Win32 API（屏幕捕获、热键、主题读取）
- MediaFoundation：摄像头采集

### 编译环境
- Visual Studio 2022
- Qt Online Installer 安装Qt6 MSVC版本
- Windows SDK 10.0.22621

### 打包发布
windeployqt 收集Qt依赖，再打包ffmpeg dll；使用Enigma Virtual Box打包成单文件可选
打包后资源：主程序 + ffmpeg.dll + 字体/样式资源

---

## 6. 备选方案：C# .NET9 + WPF + HandyControl + MicaWPF
适合场景：想要EV交互 + Win11原生Mica云母、Fluent质感，不想写大量C++
- UI：HandyControl控件库，自带深浅色+跟随系统，MicaWPF实现云母标题栏
- 屏幕捕获：Windows Graphics Capture C#封装库
- 音频：NAudio WASAPI
- Overlay：WPF顶层窗口 + Direct2D绘制画板特效
- FFmpeg：FFmpeg.AutoGen
- 局域网直播：Kestrel web服务
优点：C#开发速度更快，生态友好；缺点：不是EV同源技术栈

---

## 7. 开发风险与兜底策略
1. 硬件编码失败：自动检测编码器可用性，失败自动切软编码，弹窗提示用户
2. WGC捕获失败：自动降级DesktopDuplication
3. 音频采集异常：提示切换音频设备，提供备选采集模式
4. 全局Overlay鼠标穿透、置顶异常：增加Win32窗口扩展样式WS_EX_LAYERED、WS_EX_TRANSPARENT控制鼠标穿透
5. FFmpeg版本兼容：固定FFmpeg版本，不使用系统内其他ffmpeg，内置独立dll
