/*
 * Copyright (c) Branko Premzel.
 *
 * SPDX-License-Identifier: MIT
 */

/***
 * @file   gdb_defs.h
 * @brief  Compile time parameters and error code definitions.
 * @author B. Premzel
 */


#ifndef __GDB_DEFS_H
#define __GDB_DEFS_H

#define DEFAULT_HOST_ADDRESS  "127.0.0.1"  // "localhost"

#define RECV_TIMEOUT           500      // Max. time in ms to receive a message from the GDB server
#define LONG_RECV_TIMEOUT     2500      // Max. time in ms to receive a query message from the GDB server
#define DEFAULT_SEND_TIMEOUT    50      // Max. time in ms to send a message to the GDB server
                                        // The send() function blocks only if no buffer space is available
                                        // within the transport system to hold the data to be transmitted.
#define ERROR_DATA_TIMEOUT      50      // Max. time in ms to wait for a message following the 'O' type error message

#define DEFAULT_MESSAGE_SIZE  4096      // Default max. send message size (sent to the GDB server) if there is
                                        // no 'PacketSize' field in the capability data

#define TCP_BUFF_LENGTH      65535      // The maximum TCP packet size including header


/*----------------------------------------------------
 *  E R R O R   C O D E S
 * 
 * Update the 'display_errors()' function if new error
 * codes are added to the enum.
 *---------------------------------------------------*/

enum error_codes {
    ERR_RCV_TIMEOUT = 1,            // Message has not been received (timeout error)
    ERR_SEND_TIMEOUT,               // Message could not be sent
    ERR_SOCKET,                     // Winsock error
    ERR_BAD_MSG_FORMAT,             // Bad message format
    ERR_BAD_MSG_CHECKSUM,           // Bad message checksum
    ERR_RUN_LENGTH_ENCODING_NOT_IMPLEMENTED,
    ERR_CONNECTION_CLOSED,          // Socket has been closed
    ERR_BAD_INPUT_DATA,             // Bad function parameter
    ERR_MSG_NOT_SENT_COMPLETELY,    // The send() function could not send the complete message
    ERR_BAD_RESPONSE,               // Unknown/bad response from GDB
    ERR_GDB_REPORTED_ERROR          // GDB server returned error message '$Exx#xx' or '$E.errtext#xx'
};

#endif  //__GDB_DEFS_H

/*==== End of file ====*/
