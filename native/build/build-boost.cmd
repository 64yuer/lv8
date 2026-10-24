@echo off
rem ============================================================
rem lv8: Boost 1.84 static build (for the stpyv8 extension).
rem Build per target ABI: build-boost.cmd [python-dir]
rem Env overrides: LV8_PYTHON, VS2022_INSTALL, LV8_USE_PROXY.
rem ============================================================
setlocal enabledelayedexpansion
set "REPO_ROOT=%~dp0..\.."

if "%LV8_USE_PROXY%"=="1" goto with_proxy
set HTTP_PROXY=
set HTTPS_PROXY=
set http_proxy=
set https_proxy=
goto proxy_done
:with_proxy
set HTTP_PROXY=http://127.0.0.1:7890
set HTTPS_PROXY=http://127.0.0.1:7890
set http_proxy=%HTTP_PROXY%
set https_proxy=%HTTPS_PROXY%
:proxy_done

if "%~1"=="" goto default_py
set "PY=%~1\python.exe"
goto py_set
:default_py
if defined LV8_PYTHON set PY=%LV8_PYTHON%
if defined PY goto py_set
set "PY=%REPO_ROOT%\.venv\Scripts\python.exe"
:py_set

if defined VS2022_INSTALL goto vs_set
set VS2022_INSTALL=D:\VS2022BT
:vs_set
if exist "%VS2022_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" goto vs_ok
for %%E in (Enterprise Professional Community BuildTools) do (
  if exist "C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VS2022_INSTALL=C:\Program Files\Microsoft Visual Studio\2022\%%E"
)
:vs_ok

set "BOOST_ROOT_DIR=%REPO_ROOT%\native\boost"

echo [%date% %time%] === boost pipeline start ===
rem MSVC environment required for bootstrap/b2
call "%VS2022_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1

if not exist "%BOOST_ROOT_DIR%\boost_1_84_0" (
  echo [%time%] downloading boost 1.84.0 ...
  if not exist %BOOST_ROOT_DIR% mkdir %BOOST_ROOT_DIR%
  curl -L -o %BOOST_ROOT_DIR%\boost.zip https://archives.boost.io/release/1.84.0/source/boost_1_84_0.zip || exit /b 1
  echo [%time%] extracting ...
  powershell -NoProfile -Command "Expand-Archive -Path '%BOOST_ROOT_DIR%\boost.zip' -DestinationPath '%BOOST_ROOT_DIR%'" || exit /b 1
  del %BOOST_ROOT_DIR%\boost.zip
)
cd /d %BOOST_ROOT_DIR%\boost_1_84_0

rem (write-then-read: for /f quoting with a full python path is fragile)
"%PY%" -c "import sys; open('lv8_pyabi.txt', 'w').write(str(sys.version_info[0] * 100 + sys.version_info[1]))" || exit /b 1
set /p PYABI=<lv8_pyabi.txt
del lv8_pyabi.txt

if not exist b2.exe (
  echo [%time%] bootstrapping b2 ...
  call bootstrap.bat vc142 || exit /b 1
  rem bootstrap wrote "using msvc : 14.2" (absent on runners); drop the
  rem generated config so b2 auto-detects the real toolset (G4.394)
  del project-config.jam 2>nul
  >> project-config.jam echo using python : : "%PY%" ;
)

if not exist stage\lib\libboost_python%PYABI%-vc142-mt-s-x64-1_84.lib (
  echo [%time%] b2 stage - takes 10 to 20 min
  call b2.exe stage -j 8 address-model=64 architecture=x86 link=static runtime-link=static --with-python --with-filesystem --with-iostreams --with-date_time --with-thread || exit /b 1
)

echo [%date% %time%] === BOOST_BUILD_OK ===
dir stage\lib\*.lib | findstr /i python
exit /b 0
