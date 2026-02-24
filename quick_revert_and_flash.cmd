@echo off
copy /Y .revert\main.cpp.restorepoint src\main.cpp
python -m platformio run -e cyd -t upload --upload-port COM10
echo Reverted and flashed.
