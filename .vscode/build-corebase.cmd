@echo off
setlocal
cd /d "%~dp0.."
set "VsFind=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%V in (`"%VsFind%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VsRoot=%%V"
if not defined VsRoot (
  echo Install Desktop development with C++ and a Windows SDK in Visual Studio Installer.
  exit /b 1
)
call "%VsRoot%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
if not exist "build" mkdir "build"
rc /nologo /fo "build\CoreBase.res" "CoreBase.rc"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /Zi /Od /DUNICODE /D_UNICODE /I".vscode" /Fo"build\CoreBaseUI.obj" /Fd"build\compiler.pdb" "CPU&GPU.cpp" /Fe"build\CoreBaseUI.exe" /link "build\CoreBase.res" /DEBUG /SUBSYSTEM:WINDOWS user32.lib gdi32.lib
exit /b %errorlevel%
