@echo off
rem ============================================================
rem lv8: build the _STPyV8 extension in place.
rem usage: build-ext.cmd [python-dir]
rem   [python-dir]  target ABI's python dir containing python.exe
rem                 (defaults to the repo venv; pass e.g. D:\py312 to
rem                 build another ABI -- build-boost-abi.cmd must have
rem                 staged that ABI's boost_python lib first)
rem Env overrides: VS2022_INSTALL (VS root, probed if unset).
rem ============================================================
setlocal
set "REPO_ROOT=%~dp0..\.."

if "%~1"=="" (
  set "PYDIR=%REPO_ROOT%\.venv\Scripts"
) else (
  set "PYDIR=%~1"
)
set "PY=%PYDIR%\python.exe"

if not defined VS2022_INSTALL set VS2022_INSTALL=D:\VS2022BT
if exist "%VS2022_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" goto vs_ok
for %%E in (Enterprise Professional Community BuildTools) do (
  if exist "C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VS2022_INSTALL=C:\Program Files\Microsoft Visual Studio\2022\%%E"
)
:vs_ok
call "%VS2022_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

set "BOOST_ROOT=%REPO_ROOT%\native\boost\boost_1_84_0"
set "Python_ROOT_DIR=%PYDIR%"
set DISTUTILS_USE_SDK=1
cd /d "%REPO_ROOT%\native\stpyv8"
%PY% setup.py build_ext --inplace
if errorlevel 1 exit /b %errorlevel%
echo EXT_EXIT=0
dir *.pyd 2>nul | findstr pyd
%PY% -c "import sysconfig; open('lv8_ext_suffix.txt', 'w').write(sysconfig.get_config_var('EXT_SUFFIX'))" || exit /b 1
set /p EXT_SUFFIX=<lv8_ext_suffix.txt
del lv8_ext_suffix.txt
%PY% "%REPO_ROOT%\native\build\package-artifact.py" ^
  --extension "%CD%\_STPyV8%EXT_SUFFIX%" ^
  --icu "%CD%\stpyv8-icu\icudtl.dat" ^
  --out-dir "%REPO_ROOT%\dist-windows" ^
  --stpyv8 "%CD%"
if errorlevel 1 exit /b %errorlevel%
