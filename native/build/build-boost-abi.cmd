@echo off
rem ============================================================
rem Build Boost 1.84 static libraries for one CPython ABI.
rem Usage: build-boost-abi.cmd <python Scripts directory>
rem Kept ASCII/UTF-8 so it is safe to run under a stock cmd.exe.
rem ============================================================
setlocal
set "REPO_ROOT=%~dp0..\.."
if "%~1"=="" (
  echo usage: %~nx0 ^<python Scripts directory^>
  exit /b 2
)
set "PY=%~1\python.exe"
if not exist "%PY%" (
  echo target interpreter not found: "%PY%"
  exit /b 2
)
if not defined VS2022_INSTALL set "VS2022_INSTALL=D:\VS2022BT"
if not exist "%VS2022_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" (
  echo VS 2022 vcvars64.bat not found: "%VS2022_INSTALL%"
  exit /b 2
)
call "%VS2022_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

set "BOOST_ROOT_DIR=%REPO_ROOT%\native\boost"
set "BOOST_DIR=%BOOST_ROOT_DIR%\boost_1_84_0"
if not exist "%BOOST_DIR%" (
  echo Boost source missing: "%BOOST_DIR%"
  echo Run native\build\build-boost.cmd once to download it, then retry.
  exit /b 2
)
cd /d "%BOOST_DIR%"
for /f "tokens=2,3 delims=. " %%A in ('"%PY%" --version') do set "PYABI=%%A%%B"
if "%PYABI%"=="" (
  echo could not determine target Python ABI
  exit /b 1
)
"%PY%" "%REPO_ROOT%\native\build\boost-python-config.py" > lv8-python-config.jam || exit /b 1

if not exist b2.exe (
  echo [%time%] bootstrapping b2 ...
  call bootstrap.bat vc142 || exit /b 1
)
if exist "stage\lib\libboost_python%PYABI%-vc143-mt-s-x64-1_84.lib" goto done
if exist "stage\lib\libboost_python%PYABI%-vc142-mt-s-x64-1_84.lib" goto make_vc143_alias

echo [%time%] b2 stage for CPython %PYABI% ...
call b2.exe --user-config=lv8-python-config.jam stage -j 8 link=static runtime-link=static address-model=64 architecture=x86 python=3.%PYABI:~1% --with-python --with-filesystem --with-iostreams --with-date_time --with-thread || exit /b 1
:make_vc143_alias
if exist "stage\lib\libboost_python%PYABI%-vc143-mt-s-x64-1_84.lib" goto done
copy /y "stage\lib\libboost_python%PYABI%-vc142-mt-s-x64-1_84.lib" "stage\lib\libboost_python%PYABI%-vc143-mt-s-x64-1_84.lib" >nul || exit /b 1
:done
echo [%time%] === BOOST_BUILD_OK cp%PYABI% ===
dir stage\lib\*boost_python%PYABI%*.lib
exit /b 0
