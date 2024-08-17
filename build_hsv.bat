@echo off

REM Directory strcture:
REM <top>/zmk/build_hsv.bat
REM <top>/zmk-config/...
REM <top>/<module0>/...
REM <top>/<module1>/...
REM <top>/<module2>/...
REM The following snippet captures the aboslutel path for modules' root dir
pushd ..
set modules_root=%CD%
popd

REM relative to .../zmk/app
set zmk_modules=^
%CD%\modules_hillsideview_cirque\cirque-input-module;^
%CD%\modules_hillsideview_cirque\zmk-analog-input-driver;^
%CD%\modules_hillsideview_cirque\zmk-behavior-insomnia;^
%CD%\modules_hillsideview_cirque\zmk-behavior-key-tempo;^
%CD%\modules_hillsideview_cirque\zmk-behavior-mouse-key-toggle;^
%CD%\modules_hillsideview_cirque\zmk-hid-io;^
%CD%\modules_hillsideview_cirque\zmk-input-behavior-listener;^
%CD%\modules_hillsideview_cirque\zmk-kscan-blackout;^
%CD%\modules_hillsideview_cirque\zmk-output-behavior-listener;^
%CD%\modules_hillsideview_cirque\zmk-split-peripheral-bonding-tweak;^
%CD%\modules_hillsideview_cirque\zmk-split-peripheral-input-relay;^
%CD%\modules_hillsideview_cirque\zmk-split-peripheral-output-relay

REM %modules_root%\zmk-adns9800-driver;^
REM %CD%\modules_hillsideview_cirque\zmk-pmw3610-driver;^

pushd app

REM central half first
echo.
echo -----------------------
echo Now building HSV left
echo -----------------------
west build -d build/hsv_left -b nice_nano_v2 -- -DSHIELD="hillside_view_left nice_view" -DZMK_CONFIG="../../zmk-config/config" -DZMK_EXTRA_MODULES=%zmk_modules% || echo Error building HSV left && popd && exit /b

REM peripheral half second
echo.
echo -----------------------
echo Now building HSV right
echo -----------------------
west build -d build/hsv_right -b nice_nano_v2 -- -DSHIELD="hillside_view_right nice_view" -DZMK_CONFIG="../../zmk-config/config" -DZMK_EXTRA_MODULES=%zmk_modules% || echo Error building HSV right && popd && exit /b

popd
if not exist "output" mkdir output
copy app\build\hsv_left\zephyr\zmk.uf2 output\hsv_left.uf2 || echo Error copying HSV right firmware && exit /b
copy app\build\hsv_right\zephyr\zmk.uf2 output\hsv_right.uf2 || echo Error copying HSV left firmware && exit /b