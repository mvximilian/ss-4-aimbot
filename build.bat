@echo off
setlocal
cd /d "%~dp0"
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "SAM4_VS=%%i"
if not defined SAM4_VS exit /b 1
call "%SAM4_VS%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1

if not exist "third_party\imgui\imgui.cpp" (
  echo Dear ImGui source not found - downloading pinned v1.92.9b...
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0fetch-imgui.ps1"
  if errorlevel 1 exit /b 1
)

if not exist "third_party\minhook\include\MinHook.h" (
  echo MinHook source not found - downloading pinned v1.3.4...
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0fetch-minhook.ps1"
  if errorlevel 1 exit /b 1
)

echo Building injected internal ImGui DLL...
cl /nologo /std:c++17 /EHsc /W3 /O2 /MT /LD internal_gui.cpp ^
  third_party\imgui\imgui.cpp third_party\imgui\imgui_draw.cpp third_party\imgui\imgui_tables.cpp third_party\imgui\imgui_widgets.cpp ^
  third_party\imgui\backends\imgui_impl_win32.cpp third_party\imgui\backends\imgui_impl_dx11.cpp ^
  third_party\minhook\src\buffer.c third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde64.c ^
  /Ithird_party\imgui /Ithird_party\imgui\backends /Ithird_party\minhook\include /Ithird_party\minhook\src /Ithird_party\minhook\src\hde ^
  /Fe:Sam4-ImGui.dll user32.lib d3d11.lib dxgi.lib
if errorlevel 1 exit /b 1

echo Building controller/injector...
cl /nologo /std:c++17 /EHsc /W4 /O2 /MT main.cpp /Fe:Sam4-Aim.exe user32.lib gdi32.lib comctl32.lib
if errorlevel 1 exit /b 1

echo Built Sam4-Aim.exe and Sam4-ImGui.dll
exit /b 0
