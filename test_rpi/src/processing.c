/*
 * Copyright (c) 2025 Igalia S.L.
 * Author: Ricardo Cañuelo Navarro <rcn@igalia.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "common.h"

LOG_MODULE_DECLARE(test_rpi, CONFIG_TEST_RPI_LOG_LEVEL);

static char data_out[PROC_MSG_SIZE];

/*
 * Receives a message on the message queue passed in p1, does some
 * processing on the data received and sends a response on the message
 * queue passed in p2.
 */
void data_process(void *p1, void *p2, void *p3)
{
	struct k_msgq *inq = p1;
	struct k_msgq *outq = p2;
	ARG_UNUSED(p3);

	while (1) {
		unsigned int data;

		k_msgq_get(inq, &data, K_FOREVER);
		LOG_DBG("Received: %d", data);

		/* Data processing: convert integer to string */
		snprintf(data_out, sizeof(data_out), "%d", data);

		k_msgq_put(outq, data_out, K_NO_WAIT);
	}
}
