@echo off
REM Transfer data from embedded system using ST-Link Debug Probe GDB Server
"c:\RTEdbg\UTIL\RTEgdbData\RTEgdbData.exe" 61234 0x20000000 0

REM Decode binary file only if no error (exit value 0)
IF %ERRORLEVEL% EQU 0 goto ShowData
REM Pause to display error message in console window
pause
goto end

REM Display the decoded information
:ShowData
Decode.bat

:end
