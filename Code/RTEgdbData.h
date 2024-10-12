/*
 * Copyright (c) 2024 Branko Premzel.
 *
 * SPDX-License-Identifier: MIT
 */

/***
 * @file      RTEgdbData.h
 * @author    B. Premzel
 */


#define MIN_BUFFER_SIZE  (64U + 16U)    // Minimum buffer size for g_rtedbg circular buffer
#define MAX_BUFFER_SIZE  2100000U       // Maximum buffer size for g_rtedbg circular buffer
#define MESSAGE_FILTER_ADDRESS  (parameters.start_address + offsetof(rtedbg_header_t, filter))
                                        // Address of the message filter
#define RTE_CFG_WORD_ADDRESS    (parameters.start_address + offsetof(rtedbg_header_t, rte_cfg))
                                        // Address of the RTE configuration word

#define MAX_DRIVERS 5                   // Maximum number of drivers that should get elevated execution priority
#define BENCHMARK_REPEAT_COUNT 1000     // Maximum number of data transfers in the benchmark
#define MAX_BENCHMARK_TIME_MS 20000     // Maximum time for the data transfer benchmark in milliseconds

__declspec(noreturn) void close_files_and_exit(void);
void initialize_data_logging_structure(unsigned cmd_word, unsigned timestamp_frequency);
void set_new_filter_value(const char* filter_value);
long clock_ms(void);

/*==== End of file ====*/
