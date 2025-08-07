/*
 * Copyright (c) 2025 Igalia S.L.
 * Author: Ricardo Cañuelo Navarro <rcn@igalia.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/random/random.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c_emul.h>
#include "common.h"

LOG_MODULE_DECLARE(test_rpi, CONFIG_TEST_RPI_LOG_LEVEL);

extern const struct gpio_dt_spec button;
extern const struct device *i2c_target;

/*
 * A real controller may want to continue reading after the first
 * received byte. We're implementing repeated-start semantics so we'll
 * only be sending one byte per transfer, but we need to allocate space
 * for an extra byte to process the possible additional read request.
 */
static uint8_t emul_read_buf[2];

/*
 * Emulates a single I2C READ START request from a controller.
 */
static uint8_t *i2c_emul_read(void)
{
	struct i2c_msg msg;
	int ret;

	msg.buf = emul_read_buf;
	msg.len = sizeof(emul_read_buf);
	msg.flags = I2C_MSG_RESTART | I2C_MSG_READ;
	ret = i2c_transfer(i2c_target, &msg, 1, I2C_ADDR);
	if (ret == -EIO)
		return NULL;

	return emul_read_buf;
}

static void i2c_emul_write(uint8_t *data, int len)
{
	struct i2c_msg msg;

	/*
	 * NOTE: It's not explicitly said anywhere that msg.buf can be
	 * NULL even if msg.len is 0. The behavior may be
	 * driver-specific and prone to change so we're being safe here
	 * by using a 1-byte buffer.
	 */
	msg.buf = data;
	msg.len = len;
	msg.flags = I2C_MSG_WRITE;
	i2c_transfer(i2c_target, &msg, 1, I2C_ADDR);
}

/*
 * Emulates an explicit I2C STOP sent from a controller.
 */
static void i2c_emul_stop(void)
{
	struct i2c_msg msg;
	uint8_t buf = 0;

	/*
	 * NOTE: It's not explicitly said anywhere that msg.buf can be
	 * NULL even if msg.len is 0. The behavior may be
	 * driver-specific and prone to change so we're being safe here
	 * by using a 1-byte buffer.
	 */
	msg.buf = &buf;
	msg.len = 0;
	msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;
	i2c_transfer(i2c_target, &msg, 1, I2C_ADDR);
}

/*
 * Emulates an I2C "UPTIME" command request from a controller using
 * repeated start.
 */
static void i2c_emul_uptime(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t buffer[PROC_MSG_SIZE] = {0};
	i2c_register_t reg = I2C_REG_UPTIME;
	int i;

	i2c_emul_write((uint8_t *)&reg, 1);
	for (i = 0; i < PROC_MSG_SIZE; i++) {
		uint8_t *b = i2c_emul_read();
		if (b == NULL)
			break;
		buffer[i] = *b;
	}
	i2c_emul_stop();

	if (i == PROC_MSG_SIZE) {
		shell_print(sh, "%s", buffer);
	} else {
		shell_print(sh, "Transfer error");
	}
}

/*
 * Emulates a button press with bouncing.
 */
static void button_press(void)
{
	const struct device *dev = device_get_binding(button.port->name);
	int n_bounces = sys_rand8_get() % 10;
	int state = 1;
	int i;

	/* Press */
	gpio_emul_input_set(dev, 0, state);
	/* Bouncing */
	for (i = 0; i < n_bounces; i++) {
		state = state ? 0: 1;
		k_busy_wait(1000 * (sys_rand8_get() % 10));
		gpio_emul_input_set(dev, 0, state);
	}
	/* Stabilization */
	gpio_emul_input_set(dev, 0, 1);
	k_busy_wait(100000);
	/* Release */
	gpio_emul_input_set(dev, 0, 0);
}

SHELL_CMD_REGISTER(buttonpress, NULL, "Simulates a button press", button_press);
SHELL_CMD_REGISTER(i2cread, NULL, "Simulates an I2C read request", i2c_emul_read);
SHELL_CMD_REGISTER(i2cuptime, NULL, "Simulates an I2C uptime request", i2c_emul_uptime);
SHELL_CMD_REGISTER(i2cstop, NULL, "Simulates an I2C stop request", i2c_emul_stop);
