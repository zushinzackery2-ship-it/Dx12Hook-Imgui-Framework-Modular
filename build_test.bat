@echo off
setlocal

set "ROOT=%CD%"
set "OUTDIR=%ROOT%\x64\Release"

if not exist "%OUTDIR%" mkdir "%OUTDIR%"

echo === Calling vcvars64.bat ===
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if %ERRORLEVEL% NEQ 0 (
    echo vcvars64.bat failed
    exit /b 1
)

echo === Build configuration: Release (Test) ===
echo === Compiling DX12HookTest.dll ===

cl.exe /nologo /W0 /std:c++20 /EHsc /utf-8 /O2 /MT ^
  /D"NDEBUG" /D"_WINDOWS" /D"_USRDLL" /D"UNICODE" /D"_UNICODE" ^
  /I "%ROOT%" ^
  /I "%ROOT%\Core" ^
  /I "%ROOT%\Core\DX12" ^
  /I "%ROOT%\Hook" ^
  /I "%ROOT%\ImGui" ^
  /Fo"%OUTDIR%\\" ^
  "%ROOT%\main_test.cpp" ^
  "%ROOT%\Hook\D3D12Hook.cpp" ^
  "%ROOT%\Hook\VmtHook.cpp" ^
  "%ROOT%\Hook\HookManager.cpp" ^
  "%ROOT%\Core\DX12\DX12Renderer.cpp" ^
  "%ROOT%\Core\DX12\DX12HookCallbacks.cpp" ^
  "%ROOT%\ImGui\imgui.cpp" ^
  "%ROOT%\ImGui\imgui_demo.cpp" ^
  "%ROOT%\ImGui\imgui_draw.cpp" ^
  "%ROOT%\ImGui\imgui_tables.cpp" ^
  "%ROOT%\ImGui\imgui_widgets.cpp" ^
  "%ROOT%\ImGui\imgui_impl_dx12.cpp" ^
  "%ROOT%\ImGui\imgui_impl_win32.cpp" ^
  /link /DLL /SUBSYSTEM:WINDOWS ^
  user32.lib kernel32.lib gdi32.lib d3d12.lib dxgi.lib Advapi32.lib ^
  /OUT:"%OUTDIR%\DX12HookTest.dll"

set ERR=%ERRORLEVEL%
echo.
echo === Build finished with exit code: %ERR% ===
if %ERR% EQU 0 (
    echo === DLL location: %OUTDIR%\DX12HookTest.dll ===
)
exit /b %ERR%
