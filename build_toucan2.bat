@echo off
setlocal

:: Prevent drive letter G macro expansion conflicts in Zephyr v3.5.0 LVGL
set "CFLAGS=-ULV_CONF_PATH"
set "CXXFLAGS=-ULV_CONF_PATH"

:: Get the directory where the script is located (absolute path with trailing backslash)
set "SCRIPT_DIR=%~dp0"

:: Activate virtual environment
if exist "%SCRIPT_DIR%.venv\Scripts\activate.bat" (
    echo [INFO] Activating virtual environment...
    call "%SCRIPT_DIR%.venv\Scripts\activate.bat"
) else (
    echo [!] ERROR: Virtual environment not found at %SCRIPT_DIR%.venv
    echo [!] Please ensure the virtual environment is set up.
    exit /b 1
)

:: Resolve absolute paths to dependencies using SCRIPT_DIR
:: We convert backslashes to forward slashes for compatibility with Zephyr/West
for %%i in ("%SCRIPT_DIR%..\zmk-keyboard-toucan2") do set "TOUCAN_MODULE=%%~fi"
set "TOUCAN_MODULE=%TOUCAN_MODULE:\=/%"
set "TOUCAN_CONFIG=%TOUCAN_MODULE%/config"

:: Resolve repos outside my_repos (assuming they are in a sibling 'repos' directory to 'my_repos')
for %%i in ("%SCRIPT_DIR%..\..\repos\zmk-rgbled-widget") do set "RGB_MODULE=%%~fi"
set "RGB_MODULE=%RGB_MODULE:\=/%"
for %%i in ("%SCRIPT_DIR%..\..\repos\zmk_driver_azoteq") do set "AZOTEQ_MODULE=%%~fi"
set "AZOTEQ_MODULE=%AZOTEQ_MODULE:\=/%"
for %%i in ("%SCRIPT_DIR%..\..\repos\zmk-input-zoom") do set "ZOOM_MODULE=%%~fi"
set "ZOOM_MODULE=%ZOOM_MODULE:\=/%"
for %%i in ("%SCRIPT_DIR%..\..\repos\zmk-pointing-acceleration") do set "ACCEL_MODULE=%%~fi"
set "ACCEL_MODULE=%ACCEL_MODULE:\=/%"

:: Create output directory immediately so it's ready for the first-finisher
if not exist "%SCRIPT_DIR%output" mkdir "%SCRIPT_DIR%output"

:: Define the base build commands
set "EXTRA_MODULES=%TOUCAN_MODULE%;%RGB_MODULE%;%AZOTEQ_MODULE%;%ZOOM_MODULE%;%ACCEL_MODULE%"
set "LEFT_BUILD=west build --pristine=auto -b seeeduino_xiao_ble -d build/left -- -DSHIELD="toucan_left rgbled_adapter nice_view_gem" -DZMK_CONFIG="%TOUCAN_CONFIG%" -DZMK_EXTRA_MODULES="%EXTRA_MODULES%""
set "RIGHT_BUILD=west build --pristine=auto -b seeeduino_xiao_ble -d build/right -- -DSHIELD="toucan_right rgbled_adapter" -DZMK_CONFIG="%TOUCAN_CONFIG%" -DZMK_EXTRA_MODULES="%EXTRA_MODULES%""


pushd "%SCRIPT_DIR%app"

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=both"

echo ---------------------------------------
echo Launching Builds (%TARGET%)...
echo ---------------------------------------

if /i "%TARGET%"=="left" goto build_left_sync
if /i "%TARGET%"=="right" goto build_right_sync
if /i "%TARGET%"=="both" goto build_both

echo [!] Unknown target: %TARGET%
echo Usage: build_toucan2.bat [left^|right^|both]
popd
exit /b 1

:build_both
:: Launch Left: Build -> Copy -> Pause
start "Toucan 2 Left" cmd /c "%LEFT_BUILD% && (echo. & echo Copying Left firmware... & copy build\left\zephyr\zmk.uf2 ..\output\toucan2_left.uf2 /Y & echo. & echo DONE - Press any key to close window) || (echo. & echo [!] LEFT BUILD FAILED) & pause"

:: Launch Right: Build -> Copy -> Pause
start "Toucan 2 Right" cmd /c "%RIGHT_BUILD% && (echo. & echo Copying Right firmware... & copy build\right\zephyr\zmk.uf2 ..\output\toucan2_right.uf2 /Y & echo. & echo DONE - Press any key to close window) || (echo. & echo [!] RIGHT BUILD FAILED) & pause"

popd

echo.
echo Builds are running in separate windows.
echo Files will appear in 'output/' as soon as they are ready.
echo.
exit /b 0

:build_left_sync
echo Building Left...
%LEFT_BUILD%
if %errorlevel% neq 0 (
    echo.
    echo [!] LEFT BUILD FAILED
    popd
    exit /b 1
)
echo.
echo Copying Left firmware...
copy build\left\zephyr\zmk.uf2 ..\output\toucan2_left.uf2 /Y
echo.
echo DONE
popd
exit /b 0

:build_right_sync
echo Building Right...
%RIGHT_BUILD%
if %errorlevel% neq 0 (
    echo.
    echo [!] RIGHT BUILD FAILED
    popd
    exit /b 1
)
echo.
echo Copying Right firmware...
copy build\right\zephyr\zmk.uf2 ..\output\toucan2_right.uf2 /Y
echo.
echo DONE
popd
exit /b 0
