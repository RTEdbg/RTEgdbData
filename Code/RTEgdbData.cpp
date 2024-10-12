/*
 * Copyright (c) 2024 Branko Premzel.
 *
 * SPDX-License-Identifier: MIT
 */

/***
 * @file  RTEgdbData.cpp
 * 
 * @brief The utility enables data transfer from the embedded system to the host using the GDB server. 
 *        See the Readme.md file for a detailed description, limitations, workarounds,
 *        and instructions for use.
 *
 * @note The code has been tested with the J-LINK, ST-LINK, and OpenOCD GDB servers. 
 *       The program does not automatically try to restart the data transfer if it
 *       was not successful. The data transfer has to be restarted by the user.
 *
 * @author B. Premzel
 */


#include "pch.h"
#pragma comment(lib, "Ws2_32.lib")   // Link additional libraries
#include <time.h>
#include <malloc.h>
#include <stdint.h>
#include <conio.h>
#include "gdb_lib.h"
#include "RTEgdbData.h"
#include "cmd_line.h"
#include "rtedbg.h"
#include "logger.h"
#include <tlhelp32.h>



//********** Global variables ***********
parameters_t parameters;             // Command line parameters
uint32_t old_msg_filter;             // Filter value before data logging is disabled
rtedbg_header_t rtedbg_header;       // Header of the g_rtedbg structure loaded from embedded system
static unsigned* p_rtedbg_structure; // Pointer to memory area allocated for the g_rtedbg structure


//*********** Local functions ***********
static bool allocate_memory_for_g_rtedbg_structure(void);
static void benchmark_data_transfer(void);
static int  check_header_info(void);
static int  check_message_filter_disabled(void);
static void decrease_priorities(void);
static void delay_before_data_transfer(void);
static void display_errors(const char* message);
static void display_logging_state(clock_t* start_time);
static void execute_decode_batch_file(void);
static int  erase_buffer_index(void);
static DWORD GetProcessIdByName(const char* processName);
static void increase_priorities(void);
static int  load_rtedbg_structure_header(void);
static int  pause_data_logging(void);
static int  persistent_connection(void);
static void print_filter_info(void);
static void print_rtedbg_header_info(void);
static int  read_memory_block(unsigned char * buffer, uint32_t address, uint32_t size);
static void repeat_start_command_file(void);
static int  reset_circular_buffer(void);
static int  save_rtedbg_structure(void);
static void send_commands_from_file(char name_start);
static int  set_or_restore_message_filter(void);
static void set_process_priority(const char * process_name, DWORD dwPriorityClass, bool report_error);
static bool single_shot_active(void);
static int  single_data_transfer(void);
static void show_help(void);
static void switch_to_post_mortem_logging(void);
static void switch_to_single_shot_logging(void);


/***
 * @brief Main function
 * 
 * @param argc Number of command line parameters (including the APP full path name)
 * @param argv Array of pointers to the command line parameter strings
 *
 * @return 0 - OK
 *         1 - error occurred
 */

int __cdecl main(int argc, char * argv[])
{
    int rez;
    clock_t main_start_time = clock_ms();
    process_command_line_parameters(argc, argv);

    if (gdb_connect(parameters.gdb_port) != GDB_OK)
    {
        if (logging_to_file())
        {
            printf("Could not connect to the GDB server. Check the log file for details.\n");
        }

        return 1;
    }

    increase_priorities();
    rez = gdb_send_commands_from_file(parameters.start_cmd_file);
    if (rez != 0)
    {
        decrease_priorities();
        gdb_detach();
        gdb_socket_cleanup();
        (void)_fcloseall();
        return 1;
    }
    
    if (parameters.persistent_connection)
    {
        rez = persistent_connection();
        printf("\n");
    }
    else
    {
        rez = single_data_transfer();
        log_data("\nTotal time: %llu ms\n\n", (long long)(clock_ms() - main_start_time));

        if (logging_to_file() && (rez != 0))
        {
            display_errors("\nFailed to read data from the embedded system:");
        }
    }

    decrease_priorities();
    gdb_detach();
    gdb_socket_cleanup();
    (void)_fcloseall();
    return rez;
    }


/***
 * @brief Execute a single data transfer and exit
 *
 * @return 0 - no error
 *         1 - error occurred (data not received)
 */

static int single_data_transfer(void)
{
    if (logging_to_file())
    {
        printf("\nReading from embedded system ... ");
    }

    gdb_handle_unexpected_messages();

    // Read the current message filter value before turning off filtering.
    int rez = gdb_read_memory((unsigned char*)&old_msg_filter, MESSAGE_FILTER_ADDRESS, 4U);
    if (rez != GDB_OK)
    {
        return 1;
    }

    // Pause data logging if the old message filter is not zero.
    if (old_msg_filter != 0)
    {
        if (pause_data_logging() != GDB_OK)
        {
            return 1;
        }
    }

    if (load_rtedbg_structure_header() != GDB_OK)
    {
        return 1;
    }

    if (check_header_info() != GDB_OK)
    {
        return 1;
    }

    if (save_rtedbg_structure() != GDB_OK)
    {
        (void)set_or_restore_message_filter();
        return 1;
    }

    if (check_message_filter_disabled() != GDB_OK)
    {
        set_or_restore_message_filter();
        return 1;
    }

    if (reset_circular_buffer() != GDB_OK)
    {
        return 1;
    }

    if (set_or_restore_message_filter() != GDB_OK)
    {
        return 1;
    }

    if (logging_to_file())
    {
        printf("\nData written to \"%s\"\n", parameters.bin_file_name);
    }

    // Execute the decode batch file if specified.
    execute_decode_batch_file();

    return 0;
}


/***
 * @brief Execute the -decode=name batch file if the command line argument was defined.
 */

static void execute_decode_batch_file(void)
{
    if (parameters.decode_file != NULL)
    {
        printf("\nStarting the batch file: %s", parameters.decode_file);
        int rez = system(parameters.decode_file);
        if (rez != 0)
        {
            printf("\nThe '%s' batch file could not be started!", parameters.decode_file);
        }
        else
        {
            printf("\n");
        }
    }
}


/***
 * @brief Display a list of commands and associated keys.
 */

static void show_help(void)
{
    printf(
        "\n\nAvailable commands:"
        "\n   'Space' - Start data transfer and decoding if the -decode=decode_batch_file argument is used."
        "\n   'F' - Set new filter value."
        "\n   'S' - Switch to single shot mode and restart logging."
        "\n   'P' - Switch to post-mortem mode and restart logging."
        "\n   '0' - Restart the batch file defined with the -start argument."
        "\n   '1' ... '9' - Start the command file 1.cmd ... 9.cmd. "
        "\n   'B' - Benchmark data transfer speed."
        "\n   'H' - Load the data logging structure header and display information."
        "\n   'L' - Enable / disable logging to the log file."
        "\n   '?' - View an overview of available commands."
        "\n   'Esc' - Exit."
        "\n----------------------------------------------------------------------"
        "\n"
    );
}


/***
 * @brief Load and display the g_rtedbg structure header information.
 *        This function first loads the header, checks its validity,
 *        and then prints the header information if valid.
 */

static void load_and_display_rtedbg_structure_header(void)
{
    int rez;
    rez = load_rtedbg_structure_header();
    if (rez != GDB_OK)
    {
        return;
    }

    rez = check_header_info();
    if (rez != GDB_OK)
    {
        printf("\nIncorrect header info (incorrect address or rte_init() not executed).");
        return;
    }

    print_rtedbg_header_info();
}


/***
 * @brief Print names of enabled message filters or their numbers if
 *        the filter name file is not available.
 */

static void print_filter_info(void)
{
    if (rtedbg_header.filter == 0)
    {
        printf("\nMessage filter: 0 (data logging disabled).");
        return;
    }

    FILE* filters = NULL;
    if (parameters.filter_names)
    {
        errno_t err = fopen_s(&filters, parameters.filter_names, "r");
        if (err != 0)
        {
            char error_text[256];
            (void)_strerror_s(error_text, sizeof(error_text), NULL);
            printf("\nCannot open \"%s\" file. Error: %s", parameters.filter_names, error_text);
            close_files_and_exit();
        }
    }

    uint32_t filter = rtedbg_header.filter;
    printf("\nEnabled message filters (0x%08X): ", filter);
    bool filter_number_printed = false;

    // Iterate through each bit in the filter to check which filters are enabled
    for (uint32_t i = 0; i < 32U; i++)
    {
        char filter_name[256U];
        filter_name[0] = '\0';

        // If filter names are provided, read the corresponding name from the file
        if (parameters.filter_names)
        {
            (void)fgets(filter_name, sizeof(filter_name), filters);

            char* newline = strchr(filter_name, '\n');
            if (newline != NULL)
            {
                *newline = '\0';    // Remove the newline character
            }

            // Print the filter info only if the filter is enabled and the name is defined
            if (((filter & 0x80000000UL) != 0) && (*filter_name != '\0'))
            {
                printf("\n%2u - %s", i, filter_name);
            }
        }
        else
        {
            // Print the filter index if no filter names are provided
            if ((filter & 0x80000000UL) != 0)
            {
                if (filter_number_printed)
                {
                    printf(", ");
                }
                printf("%u", i);
                filter_number_printed = true;
            }
        }

        filter <<= 1U; // Shift the filter to check the next bit
    }

    if (filters != NULL)
    {
        (void)fclose(filters);
    }
}


/***
 * @brief Check if the message filter value is disabled. It was set to zero before
 *        the data transfer and should remain zero until the transfer is complete.
 *        Report an error if it is not.
 *
 * @return GDB_OK    - no error
 *         GDB_ERROR - message filter could not be checked or is not zero
 */

static int check_message_filter_disabled(void)
{
    uint32_t message_filter;

    int rez = gdb_read_memory((unsigned char*)&message_filter, MESSAGE_FILTER_ADDRESS, 4U);
    if (rez != GDB_OK)
    {
        return GDB_ERROR;
    }

    if (message_filter != 0)
    {
        printf(
            "\n\nError: At the beginning of the transfer, the message filter was"
            "\nset to 0 to allow uninterrupted data transfer to the host."
            "\nAt the end of the data transfer, the message filter is not zero."
            "\nApparently, the filter was enabled by the firmware. Data "
            "\ntransferred from the embedded system may be partially corrupted.\n"
            );
        return GDB_ERROR;
    }

    return GDB_OK;
}


/***
 * @brief Switch to single shot logging mode. The single shot mode must be
 *        enabled in the firmware.
 */

static void switch_to_single_shot_logging(void)
{
    if (load_rtedbg_structure_header() != GDB_OK)
    {
        return;
    }

    if (!RTE_SINGLE_SHOT_LOGGING_ENABLED)
    {
        printf("\nSingle shot logging not enabled in the firmware.");
        return;
    }

    pause_data_logging();
    RTE_ENABLE_SINGLE_SHOT_MODE;

    int rez = gdb_write_memory((const unsigned char*)&rtedbg_header.rte_cfg, RTE_CFG_WORD_ADDRESS, 4U);
    if (rez != GDB_OK)
    {
        return;
    }

    if (reset_circular_buffer() != GDB_OK)
    {
        return;
    }

    if (set_or_restore_message_filter() != GDB_OK)
    {
        return;
    }

    printf("\nSingle shot logging mode enabled and restarted.");
}


/***
 * @brief Switch to post mortem data logging mode.
 */

static void switch_to_post_mortem_logging(void)
{
    if (load_rtedbg_structure_header() != GDB_OK)
    {
        return;
    }

    pause_data_logging();

    if (RTE_SINGLE_SHOT_WAS_ACTIVE)
    {
        RTE_DISABLE_SINGLE_SHOT_MODE;
        int rez = gdb_write_memory((const unsigned char*)&rtedbg_header.rte_cfg, RTE_CFG_WORD_ADDRESS, 4U);
        if (rez != GDB_OK)
        {
            return;
        }
    }

    if (reset_circular_buffer() != GDB_OK)
    {
        return;
    }

    if (set_or_restore_message_filter() != GDB_OK)
    {
        return;
    }

    if (!RTE_SINGLE_SHOT_WAS_ACTIVE)
    {
        printf("\nPost-mortem mode restarted.");
    }
    else
    {
        printf("\nPost-mortem logging mode enabled and restarted.");
    }
}


/***
 * @brief Print information from the g_rtedbg header structure.
 */

static void print_rtedbg_header_info(void)
{
    printf("\nCircular buffer size: %u words, last index: %u",
        rtedbg_header.buffer_size,
        rtedbg_header.last_index
    );

    printf(", timestamp frequency: %g MHz",
        (double)rtedbg_header.timestamp_frequency / 1e6 / (double)(uint64_t)(1ULL << RTE_TIMESTAMP_SHIFT)
    );

    printf(", long timestamps %s", RTE_USE_LONG_TIMESTAMP ? "enabled" : "disabled");

    if (RTE_SINGLE_SHOT_LOGGING_ENABLED && RTE_SINGLE_SHOT_WAS_ACTIVE)
    {
        printf(", single shot mode");
    }
    else
    {
        printf(", post-mortem mode");
    }

    if (!RTE_MSG_FILTERING_ENABLED)
    {
        printf("\nMessage filtering disabled in the firmware.");
    }
    else
    {
        print_filter_info();
    }
}


/***
 * @brief Set new message filter value.
 *         If the Enter key is pressed without a new value, the old value is retained.
 * 
 * @param filter_value  New filter value, NULL - enter new value manually.
 */

void set_new_filter_value(const char* filter_value)
{
    if (!RTE_MSG_FILTERING_ENABLED)
    {
        printf("\nMessage filtering disabled in the firmware.");
        return;
    }

    unsigned new_filter = 0;
    int no_entered = 0;

    if (filter_value == NULL)
    {
        printf("\nEnter new filter value -> -1=ALL (0x%X): ", parameters.filter);
        char number[50];
        fgets(number, sizeof(number) - 1, stdin);
        no_entered = sscanf_s(number, "%x", &new_filter);
    }
    else
    {
        no_entered = sscanf_s(filter_value, "%x", &new_filter);
    }

    if (no_entered == 1)
    {
        parameters.filter = new_filter;
    }

    parameters.set_filter = true;
        // Always set the embedded system filter even if the value has not been changed

    if (set_or_restore_message_filter() == GDB_OK)
    {
        printf("\nMessage filter set to 0x%X", parameters.filter);
    }
}


/***
 * @brief Execute the memory read benchmark using the GDB server protocol.
 * 
 * The measurement is performed for a longer time (20 s) to show the effects of
 * non-real-time Windows scheduling.
 * The results are first written to a data field.
 * Full results are written to the speed_test.csv file and summary to the console.
 */

static void benchmark_data_transfer(void)
{
    double max_time = 0;
    double min_time = 9e99;
    double time_sum = 0;

    printf("\n\nMeasuring the read memory times...\nWait max. 20 seconds for the benchmark to complete.");

    if (!parameters.log_gdb_communication)
    {
        enable_logging(false);    // Disable logging to speed up the data transfer
    }

    if (load_rtedbg_structure_header() != GDB_OK)
    {
        enable_logging(true);
        return;
    }

    double* time_used = (double*)malloc(BENCHMARK_REPEAT_COUNT * sizeof(double));
    if (time_used == NULL)
    {
        return;
    }

    clock_t benchmark_start = clock_ms();
    size_t measurements;
    for (measurements = 0; measurements < BENCHMARK_REPEAT_COUNT;)
    {
        LARGE_INTEGER start_time;
        start_timer(&start_time);

        int rez = read_memory_block(
            (unsigned char*)p_rtedbg_structure,
            parameters.start_address,
            parameters.size
            );

        double time = time_elapsed(&start_time);
        time_used[measurements] = time;
        time_sum += time;

        if (rez != GDB_OK)
        {
            printf("\nBenchmark terminated prematurely - problem with reading from embedded system.");
            break;
        }

        measurements++;

        if (_kbhit())
        {
            printf("\nBenchmark terminated with a keystroke.\n");
            break;
        }

        if ((clock_ms() - benchmark_start) > MAX_BENCHMARK_TIME_MS)
        {
            break;
        }

        if (time < min_time)
        {
            min_time = time;
        }

        if (time > max_time)
        {
            max_time = time;
        }
    }

    if (measurements > 1)
    {
        double min_speed = (double)parameters.size / max_time;
        double avg_speed = (double)parameters.size * (double)measurements / time_sum;

        FILE* report;
        int rez = fopen_s(&report, "speed_test.csv", "w");
        if (rez != 0)
        {
            char error_text[256];
            (void)_strerror_s(error_text, sizeof(error_text), NULL);
            printf("\nCannot create file 'speed_test.csv' - error: %s.\n", error_text);
        }
        else
        {
            fprintf(report, "Count;Time [ms];Data transfer speed [kB/s]\n");
            for (unsigned i = 0; i < measurements; i++)
            {
                fprintf(report, "%4u;%.1f;%.1f\n",
                    i + 1, time_used[i], (double)parameters.size / time_used[i]);
            }

            fprintf(report,
                "\nMinimal time %.1f ms, maximal time %.1f ms, block size %u bytes."
                "\nMinimal speed %.1f kB/s, average speed: %.1f kB/s.\n",
                min_time, max_time, parameters.size,
                min_speed, avg_speed
            );
            fclose(report);
        }

        printf(
            "\nMinimal time %.1f ms, maximal %.1f ms, block size %u bytes."
            "\nMinimal speed %.1f kB/s, average speed: %.1f kB/s.\n",
            min_time, max_time, parameters.size,
            min_speed, avg_speed
        );
    }

    enable_logging(true);
    free(time_used);
}


/***
 * @brief  Display the status of logging in the embedded system.
 * 
 * @param start_time  Time when the persistent_connection() function started or
 *                    when the logging status was last displayed.
 */

static void display_logging_state(clock_t* start_time)
{
    clock_t current_time = clock_ms();

    if ((current_time - *start_time) < 350)
    {
        Sleep(50);
        return;
    }

    if (!parameters.log_gdb_communication)
    {
        enable_logging(false);
    }

    gdb_handle_unexpected_messages();

    *start_time = current_time;
    int rez = load_rtedbg_structure_header();
    enable_logging(true);

    unsigned size = rtedbg_header.buffer_size - 4U;
    unsigned buffer_usage = (unsigned)((100U * rtedbg_header.last_index + size / 2U) / size);
    if (buffer_usage > 100)
    {
        buffer_usage = 100;
    }

    if (rez == GDB_OK)
    {
        if (RTE_SINGLE_SHOT_WAS_ACTIVE && RTE_SINGLE_SHOT_LOGGING_ENABLED)
        {
            printf("\rIndex:%6d, filter: 0x%08X, %u%% used               ",
                rtedbg_header.last_index, rtedbg_header.filter, buffer_usage);
        }
        else
        {
            printf("\rIndex:%6d, filter: 0x%08X                       ",
                rtedbg_header.last_index, rtedbg_header.filter);
        }
    }
    else
    {
        printf("\rCannot read data from the embedded system.              ");
    }
}


/***
 * @brief  Restart the file defined with the -start=command_file argument.
 */

static void repeat_start_command_file(void)
{
    if (parameters.start_cmd_file == NULL)
    {
        printf("\nCommand file not defined with the -start=command_file argument.");
    }
    else
    {
        (void)gdb_send_commands_from_file(parameters.start_cmd_file);
    }
}



/***
 * @brief Keep connection to the GDB server to enable multiple data transfers.
 *
 * @return 0 - no error
 *         1 - error
 */

static int persistent_connection(void)
{
    int rez = 0;
    clock_t start_time = clock_ms();
        // The time the function was started or the last time the loging status was displayed.

    printf("\nPress the '?' key for a list of available commands.\n");

    for (;;)
    {
        if (!_kbhit())
        {
            display_logging_state(&start_time);
            continue;
        }

        int key = _getch();
        if ((key == 0xE0) || (key == 0))    // Function key?
        {
            (void)_getch();
            key = '\xFF';                   // Unknown command
        }

        switch(toupper(key))
        {
        case '?':
            show_help();
            break;

        case 'H':
            load_and_display_rtedbg_structure_header();
            break;

        case 'B':
            benchmark_data_transfer();
            break;

        case 'S':
            switch_to_single_shot_logging();
            break;

        case 'P':
            switch_to_post_mortem_logging();
            break;

        case 'F':
            set_new_filter_value(NULL);     // Enter new filter value
            break;

        case 'L':
            disable_enable_logging_to_file();
            break;

        case '0':
            repeat_start_command_file();
            break;

        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            send_commands_from_file((char)key);
            break;

        case ' ':
            rez = single_data_transfer();
            if ((rez != 0) && logging_to_file())
            {
                printf("\nError - check the log file for details.\n");
            }
            break;

        case '\x1B':
            printf("\n\nPress the 'Y' button to exit the program.");
            if (toupper(_getch()) == 'Y')
            {
                return 0;
            }
            break;

        default:
            printf("\nUnknown command - Press the '?' key for a list of available commands.");
            break;
        }

        display_errors("\nCould not execute command: ");
    }
}


/***
 * @brief Send commands from a ?.cmd file to the GDB server.
 *
 * @param name_start  This character replaces the '?' in the cmd file name.
 */

static void send_commands_from_file(char name_start)
{
    char cmd_file_name[8];
    cmd_file_name[0] = name_start;
    strcpy_s(&cmd_file_name[1], sizeof(cmd_file_name), ".cmd");
    (void)gdb_send_commands_from_file(cmd_file_name);
}


/***
 * @brief Display error message if the logging is redirected to a log file.
 * 
 * @param message Text message to show in case of error.
 */

static void display_errors(const char* message)
{
    if (!logging_to_file() || (last_gdb_error == 0))
    {
        printf("\n");
        return;
    }

    printf(message);
    switch (last_gdb_error)
    {
        case ERR_CONNECTION_CLOSED:
            printf("connection to GDB server closed.");
            break;

        case ERR_RCV_TIMEOUT:
        case ERR_SEND_TIMEOUT:
        case ERR_SOCKET:
            printf("can not communicate with the GDB server.");
            break;

        case ERR_BAD_MSG_FORMAT:
        case ERR_BAD_MSG_CHECKSUM:
        case ERR_RUN_LENGTH_ENCODING_NOT_IMPLEMENTED:
        case ERR_BAD_INPUT_DATA:
        case ERR_MSG_NOT_SENT_COMPLETELY:
        case ERR_BAD_RESPONSE:
            printf("problem communicating with the GDB server.");
            break;

        case ERR_GDB_REPORTED_ERROR:
            printf("GDB server reported error.");
            break;

        default:
            break;
    }

    last_gdb_error = 0;
    printf("\nCheck the log file for details.\n");
}


/***
 * @brief Get the g_rtedbg structure header from the embedded system.
 *
 * @return GDB_OK    - no error
 *         GDB_ERROR - data not received
 */

static int load_rtedbg_structure_header(void)
{
    int res = gdb_read_memory(
        (unsigned char *)&rtedbg_header, parameters.start_address, sizeof(rtedbg_header));

    if (res != GDB_OK)
    {
        return GDB_ERROR;
    }

    unsigned new_size = rtedbg_header.buffer_size * 4U + sizeof(rtedbg_header_t);

    if ((parameters.size == 0U)             // Automatically obtain the size of the structure
        || (new_size != parameters.size))   // Size changed
    {
        parameters.size = new_size;

        if (parameters.size < MIN_BUFFER_SIZE)
        {
            log_data(
                "\nThe buffer size specified in the g_rtedbg structure header is too small (%llu)",
                (long long)parameters.size);
             log_data(
                " < %llu).\n"
                "Check that the correct data structure address is passed as a parameter and that the rte_init() function has already been called.",
                (long long)MIN_BUFFER_SIZE);
            return GDB_ERROR;
        }

        if (parameters.size > MAX_BUFFER_SIZE)
        {
            log_data(
                "\nThe buffer size specified in the g_rtedbg structure header is too large (%llu)",
                (long long)parameters.size);
            log_data(
                " > %llu).\n"
                "Check that the correct data structure address is passed as a parameter and that the rte_init() function has already been called.",
                (long long)MAX_BUFFER_SIZE);
            return GDB_ERROR;
        }

        if (p_rtedbg_structure != NULL)
        {
            // The size has changed, release the buffer to allocate a new one.
            log_data("\nLog data structure changed to: %llu", new_size);
            free(p_rtedbg_structure);
            p_rtedbg_structure = NULL;
        }
    }

    if (!allocate_memory_for_g_rtedbg_structure())
    {
        return GDB_ERROR;
    }

    return GDB_OK;
}


/***
 * @brief Set the message filter to a new value (if defined as command line argument) or
 *        restore the old version. The filter_copy value is used if the filter is zero and
 *        the firmware can disable message filtering.
 *
 * @return GDB_OK    - no error
 *         GDB_ERROR - could not restore filter
 */

static int set_or_restore_message_filter(void)
{
    uint32_t old_filter = old_msg_filter;

    if ((old_filter == 0) && RTE_FILTER_OFF_ENABLED)
    {
        old_filter = rtedbg_header.filter_copy;
    }

    if (parameters.set_filter)
    {
        old_filter = parameters.filter;     // User defined filter value (command line argument)
    }

    return gdb_write_memory((const unsigned char *)&old_filter, MESSAGE_FILTER_ADDRESS, 4U);
}


/***
 * @brief Read the complete g_rtedbg structure from the embedded system and write it to a file.
 * 
 * @return GDB_OK    - no error
 *         GDB_ERROR - data not received or file operation failed
 */

static int save_rtedbg_structure(void)
{
    if (p_rtedbg_structure == NULL)
    {
        return GDB_ERROR;
    }

    delay_before_data_transfer();
    int err = read_memory_block(
        (unsigned char *)p_rtedbg_structure,
        parameters.start_address,
        parameters.size);
    if (err != GDB_OK)
    {
        return GDB_ERROR;
    }

    FILE * bin_file;
    errno_t rez = fopen_s(&bin_file, parameters.bin_file_name, "wb");

    if (rez != 0)
    {
        printf("\n************************************************************");
        log_string("\nCould not create file \"%s\"", parameters.bin_file_name);
        char err_string[256];
        (void)strerror_s(err_string, sizeof(err_string), errno);
        log_string(": %s", err_string);

        if (logging_to_file())
        {
            printf("\nCould not create file \"%s\"", parameters.bin_file_name);
            printf(": %s", err_string);
        }

        printf("\n************************************************************\n");
        return GDB_ERROR;
    }

    // Restore the old message filter (as it was before logging was disabled)
    p_rtedbg_structure[1] = old_msg_filter;

    size_t written = fwrite(p_rtedbg_structure, 1U, parameters.size, bin_file);
    if (written != parameters.size)
    {
        log_string("\nCould not write to the file: %s.", parameters.bin_file_name);
        char error_text[256];
        (void)_strerror_s(error_text, sizeof(error_text), NULL);
        log_string(" Error: %s", error_text);
        (void)fclose(bin_file);

        if (logging_to_file())
        {
            printf("\nCould not write to the file: %s.", parameters.bin_file_name);
            printf(" Error: %s", error_text);
        }

        return GDB_ERROR;
    }

    (void)fclose(bin_file);
    return GDB_OK;
}


/***
 * @brief Read a block of memory from the embedded system.
 *
 * @param  buffer       buffer allocated for the read block
 * @param  address      start address of embedded system memory
 * @param  block_size   size in bytes
 *
 * @return GDB_OK    - no error
 *         GDB_ERROR - data not received
 */

static int read_memory_block(unsigned char * buffer, uint32_t address, uint32_t block_size)
{
    LARGE_INTEGER start_time;
    start_timer(&start_time);
    int res = gdb_read_memory(buffer, address, block_size);

    if (res != GDB_OK)
    {
        return GDB_ERROR;
    }

    log_data(", %llu kB/s. ",
        (long long)(block_size / time_elapsed(&start_time)));

    return GDB_OK;
}


/***
 * @brief Pause data logging by erasing the message filter variable.
 *
 * @return GDB_OK    - no error
 *         GDB_ERROR - data not written
 */

static int pause_data_logging(void)
{
    return gdb_write_memory(
        (const unsigned char *)"\x00\x00\x00\x00", MESSAGE_FILTER_ADDRESS, 4U);
}


/***
 * @brief Erase the circular buffer index.
 * 
 * @return GDB_OK    - no error
 *         GDB_ERROR - buffer index not erased (data not written)
 */

static int erase_buffer_index(void)
{
    return gdb_write_memory(
        (const unsigned char *)"\x00\x00\x00\x00", parameters.start_address, 4U);
}


/***
 * @brief Check if the single shot logging was enabled and is active.
 * 
 * @return true  - single shot enabled and active
 *         false - single shot disabled (compile time) or enabled but not active
 */

static bool single_shot_active(void)
{
    if ((RTE_SINGLE_SHOT_WAS_ACTIVE != 0) && (RTE_SINGLE_SHOT_LOGGING_ENABLED != 0))
    {
        return true;
    }

    return false;
}


/***
 * @brief Reset the complete contents of the circular buffer to 0xFFFFFFFF if enabled
 *        or reset just the buffer index if not enabled and single shot logging
 *        was active.
 * 
 * @return GDB_OK    - no error
 *         GDB_ERROR - buffer not reset
 */

static int reset_circular_buffer(void)
{
    int rez = GDB_OK;

    if (parameters.clear_buffer)
    {
        unsigned circular_buffer_size = parameters.size - sizeof(rtedbg_header);
        unsigned char* circular_buffer = (unsigned char*)malloc(circular_buffer_size);
        if (circular_buffer == NULL)
        {
            log_string("\nCould not allocate memory buffer.", NULL);
            return GDB_ERROR;
        }

        memset(circular_buffer, 0xFFU, circular_buffer_size);

        LARGE_INTEGER start_time;
        start_timer(&start_time);

        if (logging_to_file())
        {
            printf("\nClearing the circular buffer ...");
        }

        rez = gdb_write_memory(
            circular_buffer,
            parameters.start_address + sizeof(rtedbg_header_t),
            circular_buffer_size);
        free(circular_buffer);

        if (rez != GDB_OK)
        {
            return GDB_ERROR;
        }

        log_data(", %llu kB/s. ",
            (long long)((parameters.size - sizeof(rtedbg_header)) / time_elapsed(&start_time)));
    }

    if (parameters.clear_buffer || single_shot_active())
    {
        rez = erase_buffer_index();   // Restart logging at the start of the circular buffer
    }

    return rez;
}


/***
 * @brief Check that the information in the g_rtedbg header is correct. 
 * The tester may be using the wrong g_rtedbg structure address,
 * or the structure may not be initialized.
 *
 * @return GDB_OK    - no error
 *         GDB_ERROR - incorrect header information
 */

static int check_header_info(void)
{
    if ((sizeof(rtedbg_header_t) != RTE_HEADER_SIZE) 
        || (RTE_CFG_RESERVED_BITS != 0)
        || (RTE_CFG_RESERVED2 != 0)
        )
    {
        log_string(
            "\nError in the g_rtedbg structure header (incorrect header size / reserved bits).\n"
            "Check that the correct data structure address is passed as a parameter"
            " and that the rte_init() function has already been called.",
            NULL
        );
        return GDB_ERROR;
    }

    return GDB_OK;
}


/***
 * @brief Close all open files, clean up gdb connection, and exit with return code 1.
 */

__declspec(noreturn) void close_files_and_exit(void)
{
    decrease_priorities();
    gdb_detach();
    gdb_socket_cleanup();

    if (parameters.log_file != NULL)
    {
        printf("\n\nAn error occurred during the transfer of data from the embedded system."
            "\nThe log file contains further details.\n\n");
    }

    (void)_fcloseall();
    exit(1);
}


/***
 * @brief Initialize the data structure header and set the circular buffer to 0xFFFFFFFF.
 *        This function is intended for projects where rte_init() is not called - for
 *        resource constrained systems.
 * 
 * @param cfg_word              g_rtedbg configuration word
 * @param timestamp_frequency   timestamp timer clock frequency
 */

void initialize_data_logging_structure(unsigned cfg_word, unsigned timestamp_frequency)
{
    if (timestamp_frequency == 0)
    {
        log_string("- the timestamp frequency must not be zero", NULL);
        return;
    }

    if (parameters.size == 0)
    {
        log_string("- the size command line argument must not be zero", NULL);
        return;
    }

    rtedbg_header_t rtedbg;
    rtedbg.last_index = 0;
    rtedbg.filter = 0;
    rtedbg.filter_copy = parameters.filter;
    rtedbg.buffer_size = (parameters.size - sizeof(rtedbg_header_t)) / 4U;
    rtedbg.timestamp_frequency = timestamp_frequency;
    rtedbg.rte_cfg = cfg_word;

    // Disable logging during the g_rtedbg structure initialization
    if (pause_data_logging() != GDB_OK)
    {
        return;
    }

    // Write the header of the g_rtedbg structure to the embedded system
    int rez = gdb_write_memory((const unsigned char*)&rtedbg, parameters.start_address, sizeof(rtedbg));
    if (rez != GDB_OK)
    {
        return;
    }

    if (reset_circular_buffer() != GDB_OK)
    {
        return;
    }

    if (parameters.filter != 0)
    {
        // Enable logging
        rez = gdb_write_memory((const unsigned char*)&parameters.filter, MESSAGE_FILTER_ADDRESS, 4U);
        if (rez != GDB_OK)
        {
            return;
        }
    }

    log_string("\nThe g_rtedbg data logging structure has been initialized. ", NULL);
}


/***
 * @brief Execute a delay if defined with a command line parameter.
 *        The delay enables low priority tasks to finish writing to the circular buffer.
 */

static void delay_before_data_transfer(void)
{
    if (parameters.delay > 0)
    {
        log_data("\nDelay %llu ms", (long long)parameters.delay);
        Sleep(parameters.delay);
            // Wait for low priority tasks to finish writing to the circular buffer
    }
}


/***
 * @brief Allocate memory for the g_rtedbg logging structure if the size is known.
 * 
 * @return true if memory was successfully allocated or already allocated
 *         false if memory could not be allocated
 */

static bool allocate_memory_for_g_rtedbg_structure(void)
{
    if (parameters.size == 0)
    {
        return false;
    }

    if (p_rtedbg_structure != NULL)
    {
        return true;    // Memory already allocated
    }

    p_rtedbg_structure = (unsigned*)malloc(parameters.size);
    if (p_rtedbg_structure == NULL)
    {
        log_string("\nCould not allocate memory buffer.", NULL);
        return false;
    }

    return true;        // Memory allocation successful
}


/***
 * @brief Get process ID by process name
 * 
 * @param processName - name of the process (name of its executable file)
 * 
 * @return process ID or 0 if the process is not found
 */

static DWORD GetProcessIdByName(const char* processName)
{
    PROCESSENTRY32 processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    // Convert process name from char to wide char
    WCHAR appNameW[_MAX_PATH];
    (void)mbstowcs_s(NULL, appNameW, _MAX_PATH, processName, _TRUNCATE);

    if (Process32First(snapshot, &processEntry))
    {
        do
        {
            if (wcscmp(processEntry.szExeFile, appNameW) == 0)
            {
                (void)CloseHandle(snapshot);
                return processEntry.th32ProcessID;
            }
        }
        while (Process32Next(snapshot, &processEntry));
    }

    (void)CloseHandle(snapshot);
    return 0;
}


/***
 * @brief Set the process priority to the desired value.
 * 
 * @param processName      Name of the process (e.g., "notepad.exe").
 * @param dwPriorityClass  Priority class (e.g., HIGH_PRIORITY_CLASS).
 * @param report_error     Report errors if true.
 */

static void set_process_priority(const char* processName, DWORD dwPriorityClass, bool report_error)
{
    DWORD processId = GetProcessIdByName(processName);

    if (processId == 0)
    {
        if (report_error)
        {
            log_string("\nProcess %s not found.", processName);
        }
        return;
    }

    HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, processId);
    if (hProcess == NULL)
    {
        if (report_error)
        {
            log_string("\nUnable to get handle for process %s.", processName);
            log_data(" Error: %lu", GetLastError());
        }
        return;
    }

    if (!SetPriorityClass(hProcess, dwPriorityClass))
    {
        if (report_error)
        {
            log_string("\nFailed to set priority for process %s.", processName);
            log_data(" Error: %lu",GetLastError());
        }
        (void)CloseHandle(hProcess);
        return;
    }

    (void)CloseHandle(hProcess);
}


/***
 * @brief Increase process priorities for the RTEgdbData process and all processes defined
 *        with the -driver command line arguments.
 * 
 * This function checks if the elevated_priority flag is set in the parameters.
 * If the flag is set, it attempts to increase the priority of the current process
 * (RTEgdbData) to REALTIME_PRIORITY_CLASS. If this operation fails, it logs an error.
 * It then iterates through the list of driver names provided in the parameters and
 * attempts to set their process priorities to REALTIME_PRIORITY_CLASS using the
 * set_process_priority function.
 * Note: HIGH_PRIORITY_CLASS is set if RTEgdbData is not started with admin level.
 */

static void increase_priorities(void)
{
    if (parameters.elevated_priority)
    {
        // Increase RTEgdbData process priority
        BOOL success = SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
        if (!success)
        {
            log_data("\nError setting RTEgdbData priority: %llu.", 
                     (long long)GetLastError());
        }

        // Increase GDB server and other related process priorities
        for (size_t i = 0; i < parameters.number_of_drivers; i++)
        {
            set_process_priority(parameters.driver_names[i], REALTIME_PRIORITY_CLASS, true);
        }
    }
}


/***
 * @brief Set process priorities to normal for all processes defined
 *        with the -driver command line arguments and the RTEgdbData process.
 * 
 * This function checks if the elevated_priority flag is set in the parameters.
 * If the flag is set, it attempts to set the priority of the current process
 * (RTEgdbData) to NORMAL_PRIORITY_CLASS. If this operation fails, it logs an error.
 * It then iterates through the list of driver names provided in the parameters and
 * attempts to set their process priorities to NORMAL_PRIORITY_CLASS using the
 * set_process_priority function.
 */

static void decrease_priorities(void)
{
    if (parameters.elevated_priority)
    {
        // Decrease RTEgdbData process priority
        (void)SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);

        // Decrease GDB server and other related process priorities
        for (size_t i = 0; i < parameters.number_of_drivers; i++)
        {
            set_process_priority(parameters.driver_names[i], NORMAL_PRIORITY_CLASS, false);
        }
    }
}


/***
 * @brief How much time [ms] has passed since the CRT initialization during process start.
 * 
 * @return Time elapsed in miliseconds.
 */

long clock_ms(void)
{
    clock_t time = clock();

    if (time == -1)
    {
        return -1;
    }

    double time_ms = 0.5 + (double)time / (double)CLOCKS_PER_SEC * 1000.;

    if (time_ms > (double)INT32_MAX)
    {
        return -1;
    }

    return (long)time_ms;
}


/*==== End of file ====*/
