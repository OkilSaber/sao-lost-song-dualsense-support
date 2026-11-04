@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist dist mkdir dist
if not exist build mkdir build
cl /nologo /O2 /W4 /MT /Isrc /Fobuild\ /Fdbuild\ src\xinput_proxy.c /LD /Fe:dist\xinput9_1_0.dll /link /DEF:src\xinput_proxy.def /IMPLIB:build\xinput.lib setupapi.lib hid.lib user32.lib || exit /b 1
cl /nologo /O2 /W4 /MT /Isrc /Fobuild\ /Fdbuild\ src\dinput8_proxy.c /LD /Fe:dist\dinput8.dll /link /DEF:src\dinput8_proxy.def /IMPLIB:build\dinput8.lib || exit /b 1
cl /nologo /O2 /W4 /MT /Isrc /Fobuild\ /Fdbuild\ src\test.c /Fe:build\test.exe || exit /b 1
cl /nologo /O2 /W4 /MT /Isrc /Fobuild\ /Fdbuild\ src\dinput_test.c /Fe:build\dinput_test.exe || exit /b 1
