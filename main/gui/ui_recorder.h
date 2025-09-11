/*
 * SPDX-FileCopyrightText: 2024 VoiceRecorder Project
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the voice recorder UI
 * 
 * @param end_cb Callback function to call when exiting the recorder
 */
void ui_recorder_start(void (*end_cb)(void));

/**
 * @brief End the voice recorder UI
 */
void ui_recorder_end(void);

#ifdef __cplusplus
}
#endif
