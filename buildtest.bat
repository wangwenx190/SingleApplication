@echo off
setlocal
set root_dir=%~dp0.
set binary_dir=%root_dir%\build
set source_dir=%root_dir%
call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d "%root_dir%"
if exist build rd /s /q build
md build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=ON -GNinja -B "%binary_dir%" -S "%source_dir%"
cmake --build "%binary_dir%" --config Release --target all --parallel
cd /d "%root_dir%"
endlocal
pause
exit /b 0
