@echo off
rem lexbor static build (G2: HTML parsing). Output: lexbor\build\Release\lexbor_static.lib
rem Env overrides: VS2022_INSTALL (probed if unset), LV8_CMAKE (cmake.exe path).
setlocal
set "REPO_ROOT=%~dp0..\.."

if defined LV8_CMAKE goto have_cmake
if defined VS2022_INSTALL goto resolve_cmake
set VS2022_INSTALL=D:\VS2022BT
:resolve_cmake
set "LV8_CMAKE=%VS2022_INSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
:have_cmake
if not exist "%LV8_CMAKE%" set LV8_CMAKE=cmake

rem Clone at the pinned revision (master carries lxb_css_selector_list_destroy_memory;
rem same pin as native/build/posix/build-all.sh - G4.394 CI).
if not exist "%REPO_ROOT%\native\lexbor\CMakeLists.txt" (
  echo cloning lexbor ...
  git clone --no-checkout https://github.com/lexbor/lexbor "%REPO_ROOT%\native\lexbor" || exit /b 1
  git -C "%REPO_ROOT%\native\lexbor" checkout 26a8819b60801301df92d771f12a01dbe8705a7a || exit /b 1
)
cd /d "%REPO_ROOT%\native\lexbor"
"%LV8_CMAKE%" -B build -G "Visual Studio 17 2022" -A x64 ^
  -DLEXBOR_BUILD_STATIC=ON ^
  -DLEXBOR_BUILD_SHARED=OFF ^
  -DLEXBOR_BUILD_TESTS=OFF ^
  -DLEXBOR_BUILD_EXAMPLES=OFF ^
  -DCMAKE_C_FLAGS="/MP" ^
  || (echo LEXBOR_CONFIG_FAILED & exit /b 1)
"%LV8_CMAKE%" --build build --config Release || (echo LEXBOR_BUILD_FAILED & exit /b 1)
echo LEXBOR_BUILD_OK
dir build\Release\lexbor_static.lib
