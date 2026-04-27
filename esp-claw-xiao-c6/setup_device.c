/*
 * SPDX-FileCopyrightText: 2026 Rich Washburn / kaosrmw-eng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board support for Seeed Studio XIAO ESP32-C6
 * - Onboard WS2812 RGB LED on GPIO 8
 * - No PSRAM — all tasks run from internal SRAM
 * - No display
 */
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_board_manager_includes.h"
#include "gen_board_device_custom.h"
#include "periph_rmt.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "led_strip_types.h"
