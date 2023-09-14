@echo off

set SOLUTION_DIR=%cd%\Plugin~

echo -------------------
echo Unzip LibWebRTC 

copy %cd%\artifacts\webrtc-win.zip webrtc.zip
7z x -aoa webrtc.zip -o%SOLUTION_DIR%\webrtc

echo -------------------
echo Build com.unity.webrtc Plugin

cd %SOLUTION_DIR%
cmake --preset=Win32-windows-msvc
cmake --build --preset=debug-windows-msvc --target=WebRTCPlugin