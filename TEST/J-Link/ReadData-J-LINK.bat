@echo off
REM Transfer data from embedded system using J-Link Debug Probe GDB Server
"c:\RTEdbg\UTIL\RTEgdbData\RTEgdbData.exe" 2331 0x24000000 0
REM Decode.bat
pause