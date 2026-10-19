@echo off
setlocal
cd /d "%~dp0"
set "VSTOOLS=C:\BuildTools\Common7\Tools\VsDevCmd.bat"
if not exist "%VSTOOLS%" (
  for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSTOOLS=%%i\Common7\Tools\VsDevCmd.bat"
)
call "%VSTOOLS%" -arch=amd64
if errorlevel 1 exit /b 1
msbuild MpcVideoRenderer.sln /nologo /m /p:Configuration=Release /p:Platform=x64 /v:minimal /fl /flp:logfile=_bin\ambilight-build.log
if errorlevel 1 exit /b 1
for %%T in (gpu config playback) do (
  cl /nologo /EHsc /std:c++20 tests\ambilight-%%T.cpp /Fo:_bin\ambilight-%%T.obj /Fe:_bin\ambilight-%%T.exe
  if errorlevel 1 exit /b 1
)
_bin\ambilight-gpu.exe
if errorlevel 1 exit /b 1
_bin\ambilight-config.exe "%CD%\_bin\Filter_x64\MpcVideoRenderer64.ax"
if errorlevel 1 exit /b 1
_bin\ambilight-config.exe "%CD%\_bin\Filter_x64\MpcVideoRenderer64.ax" --defaults
exit /b %errorlevel%
