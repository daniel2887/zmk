@echo off
pushd app
echo.
echo -----------------------
echo Now building Corne right
echo -----------------------
west build -d build/corne_right -b nice_nano_v2 -- -DSHIELD=corne_right -DZMK_CONFIG="../../zmk-config/config" || echo Error building Corne right && popd && exit /b
echo.
echo -----------------------
echo Now building Corne left
echo -----------------------
west build -d build/corne_left -b nice_nano_v2 -- -DSHIELD=corne_left -DZMK_CONFIG="../../zmk-config/config" || echo Error building Corne left && popd && exit /b
popd
if not exist "output" mkdir output
copy app\build\corne_left\zephyr\zmk.uf2 output\corne_left.uf2 || echo Error copying Corne right firmware && exit /b
copy app\build\corne_right\zephyr\zmk.uf2 output\corne_right.uf2 || echo Error copying Corne left firmware && exit /b