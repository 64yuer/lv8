@echo off
rem ============================================================
rem lv8: V8 13.1.201.22 fetch + monolith build (Windows).
rem Machine-specific paths are env-overridable so CI reuses this
rem exact recipe (see .github/workflows/ci.yml):
rem   LV8_PYTHON      python exe     (default <repo>\.venv\Scripts\python.exe)
rem   VS2022_INSTALL  VS root        (default D:\VS2022BT)
rem   GIT_EXE         git binary     (default D:\Git\cmd\git.exe)
rem   LV8_WIN_SDK     set only when the VS metadata SDK differs from the
rem                   installed SDK (local pin 10.0.26100.0)
rem   LV8_USE_PROXY   1 = route through the local 127.0.0.1:7890 proxy
rem depot_tools must NOT be on PATH (its git wrapper breaks); all tools
rem are called by absolute path.
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
set DEPOT_TOOLS_WIN_TOOLCHAIN=0

if defined LV8_PYTHON goto py_set
set "LV8_PYTHON=%REPO_ROOT%\.venv\Scripts\python.exe"
:py_set
set PY=%LV8_PYTHON%

if defined VS2022_INSTALL goto vs_probe
set VS2022_INSTALL=D:\VS2022BT
:vs_probe
rem Probe standard VS2022 editions; CI locates VS via vswhere in the
rem workflow and injects VS2022_INSTALL (cmd for /f chokes on the
rem %ProgramFiles(x86)% parens - G4.394).
if exist "%VS2022_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" goto vs_ok
for %%E in (Enterprise Professional Community BuildTools) do (
  if exist "C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VS2022_INSTALL=C:\Program Files\Microsoft Visual Studio\2022\%%E"
)
:vs_ok
if defined GIT_EXE goto git_set
set "GIT_EXE=D:\Git\cmd\git.exe"
:git_set
set V8_TAG=13.1.201.22

rem tell chromium toolchain scripts where VS lives
set vs2022_install=%VS2022_INSTALL%
set GYP_MSVS_OVERRIDE_PATH=%VS2022_INSTALL%
set GYP_MSVS_VERSION=2022

set ROOT=%REPO_ROOT%\native\stpyv8

echo [%date% %time%] === lv8 V8 build pipeline start ===
cd /d %ROOT%

rem --- 1. depot_tools ---
if not exist depot_tools (
  echo [%time%] cloning depot_tools ...
  "%GIT_EXE%" clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git depot_tools || exit /b 1
)
rem CI: the workflow bootstrap step ran the update already and left the marker
rem (the update script dies when re-called inside another batch, G4.394).
rem Local runs have no marker and self-update as before.
if not exist depot_tools\.disable_auto_update call depot_tools\update_depot_tools.bat

rem --- 2. MSVC environment (pin the SDK that is actually installed) ---
call "%VS2022_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
if not defined LV8_WIN_SDK goto sdk_ok
rem VS metadata pins SDK 10.0.22621.0 but only the LV8_WIN_SDK SDK is on
rem disk - rewrite the toolchain env paths to the real SDK, otherwise
rem setup_toolchain aborts. (cmd cannot nest variables in substitution,
rem so the local pin is literal; CI runners match their metadata and skip
rem this block entirely.)
if "%LV8_WIN_SDK%"=="10.0.26100.0" set "INCLUDE=%INCLUDE:10.0.22621.0=10.0.26100.0%"
if "%LV8_WIN_SDK%"=="10.0.26100.0" set "LIB=%LIB:10.0.22621.0=10.0.26100.0%"
if "%LV8_WIN_SDK%"=="10.0.26100.0" if defined LIBPATH set "LIBPATH=%LIBPATH:10.0.22621.0=10.0.26100.0%"
if "%LV8_WIN_SDK%"=="10.0.26100.0" set "WindowsSDKVersion=10.0.26100.0\"
:sdk_ok
echo [%time%] DEBUG INCLUDE after vcvars:
echo %INCLUDE% | findstr /i kits
echo [%time%] DEBUG WindowsSDKVersion=%WindowsSDKVersion%

rem --- 3. fetch v8 (no history) ---
if not exist v8 (
  echo [%time%] fetching v8 --no-history ...
  call depot_tools\fetch.bat --no-history v8 || exit /b 1
)

rem --- 4. pin release tag + sync deps ---
cd /d %ROOT%\v8
echo [%time%] checkout tag %V8_TAG% ...
rem Tolerate transient pack-refs lock failures; tags are local after first fetch.
"%GIT_EXE%" fetch --tags origin || echo [%time%] WARN: fetch tags failed, using existing refs
"%GIT_EXE%" checkout %V8_TAG% || exit /b 1
rem Keep DEPS-managed repos clean before sync, then re-apply fork patches after.
"%GIT_EXE%" -C %ROOT%\v8\build checkout -- . 2>nul
if not defined LV8_CI if exist "%ROOT%\..\patches\vs-toolchain-sdk.patch" (
  "%GIT_EXE%" -C "%ROOT%\v8\build" apply --reverse "%ROOT%\..\patches\vs-toolchain-sdk.patch" 2>nul
)
cd /d %ROOT%
echo [%time%] gclient sync ...
call depot_tools\gclient.bat sync -D -j8 || exit /b 1
echo [%time%] apply fork patches ...
if not defined LV8_CI if exist "%ROOT%\..\patches\vs-toolchain-sdk.patch" (
  "%GIT_EXE%" -C "%ROOT%\v8\build" apply "%ROOT%\..\patches\vs-toolchain-sdk.patch" || exit /b 1
  findstr /b "SDK_VERSION" "%ROOT%\v8\build\vs_toolchain.py"
) else (
  echo [%time%] WARN: patches\vs-toolchain-sdk.patch missing
)

rem --- 4b. ensure clang toolchain (sync hook may skip on vpython failures) ---
cd /d %ROOT%\v8
if not exist third_party\llvm-build\Release+Asserts\bin\llvm-ar.exe (
  echo [%time%] downloading clang toolchain ...
  %PY% tools\clang\scripts\update.py || exit /b 1
)
rem --- 5. gn gen + ninja ---
for /f "delims=" %%i in ('%PY% -c "import sys; sys.path.insert(0, r'%ROOT%'); import settings; print(settings.GN_ARGS)"') do set GN_ARGS=%%i
echo [%time%] gn gen with args: %GN_ARGS%
cd /d %ROOT%\v8
rem Call the CIPD-downloaded gn.exe directly; depot_tools\gn.bat forwards via an
rem extension-less python3 shim that cmd cannot execute, and its path probe
rem misresolves when .gclient sits beside the checkout.
if not exist buildtools\win\gn.exe (echo gn.exe not found & exit /b 1)
call buildtools\win\gn.exe gen out.gn\x64.release.sample --script-executable=%PY% --args="%GN_ARGS%" || exit /b 1
echo [%time%] ninja v8_monolith (this takes 1-3 hours) ...
call %ROOT%\depot_tools\ninja.bat -C out.gn\x64.release.sample v8_monolith || exit /b 1

echo [%date% %time%] === V8_BUILD_OK ===
dir out.gn\x64.release.sample\obj\v8_monolith.lib
exit /b 0
