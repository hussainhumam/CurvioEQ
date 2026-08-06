@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cmake -S c:\projects\CurvioEQ -B c:\projects\CurvioEQ\build\release -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_MT=c:/projects/CurvioEQ/tools/mt_ok.bat -DCMAKE_INSTALL_PREFIX=c:/projects/CurvioEQ/dist
cmake --build c:\projects\CurvioEQ\build\release --config Release
cmake --install c:\projects\CurvioEQ\build\release --prefix c:\projects\CurvioEQ\dist
