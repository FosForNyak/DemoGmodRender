# =============================================================================
#  Завантажує whisper-cli (whisper.cpp v1.9.4, збірка для процесора, win64) — для
#  розпізнавання мовлення гравців. Кладе лише потрібне (whisper-cli.exe, whisper.dll,
#  ggml*.dll) у теку whisper поруч із програмою. Модель розпізнавання — окремо: її
#  завантажує сама програма (вкладка «Чат» → «Завантажити модель...»).
#
#  Запуск (з папки проекту):
#     powershell -ExecutionPolicy Bypass -File scripts\get_whisper.ps1 [-Dest build\whisper]
# =============================================================================
param([string]$Dest = "")
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"   # інакше Invoke-WebRequest дуже повільний
$root = Split-Path -Parent $PSScriptRoot
if ($Dest -eq "") { $Dest = Join-Path $root "third_party\whisper" }
$tag = "b5130"   # реліз v1.9.4
$url = "https://github.com/ggml-org/whisper.cpp/releases/download/$tag/whisper-bin-x64.zip"
$zip = Join-Path $env:TEMP "whisper-bin-x64-$tag.zip"

if (Test-Path (Join-Path $Dest "whisper-cli.exe")) {
    Write-Host "whisper-cli уже є у $Dest"
    exit 0
}
Write-Host "Завантажую $url ..."
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
$tmp = Join-Path $env:TEMP "gmdr_whisper_unpack"
if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
Expand-Archive -Path $zip -DestinationPath $tmp
$bin = Get-ChildItem $tmp -Recurse -Filter whisper-cli.exe | Select-Object -First 1
if (-not $bin) { throw "в архіві немає whisper-cli.exe" }
New-Item -ItemType Directory -Force $Dest | Out-Null
Get-ChildItem $bin.DirectoryName | Where-Object { $_.Name -eq "whisper-cli.exe" -or $_.Name -eq "whisper.dll" -or $_.Name -like "ggml*.dll" } |
    Copy-Item -Destination $Dest
Remove-Item $tmp -Recurse -Force
Remove-Item $zip -Force
Write-Host "Готово: whisper-cli у $Dest"
