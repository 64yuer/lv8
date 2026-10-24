@echo off
call "D:\VS2022BT\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d D:\pyproj\lv8\native\boost\boost_1_84_0
call b2.exe stage -j 8 address-model=64 architecture=x86 link=static runtime-link=static --with-python --with-filesystem --with-iostreams --with-date_time --with-thread
echo B2_EXIT=%errorlevel%
