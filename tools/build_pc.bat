@echo off
rem Configure + build the Windows (PC) app against the port SDK (banjo-nx\sdk).
rem Usage: tools\build_pc.bat [extra cmake --build args]
rem   BUILD_DIR (env) defaults to app\out\pc-nx; REXSDK_DIR defaults to banjo-nx\sdk.
call "%~dp0pc_env.bat" || exit /b 1
set "ROOT=%~dp0.."
if "%BUILD_DIR%"=="" set "BUILD_DIR=%ROOT%\app\out\pc-nx"
if "%REXSDK_DIR%"=="" set "REXSDK_DIR=%ROOT%\sdk"
if not exist "%BUILD_DIR%\CMakeCache.txt" (
  cmake -S "%ROOT%\app" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release %REX_CMAKE_COMPILERS% -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_USE_D3D12=ON -DREXGLUE_USE_VULKAN=ON "-DREXSDK_DIR=%REXSDK_DIR%" || exit /b 1
)
cmake --build "%BUILD_DIR%" -j 14 %*
