/*
 * Copyright (c) 2024 Branko Premzel.
 *
 * SPDX-License-Identifier: MIT
 */

/***
 * @file    logger.h
 * @author  B. Premzel
 * @brief   Header file for logging functions.
 */


#pragma once
#include <stdio.h>
#include <WinSock2.h>
#include <Windows.h>

void enable_logging(bool on_off);
void create_log_file(const char* file_name);
void start_timer(LARGE_INTEGER * start_timer);
void log_data(const char * text, long long int data);
void log_string(const char * text, const char * string);
void log_timing(const char * text, LARGE_INTEGER * start);
void log_wsock_error(const char * text);
double time_elapsed(LARGE_INTEGER * start_timer);
void log_communication(const char* direction, const char* msg, int length);
bool logging_to_file(void);
void disable_enable_logging_to_file(void);


/*==== End of file ====*/
