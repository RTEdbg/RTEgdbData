/*
 * Copyright (c) 2024 Branko Premzel.
 *
 * SPDX-License-Identifier: MIT
 */

/***
 * @file      gdb_lib.h
 * @brief     GDB library function declarations and definitions.
 * @author    B. Premzel
 */

#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <Windows.h>
#include <time.h>
#include "gdb_defs.h"

extern char message_buffer[];
extern unsigned last_gdb_error;
extern clock_t app_start_time;

#define GDB_ERROR   1
#define GDB_OK      0

int  gdb_connect(unsigned short gdb_port);
int  gdb_connect_socket(unsigned short gdb_port);
int  gdb_read_memory(unsigned char * buffer, unsigned int address, unsigned int length);
int  gdb_write_memory(const unsigned char * buffer, unsigned address, unsigned length);
int  gdb_check_server_capabilities(void);
void gdb_detach(void);
int  gdb_execute_command(const char * command);
void gdb_flush_socket(void);
int gdb_request_no_ack_mode(void);
void gdb_socket_cleanup(void);
void gdb_handle_unexpected_messages(void);
int gdb_send_commands_from_file(const char * cmd_file);

/*==== End of file ====*/
