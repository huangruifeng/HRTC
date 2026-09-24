# ============================================================================
# OpenSSL 1.1.1w x64 运行库构建脚本（一次性）—— Qt 5.14.2 HTTPS/TLS 运行期依赖
#
# 背景：Qt5 的 QSslSocket 在运行期才动态加载 libssl-1_1-x64.dll / libcrypto-1_1-x64.dll
#       （已确认 Qt5Network[d].dll 内查找的就是这两个名字，Debug/Release 通用）。
#       缺失时网络请求报 "TLS initialization failed"。
#
# 使用步骤：
#   1) 下载源码（选 1.1.1w，1.1.1 LTS 最后一个补丁版；勿用 3.x，Qt 5.14 不兼容）：
#        https://github.com/openssl/openssl/releases/download/OpenSSL_1_1_1w/openssl-1.1.1w.tar.gz
#      解压出目录 openssl-1.1.1w，放到本脚本同目录（3rd/openssl/）：
#        tar -xzf openssl-1.1.1w.tar.gz -C e:\HRTC\3rd\openssl
#   2) 安装 Strawberry Perl（OpenSSL 1.1.1 的 Windows 构建必需）：
#        https://strawberryperl.com
#   3) 运行本脚本（VS2022 环境自动探测）：
#        powershell -ExecutionPolicy Bypass -File e:\HRTC\3rd\openssl\build_openssl.ps1
#
# 产物：3rd/openssl/dist/libssl-1_1-x64.dll、libcrypto-1_1-x64.dll
#       之后构建 hrtc_board_demo 时 CMake 会自动把它们拷贝到 exe 目录
#       （见 demo/QtBoard/CMakeLists.txt 的 OpenSSL 段）。
# ============================================================================
param(
    [string]$SrcDir = (Join-Path $PSScriptRoot 'openssl-1.1.1w'),
    [string]$OutDir = (Join-Path $PSScriptRoot 'dist')
)
$ErrorActionPreference = 'Stop'

if (-not (Test-Path (Join-Path $SrcDir 'Configure'))) {
    throw "未找到 OpenSSL 源码（缺少 Configure）：$SrcDir`n请先下载 openssl-1.1.1w.tar.gz 并解压到该目录。"
}
if (-not (Get-Command perl.exe -ErrorAction SilentlyContinue)) {
    throw '未找到 Perl：OpenSSL 1.1.1 的 Windows 构建需要 Strawberry Perl（https://strawberryperl.com）。'
}
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw '未找到 Visual Studio（需要含 VC x64 工具链的 VS）' }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "未找到 vcvars64.bat：$vcvars" }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# 在 vcvars64 环境的 cmd 会话中执行 1.1.1 官方构建流程（perl Configure -> nmake）
$buildCmd = Join-Path $env:TEMP 'hrtc_build_openssl.cmd'
$lines = @(
    '@echo off',
    'setlocal',
    'call "' + $vcvars + '" || exit /b 1',
    'cd /d "' + $SrcDir + '" || exit /b 1',
    'echo ==== perl Configure VC-WIN64A no-asm ====',
    'perl Configure VC-WIN64A no-asm || exit /b 1',
    'echo ==== nmake (首次约 5-15 分钟) ====',
    'nmake build_libs || nmake',
    'if errorlevel 1 exit /b 1',
    'copy /y libssl-1_1-x64.dll "' + $OutDir + '" || exit /b 1',
    'copy /y libcrypto-1_1-x64.dll "' + $OutDir + '" || exit /b 1',
    'echo BUILD_OK'
)
Set-Content -Path $buildCmd -Value $lines -Encoding Ascii

& cmd.exe /c $buildCmd
if ($LASTEXITCODE -ne 0) { throw "OpenSSL 构建失败（exit $LASTEXITCODE），请查看上方输出。" }

Get-ChildItem -Path $OutDir -Filter '*.dll' | Select-Object Name, Length
Write-Output "构建完成：$OutDir"
Write-Output '重新构建 hrtc_board_demo（Debug/Release 均可），CMake 会自动把这两个 DLL 拷贝到 exe 目录。'
