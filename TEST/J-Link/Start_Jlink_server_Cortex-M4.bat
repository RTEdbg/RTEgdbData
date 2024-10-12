@echo off
REM Start the J-Link GDB server and connect to the ARM Cortex CPU without stopping it.
:start
"C:\Program Files\SEGGER\JLink\JLinkGDBServerCL.exe" -device Cortex-M4 -if SWD -nohalt -noir -noreset -Speed 15000 -silent
if errorlevel 0 goto start
pause