/*
 * Copyright (c) 2024 Branko Premzel.
 *
 * SPDX-License-Identifier: MIT
 */

 /*******************************************************************************
 * @file    cmd_line.h
 * @author  B. Premzel
 * @brief   Command line parameter processing functions
 ******************************************************************************/

#include "RTEgdbData.h"

#ifndef _COMMAND_LINE_PAR_H
#define _COMMAND_LINE_PAR_H

// Command line parameters structure
typedef struct
{
    unsigned start_address;         // Address of the g_rtedbg data structure
    unsigned size;                  // Size of the g_rtedbg data structure to load (0 = auto)
    unsigned filter;                // Filter value to set after the data transfer
    bool set_filter;                // true - set the new filter value, false - restore the old value
    unsigned delay;                 // Delay [ms] after the message filter value has been set to zero
    const char* log_file;           // Log file name (logging messages about operation and errors)
    const char* decode_file;        // Name of batch file for data decoding
    const char* bin_file_name;      // Binary file name
    const char* ip_address;         // GDB server IP address (default: "127.0.0.1" => "localhost")
                                    // The port must be defined separately with the -port=xxx parameter
    const char* start_cmd_file;     // File with commands sent to the GDB server after the start
    const char* filter_names;       // File with filter names
    unsigned short gdb_port;        // GDB server port number
    const char* driver_names[MAX_DRIVERS];  // Names of drivers with elevated priority
    size_t number_of_drivers;       // Number of drivers with elevated priority
    bool elevated_priority;         // true - set higher execution priority for RTEgdbData and servers (if names are given)
    bool clear_buffer;              // true - clear the circular buffer after data transfer to host
    bool log_gdb_communication;     // true - log all communication to the log file
    bool persistent_connection;     // true - connect to the GDB server permanently to enable multiple transfers
    bool detach;                    // true - send the detach command to the GDB server before disconnecting from the server. 
    unsigned max_message_size;      // Custom max. GDB message size the server may send
} parameters_t;

extern parameters_t parameters;

void process_command_line_parameters(int argc, char * argv[]);

#endif  // _COMMAND_LINE_PAR_H

/*==== End of file ====*/
