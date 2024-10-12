@echo off
REM The ST-LINK GDB server must be started separately if it is not started automatically by the IDE.
"c:\ST\STM32CubeIDE_1.16.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.stlink-gdb-server.win32_2.1.400.202404281720\tools\bin\ST-LINK_gdbserver.exe" --swd -e --frequency 24000 --attach --shared --port-number 61234 --verbose -cp "c:\ST\STM32CubeIDE_1.16.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.1.400.202404281720\tools\bin"
pause
