/*
 * Copyright (c) 2024 Branko Premzel.
 *
 * SPDX-License-Identifier: MIT
 */

/***
 * @file    logger.cpp
 * @brief   Time measurement and data logging to a file or to the console.
 * @author  B. Premzel
 */


#include "pch.h"
#include <time.h>
#include "logger.h"
#include "gdb_defs.h"
#include "gdb_lib.h"
#include "cmd_line.h"
#include <share.h>


/*---------------- GLOBAL VARIABLES ------------------*/
static FILE * log_output = stdout;      // File to which the messages will be logged (default = console)
static bool logging_enabled = true;     // false - do not log any information
static LARGE_INTEGER Frequency;         // Frequency of the performance counter


/***
 * @brief Enable or disable logging to file or stdout.
 * 
 * @param on_off - true to enable logging,
 *                 false to disable logging.
 */

void enable_logging(bool on_off)
{
    logging_enabled = on_off;
}


/***
 * @brief Check if the logging to a file is active.
 * 
 * @return true if logging to a file and false if logging to stdout
 */

bool logging_to_file(void)
{
    if (log_output != stdout)
    {
        return true;
    }
    else
    {
        return false;
    }
}


/***
 * @brief Create log file to which the messages will be printed.
 *        If the file cannot be opened (created), messages will be printed to stdout.
 *
 * @param file_name  Log file name (NULL for stdout).
 */

void create_log_file(const char * file_name)
{
    if (file_name == NULL)
    {
        log_output = stdout;
    }
    else
    {
        // Open the file for reading and writing so that it can be read while it
        // is being written in case it is being opened by a log viewer software.
        log_output = _fsopen(file_name, "w+", _SH_DENYNO);
        if (log_output == NULL)
        {
            log_output = stdout;
        }
    }
}


/***
 * @brief Record the starting time.
 * 
 * @param timer Pointer to store the start time.
 */

void start_timer(LARGE_INTEGER * timer)
{
    (void)QueryPerformanceFrequency(&Frequency);
    (void)QueryPerformanceCounter(timer);
}


/***
 * @brief Log a value with a text message
 * 
 * @param text Text message (must include a format specifier for the data)
 * @param data Value to be logged
 */

void log_data(const char * text, long long int data)
{
    if (logging_enabled)
    {
        fprintf(log_output, text, data);

        if (logging_to_file())
        {
            fflush(log_output);
        }
    }
}


/***
 * @brief Log a string with a text message
 * 
 * @param text   Text message (must include %s if string is not NULL)
 * @param string String to be logged (NULL if not used)
 */

void log_string(const char * text, const char * string)
{
    if (logging_enabled)
    {
        if (string != NULL)
        {
            fprintf(log_output, text, string);
        }
        else
        {
            fprintf(log_output, text);
        }

        if (logging_to_file())
        {
            fflush(log_output);
        }
    }
}


/***
 * @brief Log elapsed time in ms with a text message
 * 
 * @param text        Text message (must include a format specifier for the elapsed time)
 * @param start_timer Start time reference
 */

void log_timing(const char * text, LARGE_INTEGER * start_timer)
{
    if (logging_enabled)
    {
        LARGE_INTEGER end_timer;
        (void)QueryPerformanceCounter(&end_timer);
        LARGE_INTEGER elapsed;
        elapsed.QuadPart = end_timer.QuadPart - start_timer->QuadPart;

        // Calculate elapsed time in milliseconds
        double time_elapsed = (double)elapsed.QuadPart * 1e3 / (double)Frequency.QuadPart;
        
        // Log the elapsed time with the provided text message
        fprintf(log_output, text, time_elapsed);

        if (logging_to_file())
        {
            fflush(log_output);
        }
    }
}


/***
 * @brief Log Winsock errors
 * 
 * @param text   Text message
 */

void log_wsock_error(const char * text)
{
    if (!logging_enabled)
    {
        return;
    }

    // Get the last socket error
    int sock_err = GetLastError();
    fprintf(log_output, "%s - Winsock error %d", text, sock_err);

    // Log specific error messages based on the error code
    switch (sock_err)
    {
    case WSAETIMEDOUT:
        fprintf(log_output, " - (time-out). ");
        break;

    case WSAECONNRESET:
        fprintf(log_output, " - (an existing connection was forcibly closed). ");
        break;

    case WSAECONNABORTED:
        fprintf(log_output, " - (an established connection was aborted). ");
        break;

    case WSAECONNREFUSED:
        fprintf(log_output, " - (connection refused - i.e. no service at this port). ");
        break;

    case WSAEADDRINUSE:
        fprintf(log_output, " - (only one usage of each socket address (protocol/network address/port) is normally permitted).");
        break;

    case WSAENETUNREACH:
        fprintf(log_output, " - (a socket operation was attempted to an unreachable network). ");
        break;

    case WSAEISCONN:
        fprintf(log_output, " - (a connect request was made on an already connected socket). ");
        break;

    case WSAEHOSTDOWN:
        fprintf(log_output, " - (a socket operation failed because the destination host was down). ");
        break;

    default:
        break;
    }

    if (logging_to_file())
    {
        fflush(log_output);
    }
}


/***
 * @brief Calculate elapsed time since timer start
 * 
 * @param start_timer  pointer to a variable with start time
 * 
 * @return Time in ms
 */

double time_elapsed(LARGE_INTEGER * start_timer)
{
    LARGE_INTEGER Elapsed;
    LARGE_INTEGER stop_timer;

    (void)QueryPerformanceCounter(&stop_timer);
    // Calculate the elapsed time
    Elapsed.QuadPart = stop_timer.QuadPart - start_timer->QuadPart;

    // Convert the elapsed time to milliseconds and return
    return Elapsed.QuadPart * 1e3 / Frequency.QuadPart;
}


/***
 * @brief Log the complete communication with the GDB server
 * 
 * @param direction   String showing the direction of communication (send / receive)
 * @param msg         Message sent to or received from the GDB server
 * @param length      Message length
 */

void log_communication(const char* direction, const char* msg, int length)
{
    if (parameters.log_gdb_communication)
    {
        fprintf(log_output, "\n%6.3f ms [%s: %.*s]\n",
            (double)(clock_ms() - app_start_time) / CLOCKS_PER_SEC * 1000, direction, length, msg);

        if (logging_to_file())
        {
            fflush(log_output);
        }
    }
}


/***
 * @brief Enable / disable logging to the log file.
 */

void disable_enable_logging_to_file(void)
{
    if (parameters.log_file == NULL)
    {
        printf("\nLog file not defined.\n");
        return;
    }

    if (logging_to_file())
    {
        fflush(log_output);
        fclose(log_output);
        log_output = stdout;
        printf("\nLogging to file disabled.\n");
    }
    else
    {
        create_log_file(parameters.log_file);
        printf("\nLogging to file enabled.\n");
    }
}


/*==== End of file ====*/
