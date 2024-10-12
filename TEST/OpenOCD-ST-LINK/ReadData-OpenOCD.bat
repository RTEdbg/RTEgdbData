@echo off
REM Transfer data from embedded system using OpenOCD Debug Probe GDB Server
"c:\RTEdbg\UTIL\RTEgdbData\RTEgdbData.exe" 3333 0x24000000 0
REM Decode.bat
pause