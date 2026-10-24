@echo off
rem Standalone V8 probe build+run (diagnostic only).
call "D:\VS2022BT\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d D:\pyproj\lv8\native\build
set V8=D:\pyproj\lv8\native\stpyv8\v8
cl /nologo /EHsc /std:c++20 /Zc:__cplusplus /O2 /MT /I"%V8%\include" probe_v8.cpp ^
  /Fe:probe_v8.exe ^
  /link /LIBPATH:"%V8%\out.gn\x64.release.sample\obj" ^
  v8_monolith.lib winmm.lib ws2_32.lib Advapi32.lib dbghelp.lib
echo PROBE_BUILD_EXIT=%errorlevel%
probe_v8.exe
echo PROBE_RUN_EXIT=%errorlevel%
