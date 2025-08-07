/*
 * Copyright (c) 2025 Igalia S.L.
 * Author: Ricardo Cañuelo Navarro <rcn@igalia.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _COMMON_H
#define _COMMON_H

#include <zephyr/drivers/gpio.h>

#define PROC_MSG_SIZE		8
#define THREAD_STACKSIZE	2048
#define THREAD_PRIORITY		10
#define I2C_ADDR		0x60

typedef enum {
	I2C_REG_UPTIME,
	I2C_REG_NOT_SUPPORTED,

	I2C_REG_DEFAULT = I2C_REG_UPTIME
} i2c_register_t;

void data_process(void *p1, void *p2, void *p3);

#endif
