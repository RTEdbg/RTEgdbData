/*
 * Copyright (c) Branko Premzel.
 *
 * SPDX-License-Identifier: MIT
 */

 /*******************************************************************************
 * @file    rtedbg.h
 * @author  B. Premzel
 * @brief   Structure definitions for data from the embedded system
 * @note    The structures defined here must be the same as in the embedded system
 *          library since the data is read from the files in binary form.
 ******************************************************************************/

#ifndef  _RTEDBG_H
#define  _RTEDBG_H

#include "stdint.h"

/***********************************************************************************
 * The configuration word defines the embedded system RTEdbg configuration.
 * Bit    0: 0 - post-mortem logging is active (default)
 *           1 - single shot logging is active
 *        1: 1 - RTE_MSG_FILTERING_ENABLED
 *        2: 1 - RTE_FILTER_OFF_ENABLED
 *        3: 1 - RTE_SINGLE_SHOT_LOGGING_ENABLED
 *        4: 1 - RTE_USE_LONG_TIMESTAMP
 *  5 ..  7: reserved for future use (must be 0)
 *  8 .. 11: RTE_TIMESTAMP_SHIFT (0 = shift by 1)
 * 12 .. 14: RTE_FMT_ID_BITS   (0 = 9, 7 = 16)
 * 15      : reserved for future use (must be 0)
 * 16 .. 23: RTE_MAX_SUBPACKETS  (1 .. 256 - value 0 = 256)
 * 24 .. 30: RTE_HDR_SIZE (header size - number of 32-bit words)
 *       31: RTE_BUFF_SIZE_IS_POWER_OF_2 (1 = buffer size is power of 2, 0 - is not)
 ***********************************************************************************/
// Macros for parsing rte_cfg from the rtedbg_header_t
#define RTE_SINGLE_SHOT_WAS_ACTIVE      ((rtedbg_header.rte_cfg  >>  0U) & 1U)
#define RTE_MSG_FILTERING_ENABLED       ((rtedbg_header.rte_cfg  >>  1U) & 1U)
#define RTE_FILTER_OFF_ENABLED          ((rtedbg_header.rte_cfg  >>  2U) & 1U)
#define RTE_SINGLE_SHOT_LOGGING_ENABLED ((rtedbg_header.rte_cfg  >>  3U) & 1U)
#define RTE_USE_LONG_TIMESTAMP          ((rtedbg_header.rte_cfg  >>  4U) & 1U)
#define RTE_CFG_RESERVED_BITS           ((rtedbg_header.rte_cfg  >>  5U) & 0x07U)
#define RTE_TIMESTAMP_SHIFT             (((rtedbg_header.rte_cfg >>  8U) & 0x0FU) + 1U)
#define RTE_FMT_ID_BITS                 ((rtedbg_header.rte_cfg  >> 12U) & 0x07U)
#define RTE_CFG_RESERVED2               ((rtedbg_header.rte_cfg  >> 15U) & 0x01U)
#define RTE_MAX_MSG_BLOCKS              (((rtedbg_header.rte_cfg >> 16U) & 0xFFU) ? \
                                         ((rtedbg_header.rte_cfg >> 16U) & 0xFFU) : 256U)
#define RTE_HEADER_SIZE                 (((rtedbg_header.rte_cfg >> 24U) & 0x7FU) * 4U)
#define RTE_BUFF_SIZE_IS_POWER_OF_2     ((rtedbg_header.rte_cfg  >> 31U) & 1U)

#define RTE_ENABLE_SINGLE_SHOT_MODE     rtedbg_header.rte_cfg |= 1U;
#define RTE_DISABLE_SINGLE_SHOT_MODE    rtedbg_header.rte_cfg &= ~1U;


/**************************************************************************************
 * @brief Embedded system data logging structure header (without circular buffer).
 *************************************************************************************/
typedef struct
{
    volatile uint32_t last_index;       /*!< Index to the circular data logging buffer. */
    volatile uint32_t filter;           /*!< Enable/disable 32 message filters - each bit enables a group of messages. */
    uint32_t rte_cfg;                   /*!< The RTEdbg configuration. */
    uint32_t timestamp_frequency;       /*!< Frequency of the timestamp counter [Hz]. */
    uint32_t filter_copy;               /*!< Copy of the filter value to indicate the last non-zero value before the message
                                             logging has been stopped. */
    uint32_t buffer_size;               /*!< Size of the circular data logging buffer. */
} rtedbg_header_t;

#endif   // _RTEDBG_H

/*==== End of file ====*/
