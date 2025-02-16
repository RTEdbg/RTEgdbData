/*
 * Copyright (c) Branko Premzel.
 *
 * SPDX-License-Identifier: MIT
 */

 /*******************************************************************************
 * @file    cmd_line.cpp
 * @author  B. Premzel
 * @brief   Command line parameter processing functions
 *******************************************************************************/

#include "pch.h"
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include "cmd_line.h"
#include "RTEgdbData.h"
#include "gdb_defs.h"
#include "logger.h"


// Local functions
static void show_help_and_exit(void);
static void check_parameters(void);


/***
 * @brief Show help and exit
 * 
 * This function displays the help message and exits the program.
 * It provides information about the usage of the RTEgdbData tool,
 * including the version, command line parameters, and a link to
 * the project's Readme.md file for further instructions.
 */

static void show_help_and_exit(void)
{
    printf(
        "\n\nRTEgdbData %s (Build date: %s)"
        "\nTransfer g_rtedbg structure to the host using a GDB server."
        "\nSee the Readme.md file in the [https://github.com/RTEdbg/RTEgdbData] project for instructions.\n\n",
        RTEGDBDATA_VERSION,
        __DATE__);
    exit(1);
}


/***
 * @brief Check command line parameter values and report error if not ok.
 * 
 * This function validates the command line parameters for size and start address.
 * It ensures that the size is divisible by 4 and greater than the minimum buffer size,
 * and that the start address is 32-bit word aligned.
 * If any parameter is invalid, it displays an error message and exits the program.
 */

static void check_parameters(void)
{
    if (((parameters.size & 3) != 0) ||
        ((parameters.size < MIN_BUFFER_SIZE) && (parameters.size != 0))
       )
    {
        printf("The size parameter must be divisible by 4 and greater than %u.", MIN_BUFFER_SIZE);
        show_help_and_exit();
    }

    if ((parameters.start_address & 3) != 0)
    {
        printf("The address parameter must be divisible by 4 (32-bit word aligned).");
        show_help_and_exit();
    }
}


/***
 * @brief Process message filter value parameter
 *
 * This function processes the message filter value parameter provided as a string.
 * It converts the string to an unsigned integer using hexadecimal format.
 * If the conversion is successful, it sets the filter value and marks the filter as set.
 * If the conversion fails, it displays an error message and exits the program.
 *
 * @param number Pointer to number string
 */

static void process_filter_value(const char * number)
{
    unsigned int n;

    if (sscanf_s(number, "%x", &n) == 1)
    {
        parameters.filter = n;
        parameters.set_filter = true;
    }
    else
    {
        printf("Incorrect -filter=xxx parameter.");
        show_help_and_exit();
    }
}


/***
 * @brief Process max message length parameter
 *
 * This function processes the max message length parameter provided as a string.
 * It converts the string to an unsigned integer.
 * If the conversion is successful and the value is within the valid range, 
 * it sets the max message size parameter.
 * If the conversion fails or the value is out of range, it displays an error message and exits the program.
 *
 * @param number Pointer to number string
 */

static void process_max_msg_length_value(const char* number)
{
    unsigned int n = 0;
    bool value_ok = false;

    if (sscanf_s(number, "%u", &n) == 1)
    {
        if ((n >= 256U) && (n <= TCP_BUFF_LENGTH))
        {
            parameters.max_message_size = n;
            value_ok = true;
        }
    }

    if (!value_ok)
    {
        printf("The '-len=xxx' parameter must be >= 256 and <= %u.", TCP_BUFF_LENGTH);
        show_help_and_exit();
    }
}


/***
 * @brief Process delay parameter
 *
 * This function processes the delay parameter provided as a string.
 * It converts the string to an unsigned integer.
 * If the conversion is successful and the value is not zero, 
 * it sets the delay parameter.
 * If the conversion fails or the value is zero, it displays an error message and exits the program.
 *
 * @param number Pointer to number string
 */

static void process_delay_value(const char * number)
{
    unsigned int n = 0;
    bool value_ok = false;

    if (sscanf_s(number, "%u", &n) == 1)
    {
        if (n != 0U)
        {
            parameters.delay = n;
            value_ok = true;
        }
    }

    if (!value_ok)
    {
        printf("The '-delay=xxx' parameter cannot be zero.");
        show_help_and_exit();
    }
}


/***
 * @brief Process command line parameter value - check and remove quotation marks
 * 
 * This function processes a command line parameter by removing the quotation marks
 * from the start and end of the string if they are present. If the quotation marks
 * are not properly paired, it displays an error message and exits the program.
 * 
 * @param parameter Command line parameter text
 *
 * @return Pointer to stripped string
 */

static const char * remove_quotation_marks(char * parameter)
{
    // Check if the first character is a quotation mark
    if (*parameter == '"')
    {
        size_t len = strlen(parameter);

        // Check if the last character is a quotation mark
        if ((len > 1) && (parameter[len - 1] == '"'))
        {
            parameter[len - 1] = '\0'; // Null-terminate the string, removing the last quotation mark
        }
        else
        {
            printf("Missing closing quotation mark: %s", parameter);
            show_help_and_exit();
        }

        parameter++; // Move the pointer to the next character, skipping the first quotation mark
    }

    return parameter;
}


/***
 * @brief Add the driver name to the list of drivers.
 *        Enable the priority elevation if the -priority argument is missing.
 * 
 * @param driver_name  Name of the driver executable file.
 */

static void add_driver_name(const char* driver_name)
{
    if (parameters.number_of_drivers >= MAX_DRIVERS)
    {
        printf("\nThe -driver argument can be used a maximum of %d times.", MAX_DRIVERS);
        show_help_and_exit();
    }
    else
    {
        parameters.driver_names[parameters.number_of_drivers] = driver_name;
        ++parameters.number_of_drivers;
        parameters.elevated_priority = true;
    }
}


/***
 * @brief Process a single command line parameter
 *
 * This function processes a single command line parameter and updates the
 * corresponding global parameters based on the parameter type. It supports
 * various parameter types such as delay, filter, binary file name, IP address,
 * log file, message size, decode file, start command file, filter names, driver,
 * clear buffer, priority, debug, and persistent connection.
 *
 * @param  parameter - string with the parameter
 */

static void process_one_cmd_line_parameter(char * parameter)
{
    if (strncmp(parameter, "-delay=", 7) == 0)
    {
        process_delay_value(&parameter[7]);
    }
    else if (strncmp(parameter, "-filter=", 8) == 0)
    {
        process_filter_value(&parameter[8]);
    }
    else if (strncmp(parameter, "-bin=", 5) == 0)
    {
        parameters.bin_file_name = remove_quotation_marks(&parameter[5]);
    }
    else if (strncmp(parameter, "-ip=", 4) == 0)
    {
        parameters.ip_address = remove_quotation_marks(&parameter[4]);
    }
    else if (strncmp(parameter, "-log=", 5) == 0)
    {
        parameters.log_file = remove_quotation_marks(&parameter[5]);
        create_log_file(parameters.log_file);
    }
    else if (strncmp(parameter, "-msgsize=", 9) == 0)
    {
        process_max_msg_length_value(&parameter[9]);
    }
    else if (strncmp(parameter, "-decode=", 8) == 0)
    {
        parameters.decode_file = remove_quotation_marks(&parameter[8]);
    }
    else if (strncmp(parameter, "-start=", 7) == 0)
    {
        parameters.start_cmd_file = remove_quotation_marks(&parameter[7]);
    }
    else if (strncmp(parameter, "-filter_names=", 14) == 0)
    {
        parameters.filter_names = remove_quotation_marks(&parameter[14]);
    }
    else if (strncmp(parameter, "-driver=", 8) == 0)
    {
        add_driver_name(remove_quotation_marks(&parameter[8]));
    }
    else if (strcmp(parameter, "-clear") == 0)
    {
        parameters.clear_buffer = true;
    }
    else if (strcmp(parameter, "-priority") == 0)
    {
        parameters.elevated_priority = true;
    }
    else if (strcmp(parameter, "-debug") == 0)
    {
        parameters.log_gdb_communication = true;
    }
    else if (strcmp(parameter, "-detach") == 0)
    {
        parameters.detach = true;
    }
    else if (strcmp(parameter, "-p") == 0)
    {
        parameters.persistent_connection = true;
    }
    else
    {
        printf("Incorrect parameter: '%s'", parameter);
        show_help_and_exit();
    }
}


/***
 * @brief Process the command line parameters
 * 
 * This function processes the command line parameters passed to the application.
 * It expects at least 4 parameters: the application path, GDB port number, 
 * data structure address, and data structure size. Additional optional parameters 
 * can be provided to further configure the application.
 * 
 * @param argc Number of command line parameters (including the APP full path name)
 * @param argv Array of pointers to the command line parameter strings
 */

void process_command_line_parameters(int argc, char* argv[])
{
    if (argc < 4)
    {
        printf("Mandatory parameters not defined.");
        show_help_and_exit();
    }

    parameters.bin_file_name = "data.bin";          // Default binary file name
    parameters.ip_address = DEFAULT_HOST_ADDRESS;

    int res = sscanf_s(argv[1], "%hu", &parameters.gdb_port);

    if (res != 1)
    {
        printf("Incorrect GDB port number parameter: %s", argv[1]);
        show_help_and_exit();
    }

    res = sscanf_s(argv[2], "%x", &parameters.start_address);

    if (res != 1)
    {
        printf("Incorrect data structure address parameter: %s", argv[2]);
        show_help_and_exit();
    }

    res = sscanf_s(argv[3], "%x", &parameters.size);

    if (res != 1)
    {
        printf("Incorrect data structure size parameter: %s", argv[3]);
        show_help_and_exit();
    }

    // Process the command line options
    for (int i = 4; i < argc; i++)
    {
        process_one_cmd_line_parameter(argv[i]);
    }

    check_parameters();
}

/*==== End of file ====*/
