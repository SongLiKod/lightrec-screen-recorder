param(
    [string]$QtPath = $env:QT_ROOT,
    [string]$FfmpegRoot = $env:FFMPEG_ROOT,
    [string]$FfmpegBin = $env:FFMPEG_BIN,
    [string]$BuildDir = "build",
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Config = "Release",
    [switch]$MinGW,
    [switch]$NoFfmpeg
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

if (-not $QtPath) {
    throw "请通过 -QtPath 或 QT_ROOT 指定 Qt 安装目录，例如 C:\Qt\6.8.3\msvc2022_64"
}

$generator = if ($MinGW) { "MinGW Makefiles" } else { "Visual Studio 17 2022" }
$args = @(
    "-S", $root,
    "-B", (Join-Path $root $BuildDir),
    "-G", $generator,
    "-DCMAKE_PREFIX_PATH=$QtPath",
    "-DCMAKE_BUILD_TYPE=$Config"
)
if (-not $MinGW) {
    $args += @("-A", "x64")
}
if ($FfmpegRoot) {
    $args += "-DFFMPEG_ROOT=$FfmpegRoot"
}
if ($FfmpegBin) {
    $args += "-DFFMPEG_DLL_DIR=$FfmpegBin"
}
if ($NoFfmpeg) {
    $args += "-DLIGHTREC_WITH_FFMPEG=OFF"
}

cmake @args
cmake --build (Join-Path $root $BuildDir) --config $Config -j 8

Write-Host "构建完成：$(Join-Path $root $BuildDir)" -ForegroundColor Green
Write-Host "Qt 运行库已由 windeployqt 自动部署到 exe 同目录。" -ForegroundColor Green
