# Utility for transfer of log data from an embedded system via GDB server

The **RTEgdbData** utility transfers binary log data from embedded systems using the GDB server TCP/IP protocol, allowing easy data retrieval from embedded systems even for debug probes that lack a dedicated command line data transfer tool. Examples of some popular debug probes are shown below. The RTEgdbData utility is part of the [**RTEdbg logging/tracing toolkit**](https://github.com/RTEdbg/RTEdbg). It is included in the **[RTEdbg toolkit distribution ZIP](https://github.com/RTEdbg/RTEdbg/releases)** file - see the ***RTEdbg\UTIL*** folder.

The GDB server software is either part of the Debug Probe software or a separate package, as in the case of [OpenOCD](https://openocd.org/doc/html/GDB-and-OpenOCD.html). The IDE automatically starts the GDB server when we start debugging the code. The programmer can start the GDB server with custom settings and set the debugger in the IDE to use the already started GDB server. When testing without a debugger, the programmer must start the GDB server himself. Check your debug probe documentation to see how to do this. See **[TEST](https://github.com/RTEdbg/RTEgdbData/tree/master/TEST)** folder of the repository for examples of batch and GDB server configuration files. See the RTEgdb demo code for more examples.

## Table of contents
* [Introduction](#Introduction)
* [RTEgdbData Command Line Arguments](#rtegdbdata-command-line-arguments)
* [Multiple Data Transfers in the Persistent Mode of Operation](#multiple-data-transfers-in-the-persistent-mode-of-operation)
* [How to Obtain the Address of the Data Logging Structure](#how-to-obtain-the-address-of-the-data-logging-structure)
* [Send Commands to the GDB Server after Connecting to it](#send-commands-to-the-gdb-server-after-connecting-to-it)
* [Issues with some GDB Servers and Workarounds](#issues-with-some-gdb-servers-and-workarounds)
* [Logging Data Structure Initialization without the rte_init() Function](#logging-data-structure-initialization-without-the-rte_init-function)
* [Common Debug Probe Examples](#common-debug-probe-examples)
* [&nbsp; &nbsp; &nbsp; Segger J-Link](#segger-j-link)
* [&nbsp; &nbsp; &nbsp; STMicro ST-LINK](#stmicroelectronics-st-link)
* [&nbsp; &nbsp; &nbsp; OpenOCD](#openocd---open-on-chip-debugger)
* [&nbsp; &nbsp; &nbsp; PE Micro Multilink](#pe-micro-multilink)
* [&nbsp; &nbsp; &nbsp; ESP32 JTAG](#esp32-jtag-debugging)
* [How to Contribute or Get Help](#how-to-contribute-or-get-help)

## Introduction
This utility stops data logging by setting the message filter to zero (if not already stopped by the firmware), transfers the data to the host (writes it to a binary file), and then restarts data logging by restoring the message filter value. The firmware runs normally during the data transfer, but the messages are not logged during this time. They are discarded rather than written to the logging buffer. <br>
**Note:** The following descriptions assume that the reader is familiar with the basic features of RTEdbg toolkit.

The RTEgdbData utility automatically detects the current data logging mode (post-mortem or single shot) and restarts logging when the data transfer is complete. It can be used to:
* **Get snapshots of a running system:** Logging is only paused during data transfer while the code is running normally. When the data transfer to the host is complete, logging is enabled again.
* **Get single-shot data:** In single-shot mode, the firmware stops logging when the logging buffer is full. Single-shot mode logging is automatically restarted when the data transfer to the host is complete. Automatic restart can be disabled by setting the filter to zero with the command line argument "-filter=0". In this case, the firmware can enable data logging by setting the message filter to a non-zero value, e.g. using a software trigger.
* **Get post-mortem data:** Transfer data after the firmware stops logging, e.g., due to a fatal system or application error.

When the utility is started, data can be transferred once or several times in a row in persistent mode. It is also possible to run a batch file to automate the decoding and display of binary logged data after it is transferred to the host.

**How the RTEgdbData utility works:** The utility connects to the debug probe's GDB server via TCP/IP protocol and reads the logged data from the embedded system memory using GDB commands. The utility sets the message filter to zero to temporarily stop logging. It resets the logging buffer index if single-shot logging was performed, and clears the logging buffer if requested by the '-clear' command line argument (sets the entire logging buffer to 0xFFFFFFFF indicating that the buffer is empty).

**Using RTEgdbData in parallel with the IDE's built-in debugger:**  Most GDB servers have only two parallel channels enabled, the first one for general debugging and the second one for the Live Variable Viewer. An additional channel is required for the RTEgdbData utility. If it is not possible to set a larger number of channels, then the Live Variable View functionality must be disabled in order to be able to transfer data to the host with the RTEgdbData utility. See also [Issues with some GDB Servers and Workarounds](#issues-with-some-gdb-servers-and-workarounds).

**Notes:** 
1. For many debug probes it is possible to start the GDB server so that the server does not reset the embedded system. This makes it possible for a debug probe to be connected and for data to be transferred while the system is already in operation (without to interrupt the code execution).
3. For your debug probe, check if the GDB server can work in parallel with the debugger. This may be possible even if the IDE debugger does not use the GDB server, but communicates directly with the probe.
4. Automatic retry of the data transfer in the event of an error during the data transfer is not integrated into the program. The user has to restart the utility or batch file. <br>
**Caution:** In the event of an error in the middle or RTEgdbData execution, the message filter value may remain at 0, which means that logging is disabled. To re-enable it, the filter must be set to a value other than 0 using the debugger or the command line option '-filter=value'.
5. This utility has been tested (so far) with J-Link GDB Server (ARM Cortex-M), ST-LINK GDB Server and OpenOCD GDB Server (on ST-LINK for ARM Cortex-M and JTAG ESP32). If you encounter any problems, please check the GDB server settings first and report both the problems and workarounds (if found). OpenOCD is very flexible but not easy to configure. Use the preconfigured CFG files and modify them to your needs. They are usually part of the IDE installation.
6. The utility is equipped with many checks and reports its actions and detected problems to the screen or to the log file. This makes it possible to pinpoint the problem if it occurs in practice. By default, it writes to the console window, but if this bothers you, the output can be redirected to a log file (see the '-log=' switch). The -debug argument enables additional verbosity. Use it together with the -log argument when reporting a problem (include the log file).
7. In case of data access problems in the microcontroller, GDB servers report errors such as E01, E31, etc. The meaning of these errors is not standardized, but they mean e.g. that the debugger has no access to memory, that there is no memory at the address from which we want to read data, etc. Some GDB servers do not report certain types of errors (e.g. writing to a non-existent part of memory) at all.
7. The GDB serial port protocol is not supported in this release. Therefore, debug probes such as the Black Magic cannot be used. For debug probes that are not supported, you can save the contents of the data logging structure to a file via the IDE - see an example in the RTEdbg manual - section *Save Embedded System Memory to a File Using an Eclipse IDE*.

See the **RTEdbg Manual** for information on how to transfer data to the host using command line utilities for the J-Link or ST-LINK debug probes (section *'Using a Debug Probe to Transfer Data to a Host'*). See also the description *'Save Embedded System Memory to a File Using an Eclipse IDE'*.

<br>

## RTEgdbData command line arguments
**RTEgdbData** &nbsp; **port_number &nbsp; address &nbsp; size &nbsp; \<Options\>**
* **port_number** - GDB server port number
* **hex_address** - Address of the *g_rtedbg* data logging structure
* **size** - Size of *g_rtedbg* data logging structure (0 - get the size from *g_rtedbg* structure header automatically)
<br> The address and size must be hexadecimal and divisible by 4.

The utility transfers data from the embedded system and writes it to the ***data.bin*** file or to a file specified with the '-bin=' argument. If data logging in post-mortem mode has been stopped by the firmware (the firmware has set the message filter to zero), logging will not be resumed after the data transfer is complete unless requested by the '-filter=value' command line argument. See the description of **[Multiple data transfers](#multiple-data-transfers-in-the-persistent-mode-of-operation)** if you want to transfer data from the embedded system several times in a row.
<br>

**Return value:** Returns 0 on success and 1 if data could not be fetched from the embedded system or written to the binary file.

#### **Options:**
* **-bin=file_name** - Defines the output file where the binary data will be written (default = *data.bin*).

* **-filter=hex_value** - This value is written to the message filter at the end of a successful data transfer. <br>
The filter value must be zero if data logging is not to be restarted immediately, for example, if a software trigger is used to start single shot data logging.
<br>If the filter value is not defined, the previous message filter value is restored. When logging in single shot mode, a copy of the message filter is entered into the message filter after the data transfer. See the RTEdbg Manual section *"Create a Batch File or Firmware for Data Transfer to a Host"* for the details.

* **-filter_names=file_name** - The path to the *Filter_names.txt* file in the project, if the names of the filters currently enabled in the embedded system should also be printed when the header data of the logging structure is printed.

* **-clear** - Clear the logging logging buffer (set the entire logging buffer to 0xFFFFFFFF). It is not necessary to clear the logging buffer after the data transfer to the host is done when using single shot data logging or post-mortem debugging. <br>

* **-p** - Make the RTEgdbData persistent to enable multiple data transfers. See the **[Persistent mode](#multiple-data-transfers-in-the-persistent-mode-of-operation)** description.

* **-delay=xx** - Delay in ms after logging is stopped by setting the message filter to zero. In multitasking embedded systems, logging in one task may be interrupted by logging in another task with a higher priority. In such a case, a lower priority task will not finish writing data until it gets CPU time again. This option adds a pause before reading the data to allow the interrupted task to finish logging. See the RTEdbg Manual section *RTOS Task Starvation*.

* **-ip=address** - GDB server IP address - use decimal values (default is *localhost* = 127.0.0.1).

* **-log=file_name** - The name of the file in which operation and error messages are logged (default = print to console window).

* **-start=file_name** - The name of the command file containing commands that RTEgdbData sends to the GDB server after starting. See a detailed description in **[Send commands to the GDB server after connecting to it](#send-commands-to-the-gdb-server-after-connecting-to-it)**.

* **-detach** - Send the detach command to the GDB server before the RTEgdbData utility disconnects from the GDB server. If this option is not defined, RTEgdbData simply disconnects from the GDB server. Disconnecting from the GDB server usually resumes execution of the target, but the results depend on the particular GDB server implementation. For example, ST-LINK GDB Server resumes execution of embedded system code after the execution has been stopped, e.g. by a breakpoint in the IDE.

* **-decode=file_name** - The name of the batch file used to decode the binary data after the data transfer is complete. Can also be used to start viewing the decoded data (e.g. CSV file graphing). The batch file must terminate to enable the RTEgdbData utility to continue execution. Use the 'start' commands in a batch file while starting applications that do not terminate. See the description in [Start a batch script in a separate Command Prompt window](https://ss64.com/nt/start.html).

* **-debug** - Also prints to the log file all messages that RTEgdbData sends to and receives from the GDB server. Use it together with the -log argument when reporting a problem.

* **-priority** - Enable high priority execution for the RTEgdbData and the debug probe servers (if the server names are given with the *-driver* argument). If the RTEgdbData is executed with admin privileges the real-time priority is enabled. A higher process priority is useful when logging data is transferred frequently (e.g., data streaming to the host). Windows is not a real-time operating system, and a high priority (even a real-time priority) does not guarantee that the operating system will always give CPU time to the processes involved in transferring data to the host when they need it. However, enabling a higher execution priority greatly reduces the likelihood of an extended pause during the download of the logging data structure from the embedded system, because the Windows operating system temporarily puts the process on hold.

* **-driver=name** - Define the name of the application (e.g. GDB server, debug probe server) for that the execution priority should be elevated also.  Enter just the file name and not the full pathname. Increasing the priority of only the RTEgdbData process is not very helpful because most of the data transfer time is spent in the servers. With this command line argument, we tell which processes should be prioritized so that they are more likely to get processor time when they need it. <br>
**Note:** Can be used multiple times, e.g. if the ST-LINK server is used for the ST-LINK debug probe, typically *stlinkserver.exe* and *ST-LINK_gdbserver.exe* are involved and both names should be defined with separate arguments.

* **-msgsize=xxx** - Set the maximum message size received from the GDB server. <br>
Manually set the maximum message size to be received by the RTEgdbData utility from the GDB server. The same value as reported by the GDB server (server capabilities) is used by default. In general, a larger block size allows for higher transfer speeds and reduces the possibility that the transfer of large amounts of data from the embedded system will be interrupted by switching Windows operating system processes - for example, when the data structure for data logging needs to be transferred in several pieces. In practice, the difference is only relevant for streaming data transfers.
<br>
**Caution:** Different GDB servers support different maximum data transfer sizes from the embedded system. This applies not only to servers for different debug probes, but may also depend on the version of the server. The GDB server may crash if too large a block of memory is requested.

**Hint:** The settings for starting the GDB server can be obtained from the IDE, e.g. with the command "Show comandline" in STM32CubeIDE (Debugger window). Some IDEs also generate a configuration file for OpenOCD that can be used as a basis for your own configuration.

<br>

## How to Obtain the Address of the Data Logging Structure
The address of the data structure in which the data is logged is the RTEgdbData command line argument and must be known by the programmer or tester. The RTEdbg manual describes how to link the *g_rtedbg* logging data structure so that it is always at the same address. If this is not possible or practical, the easiest way is to find the address in the map file. Below are snippets of the map files generated by the GNU C, Keil MDK and IAR EWARM toolchains. If the linker doesn't generate the map file by default, add the appropriate option.
```
---------------- GNU C ----------------------
.RTE            0x0000000024000000     0x2028
 *(RTEDBG)
 RTEDBG         0x0000000024000000     0x2028 ./Core/RTEdbg/rtedbg.o
                0x0000000024000000                g_rtedbg
 *(RTEDBG*)

--------------- Keil MDK -------------------
 g_rtedbg                       0x24000000   Data        8232  rtedbg.o(RTEDBG)

--------------- IAR EWARM ------------------
g_rtedbg                0x2400'0000  0x2028  Data  Gb  rtedbg.o [5]
```

Open the *'\*.map'* file and look for g_rtedbg. The data structure is at address 0x2400000000 and the size is 0x2028 in this example. It is not necessary to know the size of the data structure, because if the length is specified as 0, the program itself reads it from the header of the data structure.

<br>

## Multiple data transfers in the persistent mode of operation
With some GDB servers, the code execution in the embedded system stops at the moment when the RTEgdbData utility connects to the server via the TCP/IP protocol. If this cannot be avoided with the GDB server settings, first load the code in the embedded system microcontroller and stop the code execution, e.g. with a breapoint at the beginning of the *main()* function. Then start RTEgdbData in persistent mode with all necessary command line arguments. Then start the code execution with the debugger and start the data transfer and decoding with the commands for RTEgdbData (with the keys). Since the utility is already started, the code execution in the embedded system does not stop during the data transfers. Interrupted execution can also be automatically resumed - see description in [Send commands to the GDB server after connecting to it](#send-commands-to-the-gdb-server-after-connecting-to-it). In this case, the pause in code execution can be as little as a few milliseconds.

**Caution:** The RTEgdbData must be started before the non-interruptible functions are started. These are functions that, if interrupted, could cause damage to the control module or the system it controls.

**List of available commands**
|Key|Description|
|:---:|:-----------|
| **Space** | Start data transfer to the host. Data logging is paused (message filter is set to zero) during the data transfer. The filter value is restored after the transfer, and logging resumes.  If the -decode=decode_batch_file argument is defined the batch file is started after the data is written to the file. <br> In the case of single shot logging, logging is completely restarted. If the -clear argument is specified, the logging buffer is cleared before the restart.|
| **F** | Set new message filter value. Use it to select different sets of message groups to log, enable or disable logging, etc. <br> The previously set value is displayed between the parentheses. After starting the utility, the old value is what was set with the -filter argument or 0 if this argument was not set. If you simply press Enter, the value does not change. After entering a new value, or simply pressing Enter to maintain the current value, it typically takes only a few milliseconds for the value to be transmitted to the embedded system. Therefore, you can also use this function to manually trigger logging, or manually disable logging if the new filter value is zero. The -1 value can also be used to set the filter to 0xFFFFFFFF. |
| **S** | Switch to single shot logging mode and restart logging. Only possible if RTE_SINGLE_SHOT_ENABLED is set to 1 in the config file. <br> If the message filter value is zero, the firmware must enable logging, e.g. after a software trigger. |
| **P** | Switch to post-mortem logging mode and restart logging. |
| **R** | Reconnect to the GDB server (e.g., after the GDB server has been restarted). <br> Example: When testing in the IDE, if we trigger a recompilation of the modified code and reload it onto the embedded system, the IDE stops the GDB server and then restarts it. As a result, the **RTEgdbData** program loses its connection to the GDB server and must reconnect once the GDB server has restarted. |
| **0** | Restart the batch file defined with the -start=cmd_file argument - e.g. to reinitialize data logging after a CPU reset. |
| **1 ... 9** | Start the command file ***1.cmd*** ... ***9.cmd*** &Rightarrow; Send commands to the GDB server or to embedded system through the GDB server. <br> Use e.g to set values of embedded system variable(s) for various tests, generate disturbances, etc., and then log data about their effects on the system. |
| **B** | Benchmark data transfer speed. Use it to evaluate how much data can be transferred from the embedded system per second with the connected debug probe. The report is written to the *speed_test.csv* file and a summary is written to the console. Setting the *-priority* and *-server* command line arguments affects the consistency of data transfers. This typically greatly reduces the likelihood that the operating system will not allocate CPU time when one of the processes involved in the data transfer needs it. |
| **H** | Load the data logging structure header from the embedded system and display information. <br> Use e.g. to check if the correct address of the logging data structure has been set, display a list of enabled message filters, check if *rte_init()* has already been called to initialize the logging data, etc. |
| **L** | Enable / disable logging to the log file. <br> If the logging of information about operation and errors to the log file is enabled, only the most basic information about what the program is doing will be displayed on the screen. If we want to monitor the information in the console window (on the screen) more closely in case of data transfer problems or communication problems with the GDB server, we can use this function to temporarily enable the display of all information on the screen. By pressing the L key again, we will disable it again and the data will be written to the log file again (the old content of the log file will be overwritten). |
| **?** | Display a list of available commands. |
| **Esc** | Exit |
| Ctrl-C | Pressing Ctrl-C while a console application is running will terminate it also. |
| | |

While waiting for a keystroke, the software prints the value of the index in the logging buffer and the message filter. Filter 0 indicates, for example, that the firmware has stopped logging due to a software trigger or that logging has not yet been started. When logging in single shot mode, it also displays information about what percentage of the log buffer is already filled with messages. <br>

An index that does not change indicates, for example, that code execution has stalled, e.g. because an exception or breakpoint has been triggered. If the index stops incrementing in single shot logging mode and is right at the end of the buffer (very close to the value of RTE_BUFFER_SIZE), this is a sign that data has already been captured and single shot logging has stopped. The buffer fill level (in percent) is also displayed in single shot mode.
<br>

There can be several reasons for the \"Cannot read data from the embedded system.\" message to appear on the screen. The reasons can be as follows: the connection to the GDB server or via the debug probe to the embedded system has failed, the embedded system has gone into sleep mode, etc. The GDB server does not report any details. It is recommended not to use the persistent mode of communication (argument -p) for the first data transfers from the embedded system, but to use a one-time data transfer, because errors will be reported in more detail if they occur.

<br>

## Send commands to the GDB server after connecting to it

The **-start=command_file** option enables execution of GDB commands immediately after the RTEgdbData utility connects to the GDB server. These commands can be used, for example, to send data to the embedded system CPU, to initialize the data logging structure if the *rte_init()* function is not used in a resource-constrained system, or to configure the GDB server, e.g. if not everything could be achieved with the GDB server start parameters. See the **[GDB protocol](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Remote-Protocol.html)** for a list of available commands.

**GDB Server command examples:**
* **"set mem inaccessible-by-default off"** - Any memory area that is not in the memory map is inaccessible. This can be changed with this command.
* **"Maddr,length:XX…"** - Write length bytes of memory starting at address addr. The XX... is the data to be written. <br>
Example: "M24000010,4:12345678" - Write a four byte value of 0x78563412 to address 0x24000010. The value bytes are reversed because the processor is little-endian here. <br>
Note: Such commands can be used to reconfigure the embedded system for testing purposes - for example, to prevent the processor from turning off the CPU clock in sleep mode, thereby disabling debugger access.
* **"R XX"** - Reset the CPU. The value XX (e.g. 00), while needed, is ignored.
* **"vCont;c"** or **"c"** - Continue code execution at the current location. <br>
Example: Start the CPU if it would be stopped after the RTEgdbData connects to the GDB server.
<br> Use this as a last resort. First try to set the GDB server settings so that the code execution is not paused after attaching to the GDB server, e.g. gdb-attach { .... } for the OpenOCD GDB server (definition of what should happen after connecting to the GDB server) or contact the support of your debug probe vendor.

**The following additional comands are available:**
* **#delay xxx** - Wait xxx milliseconds before executing the next command. Data received from the GDB server during the pause will be discarded. It can be used, for example, to add a delay after commands that require more time to execute, such as Reset.
* **#init config_word timestamp_frequency** - The config_word is a hex and timestamp frequency a decimal value. This command can be used to initialize the logging data structure. Refer to description [Logging data structure initialization without the rte_init() function](#logging-data-structure-initialization-without-the-rte_init-function) for a detailed description..
* **#echo text** - Print the **text** to the console window.
* **#filter hex_value** - Set the message filter to **hex_value**.
* **## Comments** - The **##** at the beginning of a line indicates a comment.

Blank lines are allowed in the command files.

**Note:** Command files can be started by pressing the keyboard keys in [persistent mode](#multiple-data-transfers-in-the-persistent-mode-of-operation). See the description of the '0' ... '9' keys.

<br>

## Logging data structure initialization without the rte_init() function

The *rte_init()* function is not needed if the *g_rtedbg* data structure is initialized by the debug probe at the beginning of the code test - e.g., after the breakpoint at the *main()* function is hit. Omitting this function reduces the program memory consumption on resource-constrained systems, e.g. by about 160 bytes in the DEMO_STM32L053 folder. 

Below is a description of how to implement this.

1. Do not call the *rte_init()* function in your code.
2. Prepare the RTEgdbData *cmd_file* - see below. It must contain timestamp initialization (e.g. several **"Maddr,length:XX..."** memory set GDB commands) and **#init config_word timestamp_frequency** to initialize the *g_rtedbg* data structure. Other commands can be added to the *cmd_file* as needed.
<br> The *config_word* must have the same value that would be written to the *g_rtedbg.rte_cfg* variable if the *rte_init()* function were executed. <br> Note that bit 0 of the config word must be set to 1 to enable single shot logging if required (the single shot mode must be enabled in the *rtedbg_config.h* header file and the bit 0 must also be set). <br> The *timestamp_frequency* is the clock frequency of the timer used for the timestamps. 
<br> The **#init** command initializes the data logging structure. It sets the structure header (index, filter, etc.) and erases logging buffer (sets all entries to 0xFFFFFFFF). <br>
The long timestamp working variable must also be cleared if there are long timestamps and no 64-bit or at least 48-bit timer is available for timestamping (t_stamp.l = t_stamp.h = 0;). <br>
**Hint:** Step through the *rte_init_timestamp_counter()* function before disabling the *rte_init()* function and implement the same functionality with the GDB memory set (Maddr,...) commands. Enable instruction stepping mode to see all memory operations of the timestamp timer initialization.
3. The **-filter=value** argument defines the initial filter value. If not defined, the initial value is zero and the firmware has to set the message filter with the *rte_set_filter()* function - e.g. after a trigger. <br>
3. Configure debugging in your IDE to stop executing the code after the code is downloaded and the *main()* function is reached. Then start **RTEgdbData** with a batch file (add appropriate command line arguments). The arguments **address** and **size** must be set according to the address and size of the *g_rtebdg* structure (size in bytes = 40 + 4 x RTE_BUFFER_SIZE). The argument **-start=cmd_file** defines the name of the file containing the commands to be executed after the start of the RTEgdbData utility.
<br>**Note:** The batch file must include the *-p* argument to enable the [persistent mode](#multiple-data-transfers-in-the-persistent-mode-of-operation). RTEgdbData utility will wait for your commands (keystrokes).
4. Start the code execution with the debugger. Data logging starts immediately if the filter value set with the -filter=value argument is non-zero, otherwise the firmware must set the filter after a software trigger.
5. Use RTEgdbData utility commands (keyboard shortcuts) to transfer data from embedded system, change message filter, etc.

**Note:** See how to use this in the *\"c:/RTEdbg/Demo/STM32L053/TEST RTEgdbData/Testing without rte_init\"* RTEdbg demo folder (included in the RTEdbg distribution ZIP file).

<br>

## Issues with some GDB Servers and Workarounds

The GDB protocol was the obvious choice for RTEdbg's initial data transfer tool because of the widespread support for GDB servers. However, establishing proper connections to these servers proved to be unexpectedly complex, as not all servers behave as expected. Some GDB servers, such as OpenOCD, can be configured in terms of what should happen after connecting to the embedded system, but most are not freely configurable. It is recommended that you first test what happens during data transfers to the host with RTEgdbData while using your debug probe and its GDB server. The code execution should not reset or stop the processor. For very demanding projects, it is recommended to use the J-Link debug probe if your CPU is supported, as no problems have been observed with its GDB server.

For post-mortem analysis, it does not matter if the execution stops or the processor is reset when the GDB server or the RTEgdbData utility is started or when the debug probe is connected, since in these cases the code execution is already stopped. The only important thing is that the data is preserved and can be transferred to the host. However, if we want to get the data from a running system, we must use such GDB server settings that ensure continuous execution of the code in the embedded system without interruption.

For most debug probes only two channels are usually enabled for the GDB server. One is for normal commands to control code execution, and the other is for the so-called Live View functionality, which allows viewing and modifying variables during code execution. If it is not possible to set a larger number of channels, then the Live View functionality must be disabled in order to be able to transfer data to the host with the RTEgdbData utility.

See the description of **[Multiple Data Transfers in the Persistent Mode of Operation](#multiple-data-transfers-in-the-persistent-mode-of-operation)**. This is a partial workaround for debug probes where the processor stops execution when RTEgdbData connects to the GDB server. If the stop occurs, for example, immediately after the code has been loaded and started, the debugger can continue execution and the code will not be interrupted during individual data transfers.
The RTEgdbData must be started before the non-interruptible functions are started. These are functions that, if interrupted, could cause damage to the control module or the device it controls.

**Note:** See also the **[RTEcomLib](https://github.com/RTEdbg/RTEcomLib)** and **[RTEcomData](https://github.com/RTEdbg/RTEcomData)** repositories for transferring logged data via a serial channel. This library of functions and utilities enables data transfer even in cases where either the debug probe cannot be connected during testing or simultaneous transfer with RTEgdbData and use of the debugger in the IDE is not possible.

Below is a list with a brief description of the problems. Workarounds are listed in the description of each debug probe.

* **Segger J-Link**: No known issues regarding the GDBserver in combination with the RTEgdbData utility. The functionality of the Live Variable Viewer does not need to be turned off. The GDB server and debug probe behave as expected and the default setup is usually sufficient - e.g. the options "-nohalt -noir -noreset" if the GDB server should not stop the running system when the probe is connected or the GDB server is started.

* **ST-LINK**: The GDB server stops the code execution in the embedded system as soon as RTEgdbData connects to it via the TCP/IP protocol. Code execution is stopped before RTEgdbData sends a command via the GDB protocol. A partial workaround is available - see below.

* **OpenOCD for ST-LINK**: No known problems with the RTEgdbData utility if the OpenOCD GDB server is properly configured. See the OpenOCD on ST-LINK example below.

* **OpenOCD for JTAG on ESP32**: <br>
The GDB server stops the code execution in the embedded system as soon as RTEgdbData connects to it via the TCP/IP protocol. No workaround has been found yet.

||
|:--------------|
|**Note:** The RTEgdbData utility supports only the part of the GDB communication protocol that is needed to transfer data to and from the embedded system. After sending the request, it expects a response from the server (data from embedded system or confirmation that the data has been received). Usually, this is sufficient for data transfers from the embedded system if the code in it is running normally. The GDB server also sends data without a request when a special event occurs, such as a reset, a triggered breakpoint, or an exception. Handling of such messages is implemented in a very limited way. |
| If you find a workaround for the problems mentioned in this document, or for any other debug probes and GDB servers, we will be happy to add it to this document (or add a link to your description).|
<br>

## Common Debug Probe Examples

The examples below show how to use common debug probes. They should be customized according to the needs of your project and the paths where the software packages (IDE, GDB server, etc.) have been installed. See the *TEST* folder in this repository and the *TEST RTEgdbData* folders in the RTEdbg toolkit demo code for more examples.

### Segger J-Link
There are no known problems using J-Link with RTEgdbData.

Example batch file for data transfer using a J-Link debug probe (using default GDB server port 2331).
```
"c:\RTEdbg\UTIL\RTEgdbData" 2331 2000C00 2028 -delay=50 -bin="..\Test HV.bin"
```
Batch file to start the J-Link GDB Server. The CPU core (Cortex-M4) is not stopped or reset. See also [J-Link GDB Server](https://wiki.segger.com/J-Link_GDB_Server). Set the parameters according to your needs and hardware limitations.
```
@echo off
REM Start the J-Link GDB server and connect to the ARM Cortex CPU without stopping it.
:start
"C:\Program Files\SEGGER\JLink\JLinkGDBServerCL.exe" -device Cortex-M4 -if SWD -nohalt -noir -noreset -Speed 15000 -silent
if errorlevel 0 goto start
pause
```

**Note:** 
1. J-Link GDB Server terminates operation and returns 0 when RTEgdbData disconnects. In such a case, it will be automatically restarted in the batch (see the example above) file so that the programmer does not have to start the server each time data is to be transferred from the embedded system - see below.
2. The generally recommended order is to power up the target, connect it to J-Link and then start GDB Server. The "-notimeout" command line argument prevents GDB Server from closing automatically after a timeout of 5 seconds if no target voltage can be measured or the connection to the target fails. This command line option allows you to connect to a target after starting GDB Server.

### STMicroelectronics ST-LINK

The ST-LINK GDB server has a problem for which no direct solution has been found yet. The server stops the code execution in the embedded system as soon as RTEgdbData connects to it via the TCP/IP protocol. Code execution is stopped before RTEgdbData sends a command via the GDB protocol.

#### Workarounds: 
1. Restart the code execution with the command file argument (-start=cmd_file). Put the command \"vCont;c\" in this file and add other commands if necessary. This will restart code execution immediately after connecting to the ST-LINK GDB server. Note that it may take up to tens of milliseconds to resume execution. See also the batch and cmd files in the *TEST RTEgdbData* folders in the RTEdbg demo projects.
1. See also the description [Multiple data transfers in the persistent mode of operation](#multiple-data-transfers-in-the-persistent-mode-of-operation). Manually start the execution of the code from the IDE or with a command file after starting the RTEgdbData utility in persistent mode and then download the data from the embedded system as often as you like. Execution is only interrupted after RTEgdbData is started, not during individual data transfers. <br>
**Note:* If we recompile the modified code in the IDE and reload it onto the embedded system, the IDE automatically stops and restarts the GDB server. As a result, the RTEgdbData program loses its connection to the GDB server and must reconnect once the server has restarted. To do this, press the 'R' key to re-establish the connection. Once RTEgdbData reconnects, code execution can resume, and data transfer will function normally.
1. Use the ST-LINK for testing in the IDE, enable "Shared ST-LINK" (or start the ST-LINK server manually), and use the command line tool to transfer data to the host - see the section *'Transferring Data to the Host Using the ST-LINK Debug Probe'* in the RTEdbg Manual.
2. Use OpenOCD for ST-LINK - see [OpenOCD](#openocd---open-on-chip-debugger).
3. Enable "Shared ST-LINK" in the IDE or start the ST-LINK server manually first. ST-LINK can be used to test code in the IDE. Additionally, start a separate instance of the OpenOCD GDB server and transfer data during testing as described for OpenOCD. The ST-LINK server ensures that both GDB servers (ST-LINK and OpenOCD) have access to the embedded system memory. <br>
**Note:** Use "st-link backend tcp" in the OpenOCD config file to enable the parallel operation of ST-LINK and OpenOCD servers.

The following descriptions of how to use the ST-LINK GDB server are left for completeness and in case a workaround for the ST-LINK server is found or this problem is solved in the future.
<br>

**Example for the ST-LINK debug probe:** Start the ST-LINK GDB server first - see the batch file below. See also [ST-LINK GDB Server](https://www.st.com/resource/en/user_manual/um2576-stm32cubeide-stlink-gdb-server-stmicroelectronics.pdf). Set the parameters according to your requirements and hardware limitations - e.g. SWD clock speed.
```
"C:\ST\STM32CubeIDE_1.15.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.stlink-gdb-server.win32_2.1.300.202403291623\tools\bin\ST-LINK_gdbserver.exe" --swd -e --frequency 24000 --attach --shared --port-number 61234 --verbose -cp C:\ST\STM32CubeIDE_1.13.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.1.0.202305091550\tools\bin
```
Transfer data from the embedded system and start decoding if data transfer was successful (if RTEgdbData return value is 0). Data structure size is defined as 0 => the RTEgdbData utility reads the size from the structure header. The data is written to the *data.bin* file. If the data transfer is successful, the Decode.bat file is executed. See the RTEdbg demo projects for examples of the Decode batch file.

**Note:** The RTEdbg manual describes how to transfer data to the host using the command line application for ST-LINK (using default GDB server port 61234).
``` 
"c:\RTEdbg\UTIL\RTEgdbData" 61234 0x24000000 0
IF %ERRORLEVEL% EQU 0 goto decode
pause
goto exit:
:decode
Decode.bat
:exit
```

### OpenOCD - Open On-Chip Debugger
The OpenOCD GDB server works with many different debug probes. Use it if you have problems with the default GDB server, as it allows fine tuning to fix potential problems. The user should first test the operation on a test project where possible errors (e.g. the CPU is stopped when the RTEgdbData utility connects to the GDB server) will not cause any damage. When establishing communication between the GDB server and the RTEgdbData utility, the processor may be stopped. Stopping the execution with the GDB server is like triggering a breakpoint. If this would stop the execution of e.g. a control process, damage could occur.

If you have problems, check the OpenOCD settings. They are usually located in one or more configuration files, since e.g. CPU-specific settings defined in a separate file may be included in the main configuration file.

The most common problem is that the maximum number of connections for the OpenOCD GDB server is set 2. <br>
Example for the STM32CubeIDE: When you start code testing from the STM32CubeIDE, parallel communication with the GDB server is enabled for two GDB server channels (two connections). The first one is used for general debugging and the second one is used to inspect (and modify) the variables in the "Live Expressions" window during code execution. If you want to transfer data with the RTEgdbData utility while debugging, you must not enable "Live Expressions" in the debugger settings in the IDE. The second GDB channel can be used by RTEgdbData. If the "Live Expressions" functionality is important to you, consider the workarounds listed below.

The IDE's built-in GDB uses the first connection for general debugger functions and the second one for the "Variable Live View". When the RTEgdbData utility tries to connect to the GDB server, the request is rejected with a message like "Info : rejected 'gdb' connection, no more connections allowed".

It is possible to allow more connections to the same target by using the "-gdb-max-connections number" configuration option. Set this value to 3 or more to allow the RTEgdbData utility to access the embedded system memory. Your specific OpenOCD settings, such as -gdb-max-connections, must be at the end of the main config file to override any settings of this parameter in the include files.

Check the description of **[Using GDB as a non-intrusive memory inspector](https://openocd.org/doc/html/GDB-and-OpenOCD.html#gdbmeminspect)** and **[GDB CPU Configuration](https://openocd.org/doc/html/CPU-Configuration.html)** if you have a problem such as that the GDB server stops executing code in the processor when communication with the GDB server is established (at the start of data transfer with the RTEgdbData utility).


**Example of the data transfer with OpenOCD**

Data transfer to the host using the OpenOCD GDB serve (using default GDB server port 3333) - see below.
The OpenOCD server must be started first (it can also be started automatically in the IDE when you start testing code).
```
"c:\RTEdbg\RTEgdbData\RTEgdbData.exe" 3333 24000000 0
```
In the case of the ST-LINK debug probe, start the ST-LINK server before starting the OpenOCD GDB server. <br>
**Note:** The ST-LINK server only needs to be started if "st-link backend tcp" is enabled in the OpenOCD.cfg file.
```
"c:\Program Files (x86)\STMicroelectronics\stlink_server\stlinkserver.exe"
```
Start the OpenOCD GDB server - example for the ST-LINK debug probe. Change the paths according to the version of OpenOCD installed in your IDE. See also an example OpenOCD config file below. 
```
"C:\ST\STM32CubeIDE_1.15.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.openocd.win32_2.3.100.202312181736\tools\bin\openocd.exe" "-f" "OpenOCD.cfg" "-s" "C:\ST\STM32CubeIDE_1.15.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.debug.openocd_2.2.0.202401261111\resources\openocd\st_scripts" "-c" "gdb_report_data_abort enable" "-c" "gdb_port 3333" "-c" "tcl_port 6666" "-c" "telnet_port 4444"
```
**Note:** If a specific debug probe is to be used, add the debug probe serial number to the OpenOCD.exe command line options - e.g:  *"-c" "adapter serial 066EFF383930434B43184113"*

Example of an OpenOCD configuration file for the ST-LINK debug probe.
```
# The file was generated by STM32CubeIDE for the STM32H743ZITx chip. 
# The 'Reset mode' was set to 'none' in the IDE debugger settings.
# The 'Enable debug in low power modes' and 'Stop watchdog counters 
# on halt' were selected, as well as the 'Shared ST-LINK'.
#
# The following modifications have been done:
# A) The '-gdb-max-connections 4' has been added at the end to override
#    the same parameter (value = 2) in the 'target/stm32h7x.cfg' file.
#    Modify the settings according to your hardware limitations and 
#    project requirements.
#
# B) The "st-link backend tcp" has been disabled. 
#    Enable it if you'll use the ST-LINK server as backend.

source [find interface/stlink-dap.cfg]

# Uncomment the next line to use ST-LINK server as backend.
# st-link backend tcp

set WORKAREASIZE 0x8000
transport select "dapdirect_swd"
set CHIPNAME STM32H743ZITx
set BOARDNAME genericBoard

# Enable debug when in low power modes
set ENABLE_LOW_POWER 1

# Stop Watchdog counters when halt
set STOP_WATCHDOG 1

# STlink Debug clock frequency
set CLOCK_FREQ 24000

# Reset configuration
# use software system reset if reset done
reset_config none
set CONNECT_UNDER_RESET 0
set CORE_RESET 0

# ACCESS PORT NUMBER
set AP_NUM 0
# GDB PORT
set GDB_PORT 3333

set DUAL_BANK 1

# BCTM CPU variables
source [find target/stm32h7x.cfg]

# gdb-max-connections set to 4 (-1 = unlimited)
# The IDE uses one for general debugging and one for the Live Variable View.
# The default value in the "target/stm32---.cfg]" files is 2.
# The name "_CHIPNAME.cm7" must be replaced with the name used in the include file - see
# the target name used in the "source [find target/----.cfg]" - e.g. "$_CHIPNAME.cpu".
# The example below is for the STM32H7xx family.
$_CHIPNAME.cm7 configure -gdb-max-connections 4
```

**Testing with an OpenOCD GDB server in the STM32CubeIDE:** When the OpenOCD for ST-LINK is used for IDE debugging, the code execution in the embedded system is stopped when the code execution is aborted (e.g. after pressing "Ctrl-F2"). When using the ST-LINK or J-Link GDB debug probes and servers, the code continues to run normally in this case. A direct workaround has not yet been found. An indirect workaround is to use the ST-LINK server together with the OpenOCD GDB server for data transfer to the host using RTEgdbData and the ST-LINK GDB server for code debugging in the STM32Cube IDE.

### PE Micro Multilink

Start the PE Micro GDB server first (using default GDB server port 7224) - see the example below. See the [List of available commands](https://www.pemicro.com/blog/index.cfm?post_id=245) for the PE Micro GDB Server.
```
pegdbserver_console.exe -startserver -device=NXP_S32K3xx_S32K312 -interface=USBMULTILINK
```
Example of data transfer to the host using the PE Micro GDB server.
```
"c:\RTEdbg\UTIL\RTEgdbData\RTEgdbData.exe" 7224 0x04000000 0
```

### ESP32 JTAG Debugging

The code execution stops as soon as the RTEgdbData utility connects to the GDB server (even before the data transfer starts). A workaround has not yet been found. Instructions for transferring data to the host using the JTAG ESP32 debug probe will be published when testing is complete and the ESP32 RTEdbg demo is released.

<br>

## How to Contribute or Get Help
Follow the **[Contributing Guidelines](https://github.com/RTEdbg/RTEdbg/blob/master/docs/CONTRIBUTING.md)** for bug reports and feature requests. See also the **[RTEgdbData ToDo](ToDo.md)** list. When asking a support question, be clear and take the time to explain your problem properly. Upload log file to cloud service and add link to the file. Please use **[RTEdbg.freeforums.net](https://rtedbg.freeforums.net/)** for general discussions about the **[RTEdbg toolkit](https://github.com/RTEdbg/RTEdbg)**. If your problem is not strictly related to the RTEdbg toolkit or this repository, we recommend that you use [Stack Overflow](https://stackoverflow.com/), [r/Embedded](https://www.reddit.com/r/embedded/) or similar question-and-answer website instead.
