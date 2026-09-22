# =============================================================================
#  Завантажує готову збірку FFmpeg (BtbN, 8.1, win64, gpl, shared) у
#  third_party\ffmpeg — потрібна для збірки програми у Visual Studio.
#
#  Запуск (з папки проекту):
#     powershell -ExecutionPolicy Bypass -File scripts\get_ffmpeg.ps1
# =============================================================================
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"   # інакше Invoke-WebRequest дуже повільний
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "third_party\ffmpeg"
$name = "ffmpeg-n8.1-latest-win64-gpl-shared-8.1"
$url  = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/$name.zip"
$zip  = Join-Path $env:TEMP "$name.zip"

if (Test-Path (Join-Path $dest "include\libavcodec\avcodec.h")) {
    Write-Host "FFmpeg уже є у $dest"
    exit 0
}
Write-Host "Завантажую $url ..."
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
Write-Host "Розпаковую..."
$tmp = Join-Path $env:TEMP "gmdr_ffmpeg_unpack"
if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
Expand-Archive -Path $zip -DestinationPath $tmp
if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
Move-Item (Join-Path $tmp $name) $dest
Remove-Item $zip -Force
Write-Host "Готово: FFmpeg у $dest"
