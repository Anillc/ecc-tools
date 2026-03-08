// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file Log.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-08
 * @brief Routing compatibility logging macros backed by iCTS logger
 */

#pragma once

#include "utils/logger/Logger.hh"

#define LOG_INFO CTS_LOG_INFO
#define LOG_WARNING CTS_LOG_WARNING
#define LOG_ERROR CTS_LOG_ERROR
#define LOG_FATAL CTS_LOG_FATAL

#define LOG_INFO_IF CTS_LOG_INFO_IF
#define LOG_WARNING_IF CTS_LOG_WARNING_IF
#define LOG_ERROR_IF CTS_LOG_ERROR_IF
#define LOG_FATAL_IF CTS_LOG_FATAL_IF
