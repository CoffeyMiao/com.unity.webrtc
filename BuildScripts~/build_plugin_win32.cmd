@echo off

set LIBWEBRTC_DOWNLOAD_URL=https://github.com/CoffeyMiao/com.unity.webrtc/releases/download/M92/webrtc-win32.zip
set SOLUTION_DIR=%cd%\Plugin~

echo -------------------
echo Download LibWebRTC 

curl -L %LIBWEBRTC_DOWNLOAD_URL% > webrtc.zip
7z x -aoa webrtc.zip -o%SOLUTION_DIR%\webrtc

echo -------------------
echo Build com.unity.webrtc Plugin

cd %SOLUTION_DIR%
cmake --preset=Win32-windows-msvc
cmake --build --preset=release-windows-msvc-Win32 --target=WebRTCPlugin