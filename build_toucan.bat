@echo off
setlocal

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
for %%i in ("%SCRIPT_DIR%..\zmk-keyboard-toucan") do set "TOUCAN_MODULE=%%~fi"
set "TOUCAN_MODULE=%TOUCAN_MODULE:\=/%"
set "TOUCAN_CONFIG=%TOUCAN_MODULE%/config"

:: Resolve repos outside my_repos (assuming they are in a sibling 'repos' directory to 'my_repos')
for %%i in ("%SCRIPT_DIR%..\..\repos\zmk-rgbled-widget") do set "RGB_MODULE=%%~fi"
set "RGB_MODULE=%RGB_MODULE:\=/%"
for %%i in ("%SCRIPT_DIR%..\..\repos\cirque-input-module") do set "CIRQUE_MODULE=%%~fi"
set "CIRQUE_MODULE=%CIRQUE_MODULE:\=/%"

:: Create output directory immediately so it's ready for the first-finisher
if not exist "%SCRIPT_DIR%output" mkdir "%SCRIPT_DIR%output"

:: Define the base build commands
set "LEFT_BUILD=west build --pristine=auto -b seeeduino_xiao_ble -d build/left -- -DSHIELD="toucan_left rgbled_adapter nice_view_gem" -DZMK_CONFIG="%TOUCAN_CONFIG%" -DZMK_EXTRA_MODULES="%TOUCAN_MODULE%;%RGB_MODULE%""
set "RIGHT_BUILD=west build --pristine=auto -b seeeduino_xiao_ble -d build/right -- -DSHIELD="toucan_right rgbled_adapter" -DZMK_CONFIG="%TOUCAN_CONFIG%" -DZMK_EXTRA_MODULES="%TOUCAN_MODULE%;%RGB_MODULE%;%CIRQUE_MODULE%""

pushd "%SCRIPT_DIR%app"

echo ---------------------------------------
echo Launching Parallel Builds...
echo ---------------------------------------

:: Launch Left: Build -> Copy -> Pause
start "Toucan Left" cmd /c "%LEFT_BUILD% && (echo. & echo Copying Left firmware... & copy build\left\zephyr\zmk.uf2 ..\output\toucan_left.uf2 /Y & echo. & echo DONE - Press any key to close window) || (echo. & echo [!] LEFT BUILD FAILED) & pause"

:: Launch Right: Build -> Copy -> Pause
start "Toucan Right" cmd /c "%RIGHT_BUILD% && (echo. & echo Copying Right firmware... & copy build\right\zephyr\zmk.uf2 ..\output\toucan_right.uf2 /Y & echo. & echo DONE - Press any key to close window) || (echo. & echo [!] RIGHT BUILD FAILED) & pause"

popd

echo.
echo Builds are running in separate windows.
echo Files will appear in 'output/' as soon as they are ready.
echo.
