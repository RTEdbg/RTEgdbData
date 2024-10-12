## A list of things that are planned to be implemented in the future:
* Automatically find the address and size of the g_rtedbg data structure with logged data in the map file.
* Automatic or manual multiple transfer of data from the embedded system and collection of all data into one file for common decoding and display (multiple snapshots).
* Continuously stream data from the embedded system to the host when sufficient data transfer bandwidth is available.
* Rename the current binary data file to preserve it for later analysis.
* Set trigger address and value to enable parameterization of the software trigger, for example, to enable single-shot logging or disable obst-mortem logging mode.
* TestingTesting on different platforms and with different GDB servers.
* Checking and improving the code - especially the implementation of communication with the GDB server - e.g. messages sent by the GDB server without request, e.g. on reset, breakpoint, exception, etc., are not handled correctly.