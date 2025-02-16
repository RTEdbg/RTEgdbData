/*
 * Copyright (c) Branko Premzel.
 *
 * SPDX-License-Identifier: MIT
 */

/***
 * @file    gdb_lib.cpp
 * @brief   Helper functions for communication with the GDB server
 * @author  B. Premzel
 *
 * This file contains functions for:
 * - Establishing a connection with the GDB server
 * - Reading GDB server configuration information
 * - Reading from the embedded system memory
 * - Writing to the embedded system memory
 * - Other GDB-related operations
 *
 * Tested GDB servers:
 * - Segger J-LINK
 * - ST-LINK
 * - OpenOCD (ST-LINK and JTAG ESP32)
 *
 * Useful references:
 * - GDB Remote Protocol:        https://sourceware.org/gdb/onlinedocs/gdb/Remote-Protocol.html
 * - GDB Documentation:          https://www.sourceware.org/gdb/documentation/
 * - GDB Remote Serial Protocol: https://www.embecosm.com/appnotes/ean4/embecosm-howto-rsp-server-ean4-issue-2.pdf
 * - Winsock FAQ:                https://tangentsoft.net/wskfaq/general.html
 * - Winsock 2 API:              https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-send
 */

#include "pch.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "gdb_lib.h"
#include "logger.h"
#include "cmd_line.h"
#include "RTEgdbData.h"


 /*---------------- GLOBAL VARIABLES ------------------*/
unsigned last_gdb_error;                        // Last GDB error reported
char message_buffer[TCP_BUFF_LENGTH];           // Buffer for TCP message send/receive

static SOCKET gdb_socket = INVALID_SOCKET;
static unsigned data_received;                  // Number of bytes received in the buffer
static bool ack_mode_enabled = false;           // If true, send message acknowledgments
static unsigned max_memo_read_packet_size;      // Maximum read_memory_packet() size
static unsigned max_memo_write_packet_size;     // Maximum write_memory_packet() size
static unsigned max_gdb_send_message_size;      // Maximum size of message that can be sent to the GDB server
static unsigned max_gdb_recv_message_size;      // Maximum size of message that can be received from the GDB server
clock_t app_start_time;                         // Time of connection to GDB server


/*---------------- Local functions ---------------*/
static int get_hex_digit(const char * ptr);
static int gdb_get_message(size_t timeout);
static int read_memory_packet(unsigned char* buffer, unsigned int address, unsigned int length);
static int write_memory_packet(const unsigned char* buffer, unsigned address, unsigned length);
static int gdb_send_command(const char * command);
static void gdb_send_ack(void);
static void gdb_check_ack(void);
static void calculate_max_message_sizes(void);
static void print_O_type_message(void);
static void print_remaining_messages(void);
static int gdb_send(const char* msg, int length);
static bool gdb_error_reported(void);
static void internal_command(const char* cmd_text);
static const char* get_core_content(char* message);
static int parse_capability_data(const char* recvbuf);


/***
 * @brief Connect to the GDB server over the specified port.
 * 
 * @param gdb_port  GDB port number
 * 
 * @return GDB_OK    - Connection to GDB server successful
 *         GDB_ERROR - Could not connect to the GDB server
 */

int gdb_connect(unsigned short gdb_port)
{
    last_gdb_error = 0;
    app_start_time = clock_ms();
    int res = gdb_connect_socket(gdb_port);
    if (res != GDB_OK)
    {
        gdb_socket_cleanup();
        return GDB_ERROR;
    }

    ack_mode_enabled = true;

    // Check for initial acknowledgment from GDB server
    res = recv(gdb_socket, message_buffer, sizeof(message_buffer), 0);

    if (res > 0)    // Data received
    {
        log_communication("Recv", message_buffer, res);
        gdb_flush_socket();
    }

    res = gdb_check_server_capabilities();

    if (res != GDB_OK)
    {
        gdb_socket_cleanup();
        _fcloseall();
        return GDB_ERROR;
    }

    return gdb_request_no_ack_mode();
}


/***
 * @brief Connect to the GDB socket using TCP protocol.
 * 
 * @param gdb_port   GDB port number
 * 
 * @return GDB_OK    - Connection successful
 *         GDB_ERROR - Could not connect to the socket
 */
 
int gdb_connect_socket(unsigned short gdb_port)
{
    int res;
    WSADATA wsaData;
    struct sockaddr_in clientService;
    LARGE_INTEGER StartingTime;

    start_timer(&StartingTime);
    log_string("Connecting to the GDB server: ", NULL);

    // Initialize Winsock
    res = WSAStartup(MAKEWORD(2, 2), &wsaData);

    if (res != NO_ERROR)
    {
        log_data("Winsock startup error %d\n", (long long)res);
        return GDB_ERROR;
    }

    // Create a SOCKET for connecting to server
    // SOCK_STREAM => calling recv will return as much data as is currently
    // available up to the size of the buffer specified
    gdb_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (gdb_socket == INVALID_SOCKET)
    {
        log_wsock_error("cannot create socket.\n");
        (void)WSACleanup();
        return GDB_ERROR;
    }

    // The sockaddr_in structure specifies the address family,
    // IP address, and port of the server to be connected to.
    clientService.sin_family = AF_INET;
    clientService.sin_addr.s_addr = inet_addr(parameters.ip_address);
    clientService.sin_port = htons(gdb_port);
    // TODO: Replace inet_addr() with inet_pton() to support IPv4 and IPv6 addresses
    //       and make other necessary changes to make it work.

    // Connect to server
    res = connect(gdb_socket, (SOCKADDR*)&clientService, sizeof(clientService));

    if (res == SOCKET_ERROR)
    {
        log_wsock_error("unable to connect to the GDB server.\n");
        gdb_socket_cleanup();
        return GDB_ERROR;
    }

    // Set receive timeout value for the gdb_socket.
    DWORD timeout = 1;            // Minimal value to enable some kind of polling
    (void)setsockopt(gdb_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    // Set send timeout value for the gdb_socket.
    timeout = DEFAULT_SEND_TIMEOUT;
    (void)setsockopt(gdb_socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

    log_timing("OK (%.1f ms)", &StartingTime);
    return GDB_OK;
}


/***
 * @brief Send data to the GDB server using the TCP/IP protocol.
 *        Display error message if the data can not be sent.
 * 
 * @param msg     Pointer to the message string (does not have to be null-terminated)
 * @param length  Length of the message string
 * 
 * @return  GDB_ERROR - not successful
 *          GDB_OK    - data was sent successfully
 */

static int gdb_send(const char* msg, int length)
{
    if (msg == NULL || length <= 0)
    {
        last_gdb_error = ERR_BAD_INPUT_DATA;
        log_string(" - Invalid input data for gdb_send. ", NULL);
        return GDB_ERROR;
    }

    int res = send(gdb_socket, msg, length, 0);
    log_communication("Send", msg, length);

    if (res == SOCKET_ERROR)
    {
        DWORD wsock_err = GetLastError();

        if (wsock_err == WSAETIMEDOUT)
        {
            last_gdb_error = ERR_SEND_TIMEOUT;
            log_string(" - GDB Winsock send timeout. ", NULL);
        }
        else
        {
            last_gdb_error = ERR_SOCKET;
            log_wsock_error(" - GDB Winsock send error");
        }
        return GDB_ERROR;
    }

    if (res != length)
    {
        log_data(" - message not sent completely (only %llu). ", (long long)res);
        last_gdb_error = ERR_MSG_NOT_SENT_COMPLETELY;
        return GDB_ERROR;
    }

    return GDB_OK;
}


/***
 * @brief Check the error message type and report the error
 * 
 * @return true  - error reported or bad message format
 *         false - no error reported and message starts with '$'
 */

static bool gdb_error_reported(void)
{
    if (message_buffer[0] != '$')
    {
        last_gdb_error = ERR_BAD_MSG_FORMAT;
        log_string(" - bad message format - '$' not found: %.50s. ", message_buffer);
        return true;
    }

    if (message_buffer[1] != 'E')
    {
        return false;
    }

    last_gdb_error = ERR_GDB_REPORTED_ERROR;

    if (message_buffer[4] == '#')
    {
        int res = sscanf_s(&message_buffer[2], "%x", &last_gdb_error);

        if (res == 1)
        {
            log_string(" - GDB server reported error %.3s. ", &message_buffer[1]);
        }
        else
        {
            log_string(" - bad response (%.50s). ", message_buffer);
        }
    }
    else if ((message_buffer[1] == 'E') && (message_buffer[2] == '.'))
    {
        log_string(" - GDB error: %s", &message_buffer[3]);
    }
    else
    {
        log_string(" - Unknown error: %.50s ", message_buffer);
    }

    return true;
}


/***
 * @brief Read memory packet from the embedded system memory.
 *        Maximal packet size depends on the GDB server type.
 * 
 * @param buffer  Buffer to which the data should be written
 * @param address Address of data in the embedded system
 * @param length  Length of memory block [bytes]
 * 
 * @return GDB_OK    - no error
 *         GDB_ERROR - could not read memory
 */

static int read_memory_packet(unsigned char* buffer, unsigned int address, unsigned int length)
{
    if (((length * 2 + 4) > TCP_BUFF_LENGTH) || (length == 0))
    {
        last_gdb_error = ERR_BAD_INPUT_DATA;
        return GDB_ERROR;
    }

    // Prepare GDB command
    sprintf_s(message_buffer, sizeof(message_buffer), "$m%08x,%02x", address, length);
    unsigned char sum = 0;
    size_t buf_len = strlen(message_buffer);

    // Calculate checksum
    for (size_t n = 1; n < buf_len; n++)
    {
        sum += message_buffer[n];
    }
    sprintf_s(&message_buffer[buf_len], 5, "#%02x", sum);       // Add checksum

    buf_len = strlen(message_buffer);
    int res = gdb_send(message_buffer, (int)buf_len); // Send GDB command
    if (res != GDB_OK)
    {
        return GDB_ERROR;
    }

    res = gdb_get_message(0);               // Response (if OK) = "+$....hex_bytes...#xx"
    if (res != GDB_OK)
    {
        return GDB_ERROR;
    }

    if (gdb_error_reported())
    {
        return GDB_ERROR;
    }

    if (strchr(message_buffer, '*') != NULL)
    {
        log_string("\nError run length encoding not implemented. ", NULL);
        last_gdb_error = ERR_RUN_LENGTH_ENCODING_NOT_IMPLEMENTED;
        return GDB_ERROR;
    }

    // Verify checksum
    sum = 0;
    unsigned int i;

    for (i = 0; i < (length * 2); i++)
    {
        sum += message_buffer[i + 1];
    }

    if (message_buffer[i + 1] != '#')
    {
        log_string(" - bad message format - '#' not found: %.50s. ", &message_buffer[i+1]);
        last_gdb_error = ERR_BAD_MSG_FORMAT;    // Checksum not found
        return GDB_ERROR;
    }

    res = get_hex_digit(&message_buffer[i + 2]);

    if ((res < 0) || (sum != res))
    {
        log_string(" - bad message checksum. ", NULL);
        last_gdb_error = ERR_BAD_MSG_CHECKSUM;
        return GDB_ERROR;
    }

    // Convert the hex data to binary and copy it to the 'buffer'
    for (i = 0; i < length; i++)
    {
        int temp = get_hex_digit(&message_buffer[2 * i + 1]);

        if (temp >= 0)
        {
            buffer[i] = (unsigned char)temp;
        }
        else
        {
            log_string(" - bad message format. ", NULL);
            last_gdb_error = ERR_BAD_MSG_FORMAT;
            return GDB_ERROR;
        }
    }

    return GDB_OK;      // Received the requested number of data
}


/***
 * @brief Read memory block from the embedded system memory.
 *        Maximum size depends on the maximum memory read packet size.
 *
 * @param buffer  Pointer to the buffer where the read data will be stored
 * @param address Starting address in the embedded system memory to read from
 * @param length  Number of bytes to read
 *
 * @return GDB_OK    - Operation successful
 *         GDB_ERROR - Error occurred (check last_gdb_error for details)
 */

int gdb_read_memory(unsigned char* buffer, unsigned int address, unsigned int length)
{
    last_gdb_error = 0;

    if ((length < 1U) || (buffer == NULL))
    {
        last_gdb_error = ERR_BAD_INPUT_DATA;
        return GDB_ERROR;
    }

    unsigned data_read = 0;
    int res;
    LARGE_INTEGER StartingTime;

    log_data("\nReading %llu bytes ", (long long)length);
    log_data("from address 0x%08llX ", (long long)address);
    start_timer(&StartingTime);

    do
    {
        unsigned packet_size = length - data_read;

        if (packet_size > max_memo_read_packet_size)
        {
            packet_size = max_memo_read_packet_size;
        }

        res = read_memory_packet(buffer + data_read, address + data_read, packet_size);

        if (res != GDB_OK)
        {
            break;
        }

        data_read += packet_size;
    }
    while (data_read < length);

    log_timing(" (%.1f ms)", &StartingTime);

    return res;
}


/***
 * @brief Write the contents of a memory block to the memory in the embedded CPU.
 *        Maximum size depends on the maximum memory write packet size.
 * 
 * @param buffer  Pointer to the data that should be written to the specified address
 * @param address Starting address in the embedded system's memory to write to
 * @param length  Number of bytes to write
 *
 * @return GDB_OK    - Operation successful
 *         GDB_ERROR - Error occurred (check last_gdb_error for details)
 */

int gdb_write_memory(const unsigned char* buffer, unsigned address, unsigned length)
{
    last_gdb_error = 0;

    if ((length < 1U) || (buffer == NULL))
    {
        last_gdb_error = ERR_BAD_INPUT_DATA;
        return GDB_ERROR;
    }

    unsigned data_written = 0;
    int res;
    LARGE_INTEGER StartingTime;

    log_data("\nWriting %llu bytes ", (long long)length);
    log_data("to address 0x%08llX ", (long long)address);
    start_timer(&StartingTime);

    do
    {
        unsigned packet_size = length - data_written;

        if (packet_size > max_memo_write_packet_size)
        {
            packet_size = max_memo_write_packet_size;
        }

        res = write_memory_packet(buffer + data_written, address + data_written, packet_size);

        if (res != GDB_OK)
        {
            break;
        }

        data_written += packet_size;
    }
    while (data_written < length);

    log_timing(" (%.1f ms)", &StartingTime);

    return res;
}


/***
 * @brief Write the contents of a memory packet to the memory in the embedded CPU.
 *        Maximal packet size depends on the GDB server type.
 * 
 * @param buffer  Pointer to the data that should be written to the specified address
 * @param address Address of data in the embedded system
 * @param length  Length of memory block in bytes
 *
 * @return GDB_OK    - Operation successful
 *         GDB_ERROR - Data not written (check last_gdb_error for details)
 */

static int write_memory_packet(const unsigned char * buffer, unsigned address, unsigned length)
{
    if (((length * 2 + 16 + 4) > TCP_BUFF_LENGTH) || (length == 0))
    {
        last_gdb_error = ERR_BAD_INPUT_DATA;
        return GDB_ERROR;
    }

    sprintf_s(message_buffer, sizeof(message_buffer), "$M%08X,%04X:", address, length);
    char * position = &message_buffer[16];

    for (unsigned i = 0; i < length; i++)
    {
        sprintf_s(position, (size_t)(&message_buffer[TCP_BUFF_LENGTH] - position),
            "%02X", *buffer++);
        position += 2;
    }

    const unsigned data_size = 15 + 2 * length;
    unsigned char sum = 0;
    position = &message_buffer[1];

    for (unsigned i = 0; i < data_size; i++)
    {
        sum += *position++;
    }

    sprintf_s(position, (size_t)(&message_buffer[TCP_BUFF_LENGTH] - position), "#%02X", sum);

    unsigned msg_len = (unsigned)(position + 3 - message_buffer);
    if (gdb_send(message_buffer, msg_len) != GDB_OK)
    {
        return GDB_ERROR;
    }

    if (gdb_get_message(0) != GDB_OK)
    {
        return GDB_ERROR;
    }

    if (strncmp(message_buffer, "$OK#", 4) == 0)
    {
        return GDB_OK;
    }

    if (gdb_error_reported())
    {
        return GDB_ERROR;
    }

    log_string(" - bad response: %s. ", message_buffer);
    last_gdb_error = ERR_BAD_RESPONSE;
    return GDB_ERROR;
}


/***
 * @brief Receive a message from the GDB server
 * 
 * @param timeout  Max. waiting time for a message [ms]
 * 
 * @return GDB_OK    - no error
 *         GDB_ERROR - message not received
 */

static int gdb_get_message(size_t timeout)
{
    clock_t start_time = clock_ms();
    data_received = 0;

    if (timeout == 0)
    {
        timeout = RECV_TIMEOUT;
    }

    char * msg_ptr = message_buffer;
    *msg_ptr = 0;
    const unsigned max_len = sizeof(message_buffer);

    for(;;)
    {
        int res = recv(gdb_socket, msg_ptr, max_len - data_received, 0);

        if (res == 0)
        {
            // If the remote side has shut down the connection gracefully, and all data has been received,
            // a recv will complete immediately with zero bytes received
            log_string("\nConnection to the GDB server has been gracefully closed.\n", NULL);
            last_gdb_error = ERR_CONNECTION_CLOSED;
            return GDB_ERROR;
        }

        if (res < 0)        // Error reported?
        {
            int sock_err = GetLastError();
            if (sock_err != WSAETIMEDOUT)
            {
                last_gdb_error = ERR_SOCKET;
                return GDB_ERROR;
            }

            if ((clock_ms() - start_time) > timeout)
            {
                log_string(" - time out error. ", NULL);
                message_buffer[data_received] = 0;  // Terminate the string
                last_gdb_error = ERR_RCV_TIMEOUT;
                return GDB_ERROR;
            }

            continue;
        }

        log_communication("Recv", msg_ptr, res);
        msg_ptr += res;
        data_received += res;

        if (data_received >= (sizeof(message_buffer) - 1U))
        {
            log_data(" - buffer index overflow: %u", (long long)data_received);
            return GDB_ERROR;
        }

        // Check message - shortest regular message is '$#xx'
        if ((data_received >= 4U) && (message_buffer[data_received - 3U] == '#'))
        {
            gdb_send_ack();
            message_buffer[data_received] = 0;  // Terminate the string
            return GDB_OK;
        }
    }
}


/***
 * @brief Convert two hexadecimal characters to their binary representation
 * 
 * @param ptr Pointer to the hexadecimal string (must contain at least two characters)
 * 
 * @return int The binary value if successful (0-255), or -1 if an error occurred
 */

static int get_hex_digit(const char * ptr)
{
    if (ptr == NULL)
    {
        return -1;
    }

    int dec_val = 0;

    for (int i = 0; i < 2; i++)
    {
        dec_val *= 16;

        if ((*ptr >= '0') && (*ptr <= '9'))
        {
            dec_val += *ptr - '0';
        }
        else if ((*ptr >= 'A') && (*ptr <= 'F'))
        {
            dec_val += *ptr - 'A' + 10;
        }
        else if ((*ptr >= 'a') && (*ptr <= 'f'))
        {
            dec_val += *ptr - 'a' + 10;
        }
        else
        {
            return -1;
        }

        ptr++;
    }
    return dec_val;
}


/***
 * @brief Send a command to the GDB server
 * 
 * @param command  Command string to send to the GDB server
 * 
 * @return GDB_OK on success, GDB_ERROR on failure
 */

static int gdb_send_command(const char * command)
{
    char sendbuff[1024U];
    size_t len = strlen(command);

    if (len >= (sizeof(sendbuff) - 4))
    {
        log_data(" GDB command too long (%llu) ", (long long)len);
        last_gdb_error = ERR_BAD_INPUT_DATA;
        return GDB_ERROR;
    }

    // Calculate checksum (sum of all bytes modulo 256)
    unsigned char checksum = 0;
    for (size_t i = 0; i < len; i++)
    {
        checksum += (unsigned char)command[i];
    }

    // Format the command with $ prefix, command, # separator, and 2-digit hex checksum
    sprintf_s(sendbuff, sizeof(sendbuff), "$%s#%02X", command, checksum);

    // Send the formatted command (length + 4 for $, #, and two checksum digits)
    return gdb_send(sendbuff, (int)(len + 4));
}


/***
 * @brief Check the GDB debug server capability information and
 *        set the global statuses and variables accordingly.
 * 
 * @param recvbuf  Pointer to string with capability information
 *
 * @return GDB_OK on success, GDB_ERROR if NoAckMode is not supported
 */

static int parse_capability_data(const char * recvbuf)
{
    // Check if GDB server supports the QStartNoAckMode
    if (strstr(recvbuf, "QStartNoAckMode+") == NULL)
    {
        log_string("Error: ", "GDB server does not support 'QStartNoAckMode+' mode.");
        return GDB_ERROR;
    }

    // Determine max. message size that can be received by the GDB server
    max_gdb_send_message_size = DEFAULT_MESSAGE_SIZE;
    const char * text_position = strstr(recvbuf, "PacketSize=");

    if (text_position != NULL)
    {
        text_position += sizeof("PacketSize=") - 1;
        int res = sscanf_s(text_position, "%x", &max_gdb_send_message_size);

        if (res == 1)
        {
            log_data("max. message size %u", (long long)max_gdb_send_message_size);
        }
        else
        {
            log_data(
                "\nCannot determine maximal GDB message packet size - using default: %u.\n",
                DEFAULT_MESSAGE_SIZE);
            max_gdb_send_message_size = DEFAULT_MESSAGE_SIZE;
        }
    }
    else
    {
        log_data("\nPacketSize field not found - using default message size: %u.\n",
            DEFAULT_MESSAGE_SIZE);
        max_gdb_send_message_size = DEFAULT_MESSAGE_SIZE;
    }

    calculate_max_message_sizes();

    return GDB_OK;
}


/***
 * @brief Calculate max. message sizes for the communication with the GDB server.
 */

static void calculate_max_message_sizes(void)
{
    if (max_gdb_send_message_size > TCP_BUFF_LENGTH)
    {
        max_gdb_send_message_size = TCP_BUFF_LENGTH;
    }

    max_gdb_recv_message_size = max_gdb_send_message_size;

    if (parameters.max_message_size != 0)
    {
        // User defined receive buffer size
        max_gdb_recv_message_size = parameters.max_message_size;
        if (max_gdb_recv_message_size > TCP_BUFF_LENGTH)
        {
            max_gdb_recv_message_size = TCP_BUFF_LENGTH;
        }
    }

    /* Calculate the maximal read and write memory size (bytes).
     * Note: The present library implementation can not handle TCP messages with
     * a length over 65535 bytes -> max. possible read_memory size = (65535 - 4) / 2.
     * 
     * Size is made divisible by 4 because some debug probes transfer data more
     * slowly when it is not.
     */
    max_memo_read_packet_size = ((max_gdb_recv_message_size - 4) / 8) * 4;
        // Read packet: '$' at the start and checksum '#xx' at the end (no zero at end of string)

    max_memo_write_packet_size = ((max_gdb_send_message_size - 16 - 4) / 8) * 4;
        // Write packet: '$Mxxxxxxxx,xxxx:' at the start + '#xx' & zero at the end of string
}


/***
 * @brief Read the GDB server capabilities and check the capabilities used by our code.
 *        Set the global statuses accordingly to the results.
 *
 * @return GDB_OK    - capabilities have been successfully retrieved and parsed
 *         GDB_ERROR - an error occurred during the process
 */

int gdb_check_server_capabilities(void)
{
    LARGE_INTEGER StartingTime;
    start_timer(&StartingTime);
    last_gdb_error = 0;
    log_string("\nRetrieving GDB server capabilities: ", NULL);
    int res = gdb_send_command("qSupported");

    if (res != GDB_OK)
    {
        return GDB_ERROR;
    }

    gdb_check_ack();
    res = gdb_get_message(LONG_RECV_TIMEOUT);
    if (res != GDB_OK)
    {
        return GDB_ERROR;
    }

    res = parse_capability_data(message_buffer);
    if (res == GDB_OK)
    {
        log_timing(" (%.1f ms)", &StartingTime);
    }

    return res;
}


/***
 * @brief Send the "D" (detach) command to the GDB server.
 *        No error check if the GDB server responded properly. We'll disconnect anyway.
 */

void gdb_detach(void)
{
    if (parameters.detach)
    {
        // Send the detach command
        int res = gdb_send_command("D");

        if (res != GDB_OK)
        {
            return;
        }

        // Ignore the received message (the server sends "OK").
        (void)gdb_get_message(0);
    }
}


/***
 * @brief Execute internal command - the following ones are available:
 *    #delay xxx - delay xxx ms
 *    #init config_word timestamp_frequency
 *    #filter value - set a new filter value
 *    #echo text - echo the text
 * 
 * @param cmd_text  Pointer to the command text
 */

static void internal_command(const char * cmd_text)
{
    if (strncmp(cmd_text, "##", 2) == 0)
    {
        return;     // Ignore comments
    }

    if (strncmp(cmd_text, "#echo ", 6) != 0)
    {
        if (logging_to_file())
        {
            printf("\n   \"%s\" ", cmd_text);
        }

        log_string("\n   \"%s\" ", cmd_text);
    }

    if (strncmp(cmd_text, "#delay ", 7) == 0)
    {
        unsigned delay_ms = 0;
        int fields = sscanf_s(&cmd_text[7], "%u", &delay_ms);
        if ((fields == 1) && (delay_ms > 0))
        {
            if (logging_to_file())
            {
                printf("\ndelay %u ms", delay_ms);
            }
            Sleep(delay_ms);
            gdb_flush_socket();
        }
    }
    else if (strncmp(cmd_text, "#init ", 6) == 0)
    {
        unsigned cfg_word = 0;
        unsigned timestamp_frequency = 0;
        if (sscanf_s(&cmd_text[6], "%x %u", &cfg_word, &timestamp_frequency) == 2)
        {
            printf("\nLogging data structure initialization");
            initialize_data_logging_structure(cfg_word, timestamp_frequency);
        }
        else
        {
            log_string("- #init command must have two parameters: config word (hex) and timestamp frequency (decimal value) ", NULL);
        }
    }
    else if (strncmp(cmd_text, "#filter ", 8) == 0)
    {
        set_new_filter_value(&cmd_text[8]);
    }
    else if (strncmp(cmd_text, "#echo ", 6) == 0)
    {
        printf("\n   %s", &cmd_text[6]);
    }
    else
    {
        log_string("- unknown command", NULL);
    }
}


/***
 * @brief Send commands to the GDB server
 * 
 * @param cmd_file  File with GDB commands
 *
 * GDB Protocol description:
 *          https://sourceware.org/gdb/current/onlinedocs/gdb.html/Packets.html
 * Examples of GDB commands:
 *     "R 00"                               ;Reset
 *     "c"                                  ;Continue at present location
 *     "vCont;c"                            ;Continue at present location
 *     "Maddr,length:XX"                    ;Write length addressable memory units starting at address addr
 *     "set mem inaccessible-by-default off"
 *     "set remote hardware-watchpoint-limit 2"
 *     "set remote hardware-breakpoint-limit 2"
 *     "flushregs"
 * 
 * @return 0 - no error
 *         1 - error
 */

int gdb_send_commands_from_file(const char* cmd_file)
{
    if (cmd_file == NULL)
    {
        return 0;
    }

    // Discard a potentially unhandled message from GDB server (e.g. after reset)
    gdb_handle_unexpected_messages();

    if (logging_to_file())
    {
        printf("\nExecute command file: \"%s\" ...", cmd_file);
    }

    log_string("\nExecute command file: \"%s\" ...", cmd_file);

    FILE * commands;
    errno_t err = fopen_s(&commands, cmd_file, "r");

    if ((err != 0) || (commands == NULL))
    {
        char err_string[256];
        (void)strerror_s(err_string, sizeof(err_string), errno);
        log_string("\nCould not open command file - error: %s \n", err_string);

        if (logging_to_file())
        {
            printf("\nCould not open command file - error: %s \n", err_string);
        }
        
        return 1;
    }

    for (;;)
    {
        char cmd_text[512];
        char * rez = fgets(cmd_text, sizeof(cmd_text), commands);
        if (rez == NULL)
        {
            if (!feof(commands))
            {
                (void)strerror_s(cmd_text, sizeof(cmd_text), errno);
                log_string(": can't read from file - error: %s\n", cmd_text);

                if (logging_to_file())
                {
                    printf(": can't read from file - error: %s\n", cmd_text);
                }
            }
            break;
        }

        rez = strchr(cmd_text, '\n');
        if (rez != NULL)
        {
            *rez = '\0';        // Strip the newline at the end of line
        }

        if (strlen(cmd_text) > 0)
        {
            if (*cmd_text == '#')
            {
                internal_command(cmd_text);
            }
            else
            {
                if (gdb_execute_command(cmd_text) != GDB_OK)
                {
                    break;
                }
            }
        }
    }

    (void)fclose(commands);
    printf("\n");

    return 0;
}


/***
 * @brief Print hex encoded string that ends with '#'
 *        The string is in the message_buffer.
 */

static void print_O_type_message(void)
{
    char* hex_string = message_buffer;

    if (strncmp(message_buffer, "$O", 2) == 0)
    {
        hex_string += 2;
    }

    char* position = strchr(hex_string, '#');
    if (position != NULL)
    {
        *position = '\0';
    }

    size_t len = strlen(hex_string);
    if (len < 2)
    {
        return;
    }

    // Convert hex string to ASCII
    char* ptr = hex_string;
    char* out = hex_string;
    while (*ptr && *(ptr + 1)) {
        char first_hex = *ptr;
        char second_hex = *(ptr + 1);

        // Convert hex characters to integer values (0-9 and A-F)
        int first_value = toupper(first_hex) - '0' - ((first_hex > '9') ? 7 : 0);
        int second_value = toupper(second_hex) - '0' - ((second_hex > '9') ? 7 : 0);

        // Combine hex values into a single ASCII character
        *out = (char)((first_value << 4) | second_value);

        ptr += 2;  // Move to the next pair of hex characters
        out++;
    }
    *out = '\0';

    // Replace newline characters with spaces
    do
    {
        ptr = strchr(hex_string, '\n');
        if (ptr != NULL)
        {
            *ptr = ' ';
        }
    }
    while (ptr != NULL);

    log_string("\"%s\" ", hex_string);
}


/***
 * @brief Strip the starting '$' at the beginning and '#' and CRC at the end of the message.
 * 
 * @param  message  String to be processed
 * 
 * @return  pointer to the core content of the message string
 */

static const char* get_core_content(char* message)
{
    if (*message == '$')
    {
        message++;
    }

    char* ptr = strchr(message, '#');
    if (ptr != NULL)
    {
        *ptr = '\0';
    }

    return message;
}


/***
 * @brief Send a command to the GDB server and check the response
 * 
 * @param command  GDB command string
 * 
 * @return GDB_OK    - command executed
 *         GDB_ERROR - command could not be executed
 */

int gdb_execute_command(const char * command)
{
    last_gdb_error = 0;
    log_string("\n   \"%s\": ", command);
    LARGE_INTEGER StartingTime;
    start_timer(&StartingTime);

    if (gdb_send_command(command) != GDB_OK)
    {
        return GDB_ERROR;
    }

    if (gdb_get_message(0) != GDB_OK)
    {
        return GDB_ERROR;
    }

    if (gdb_error_reported())
    {
        return GDB_ERROR;
    }

    // Receives "$OK#" if OK, "$Oxx... - error message", or some other response
    // 'xx...' is hex encoding of ASCII data, to be written as the program's console output.
    if (strncmp(message_buffer, "$O", 2) == 0)
    {
        if (message_buffer[2] != 'K')
        {
            int res;
            do
            {
                print_O_type_message();

                /*TODO - Error message processing is not finalized.
                * 'O' type message may not be continued. Check for other data.
                */
                res = gdb_get_message(ERROR_DATA_TIMEOUT);
                if ((res > 0) && gdb_error_reported())
                {
                    return GDB_ERROR;
                }
            }
            while (res == GDB_OK);
        }
        else
        {
            log_string("OK", NULL);
        }
    }
    else
    {
        const char* text = get_core_content(message_buffer);
        log_string("\"%s\"", *text == '\0' ? "unsupported command" : text);
        gdb_flush_socket();
        return GDB_ERROR;
    }

    log_timing(" (%.1f ms)", &StartingTime);
    return GDB_OK;
}


/***
 * @brief Try to flush the complete contents of the GDB stream.
 *        This is done in case the GDB server sends an unexpected message.
 */

void gdb_flush_socket(void)
{
    char recvbuf[256];
    int res;

    do
    {
        res = recv(gdb_socket, recvbuf, sizeof(recvbuf), 0);
        if (res > 0)
        {
            log_communication("Recv", recvbuf, res);
        }
    }
    while (res > 0);
}


/***
 * @brief Try to set the no ACK mode.
 *        GDB server may not support this command.
 *        If the command is supported, the server will respond with "$OK#".
 *        Otherwise, it will respond with an error message.
 */

int gdb_request_no_ack_mode(void)
{
    ack_mode_enabled = true;

    if (gdb_send_command("QStartNoAckMode") != GDB_OK)
    {
        return GDB_ERROR;
    }

    gdb_check_ack();

    if (gdb_get_message(0) != GDB_OK)
    {
        return GDB_ERROR;
    }

    if (strncmp(message_buffer, "$OK#", 4) != 0)
    {
        log_string("NoACK mode not supported by the GDB server - received: %s. ", message_buffer);
        return GDB_ERROR;
    }
    else
    {
        ack_mode_enabled = false;
        gdb_flush_socket();
    }

    return GDB_OK;
}


/***
 * @brief  Send acknowledge if necessary for the previously received data.
 *         Do not send it if the "QStartNoAckMode" was enabled.
 */

static void gdb_send_ack(void)
{
    if (ack_mode_enabled)
    {
        (void)gdb_send("+", 1);
    }
}


/***
 * @brief  Receive and check the '+' acknowledge character.
 * @note   This function waits for an acknowledgement character from the GDB server.
 *         If the acknowledgement is not received within the specified timeout, it logs an error.
 */

static void gdb_check_ack(void)
{
    clock_t start_time = clock_ms();   // Record the start time

    while ((clock_ms() - start_time) < LONG_RECV_TIMEOUT) // Loop until timeout
    {
        message_buffer[0] = 0;      // Clear the message buffer
        int res = recv(gdb_socket, message_buffer, 1, 0); // Receive a single character

        switch (res)
        {
            case 0: // Connection closed
                log_string("\nConnection to the GDB server has been gracefully closed.", NULL);
                return;

            case 1: // Character received
                if (message_buffer[0] == '+') // Check if it's an ACK
                {
                    return; // Successful ACK received
                }
                log_communication("Recv", message_buffer, res); // Log the received data
                log_string("\nBad ACK received: %s", message_buffer); // Log the bad ACK
                gdb_flush_socket(); // Flush any remaining data in the socket buffer
                break;

            case SOCKET_ERROR: // Socket error
            {
                int sock_err = GetLastError();
                if (sock_err != WSAETIMEDOUT)
                {
                    log_wsock_error("\nSocket error while waiting for ACK");
                    return;
                }
                // If it's a timeout, continue the loop
                break;
            }

            default:
                log_wsock_error("Unexpected error");
                return;
        }
    }

    log_string("\nACK timeout: No acknowledgement received within the specified timeout.", NULL);
}


/***
 * @brief  Check if the GDB server has sent a message without a request, as in the case of
 *         a triggered breakpoint, reset, etc. Such message is logged and discarded.
 */

void gdb_handle_unexpected_messages(void)
{
    int res = 0;

    do
    {
        res = recv(gdb_socket, message_buffer, TCP_BUFF_LENGTH, 0);
        if (res > 0)
        {
            // Log an unexpected GDB message
            log_string("\nUnexpected message: %s", message_buffer);
        }
    }
    while (res > 0);    // Repeat until all data received
}


/***
 * @brief  Close and cleanup the socket used for communication with the GDB server.
 */

void gdb_socket_cleanup(void)
{
    log_string("\n", NULL);
    (void)closesocket(gdb_socket);  // Close the socket
    (void)WSACleanup();             // Cleanup the Winsock library
}

/*==== End of file ====*/
