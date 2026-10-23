param(
    [string]$BuildDir = "build",
    [string]$FfmpegDir = "",
    [string]$Output = "dist",
    [switch]$Upx
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$versionFile = Join-Path $root "version.txt"
if (-not (Test-Path $versionFile)) {
    throw "缺少 version.txt，无法确定发布版本号"
}
$Version = (Get-Content $versionFile -Raw).Trim()
$build = Join-Path $root $BuildDir
$exe = Join-Path $build "LightRec.exe"

if (-not (Test-Path $exe)) {
    $releaseExe = Join-Path $build "Release\LightRec.exe"
    if (Test-Path $releaseExe) {
        $exe = $releaseExe
        $build = Split-Path -Parent $releaseExe
    }
}

if (-not (Test-Path $exe)) {
    throw "未找到 $exe，请先运行 scripts\build.ps1"
}

$target = Join-Path $root $Output
if (Test-Path $target) {
    Remove-Item $target -Recurse -Force
}
New-Item -ItemType Directory -Path $target | Out-Null

Copy-Item $exe $target
Copy-Item (Join-Path $root "README.md") $target

# Qt runtime + plugins are already next to the executable thanks to the CMake
# post-build windeployqt step.  Copy everything needed for a standalone folder.
$runtimePatterns = @(
    "Qt6*.dll", "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll",
    "avcodec*.dll", "avdevice*.dll", "avfilter*.dll", "avformat*.dll", "avutil*.dll",
    "postproc*.dll", "swscale*.dll", "swresample*.dll",
    "ffmpeg.exe", "d3dcompiler_*.dll"
)
foreach ($pattern in $runtimePatterns) {
    Get-ChildItem -Path $build -Filter $pattern -File -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item $_.FullName $target -Force }
}
foreach ($folder in @("platforms", "styles", "imageformats", "multimedia", "networkinformation", "tls", "generic")) {
    $source = Join-Path $build $folder
    if (Test-Path $source) {
        Copy-Item $source $target -Recurse -Force
    }
}

if ($FfmpegDir) {
    Get-ChildItem -Path $FfmpegDir -Filter "avcodec*.dll" -File -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item $_.FullName $target -Force }
    Get-ChildItem -Path $FfmpegDir -Filter "avdevice*.dll" -File -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item $_.FullName $target -Force }
    Get-ChildItem -Path $FfmpegDir -Filter "avfilter*.dll" -File -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item $_.FullName $target -Force }
    Get-ChildItem -Path $FfmpegDir -Filter "avformat*.dll" -File -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item $_.FullName $target -Force }
    Get-ChildItem -Path $FfmpegDir -Filter "avutil*.dll" -File -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item $_.FullName $target -Force }
    Get-ChildItem -Path $FfmpegDir -Filter "postproc*.dll" -File -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item $_.FullName $target -Force }
    Get-ChildItem -Path $FfmpegDir -Filter "swscale*.dll" -File -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item $_.FullName $target -Force }
    Get-ChildItem -Path $FfmpegDir -Filter "swresample*.dll" -File -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item $_.FullName $target -Force }
    $ffmpeg = Join-Path $FfmpegDir "ffmpeg.exe"
    if (Test-Path $ffmpeg) {
        Copy-Item $ffmpeg $target -Force
    }
}

if ($Upx) {
    $upx = Get-Command upx -ErrorAction SilentlyContinue
    if ($upx) {
        Get-ChildItem $target -Filter "*.exe" -File | ForEach-Object {
            & $upx.Source --best --lzma $_.FullName | Out-Null
        }
        Get-ChildItem $target -Filter "*.dll" -File | ForEach-Object {
            & $upx.Source --best --lzma $_.FullName | Out-Null
        }
    } else {
        Write-Warning "未找到 upx，跳过压缩"
    }
}

$zip = Join-Path $target ("LightRec-{0}-win64.zip" -f $Version)
if (Test-Path $zip) {
    Remove-Item $zip -Force
}
$tempZip = Join-Path $env:TEMP ("LightRec-{0}-{1}.zip" -f $Version, [guid]::NewGuid().ToString("N"))
Compress-Archive -Path (Join-Path $target "*") -DestinationPath $tempZip -CompressionLevel Optimal
Move-Item -LiteralPath $tempZip -Destination $zip -Force

$size = (Get-Item $zip).Length / 1MB
Write-Host ("打包完成：{0}（{1:N1} MB）" -f $zip, $size) -ForegroundColor Green
