/*
 * Copyright (c) 2025 Igalia S.L.
 * Author: Ricardo Cañuelo Navarro <rcn@igalia.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include "common.h"

#define THREAD_STACKSIZE	2048
#define THREAD_PRIORITY		10

LOG_MODULE_REGISTER(test_rpi, CONFIG_TEST_RPI_LOG_LEVEL);

K_MSGQ_DEFINE(in_msgq, sizeof(int), 1, 1);
K_MSGQ_DEFINE(out_msgq, PROC_MSG_SIZE, 1, 1);

K_THREAD_STACK_DEFINE(processing_stack, THREAD_STACKSIZE);

static char str_data[PROC_MSG_SIZE];
K_MUTEX_DEFINE(str_data_mutex);

/*
 * Get button configuration from the devicetree zephyr,user node.
 */
#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(
	ZEPHYR_USER_NODE, button_gpios, {0});

/*
 * Get I2C device configuration from the devicetree i2ctarget alias.
 * Check node availability at buid time.
 */
#define I2C_NODE	DT_ALIAS(i2ctarget)
#if !DT_NODE_HAS_STATUS_OKAY(I2C_NODE)
#error "Unsupported board: i2ctarget devicetree alias is not defined"
#endif
const struct device *i2c_target = DEVICE_DT_GET(I2C_NODE);
/* I2C data structures */
static char i2cbuffer[PROC_MSG_SIZE];
static int i2cidx = -1;
static i2c_register_t i2creg = I2C_REG_DEFAULT;

/*
 * Callback called on a write request from the controller.
 */
int write_requested_cb(struct i2c_target_config *config)
{
	LOG_DBG("I2C WRITE start");
	return 0;
}

/*
 * Callback called when a byte was received on an ongoing write request
 * from the controller.
 */
int write_received_cb(struct i2c_target_config *config, uint8_t val)
{
	LOG_DBG("I2C WRITE: 0x%02x", val);
	i2creg = val;
	if (val == I2C_REG_UPTIME)
		i2cidx = -1;

	return 0;
}

/*
 * Callback called on a read request from the controller.
 * If it's a first read, load the output buffer contents from the
 * current contents of the source data buffer (str_data).
 *
 * The data byte sent to the controller is pointed to by val.
 * Returns:
 *   0 if there's additional data to send
 *   -ENOMEM if the byte sent is the end of the data transfer
 *   -EIO if the selected register isn't supported
 */
int read_requested_cb(struct i2c_target_config *config, uint8_t *val)
{
	if (i2creg != I2C_REG_UPTIME)
		return -EIO;

	LOG_DBG("I2C READ started. i2cidx: %d", i2cidx);
	if (i2cidx < 0) {
		/* Copy source buffer to the i2c output buffer */
		k_mutex_lock(&str_data_mutex, K_FOREVER);
		strncpy(i2cbuffer, str_data, PROC_MSG_SIZE);
		k_mutex_unlock(&str_data_mutex);
	}
	i2cidx++;
	if (i2cidx == PROC_MSG_SIZE) {
		i2cidx = -1;
		return -ENOMEM;
	}
	*val = i2cbuffer[i2cidx];
	LOG_DBG("I2C READ send: 0x%02x", *val);

	return 0;
}

/*
 * Callback called on a continued read request from the
 * controller. We're implementing repeated start semantics, so this will
 * always return -ENOMEM to signal that a new START request is needed.
 */
int read_processed_cb(struct i2c_target_config *config, uint8_t *val)
{
	LOG_DBG("I2C READ continued");
	return -ENOMEM;
}

/*
 * Callback called on a stop request from the controller. Rewinds the
 * index of the i2c data buffer to prepare for the next send.
 */
int stop_cb(struct i2c_target_config *config)
{
	i2cidx = -1;
	LOG_DBG("I2C STOP");
	return 0;
}

static struct i2c_target_callbacks target_callbacks = {
	.write_requested = write_requested_cb,
	.write_received = write_received_cb,
	.read_requested = read_requested_cb,
	.read_processed = read_processed_cb,
	.stop = stop_cb,
};


/*
 * Deferred irq work triggered by the GPIO IRQ callback
 * (button_pressed). This should run some time after the ISR, at which
 * point the button press should be stable after the initial bouncing.
 *
 * Checks the button status and sends the current system uptime in
 * seconds through in_msgq if the the button is still pressed.
 */
static void debounce_expired(struct k_work *work)
{
	ARG_UNUSED(work);

	unsigned int data = k_uptime_seconds();

	if (gpio_pin_get_dt(&button))
		k_msgq_put(&in_msgq, &data, K_NO_WAIT);
}

static K_WORK_DELAYABLE_DEFINE(debounce_work, debounce_expired);

/*
 * Callback function for the button GPIO IRQ.
 * De-bounces the button press by scheduling the processing into a
 * workqueue.
 */
void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_work_reschedule(&debounce_work, K_MSEC(30));
}

struct k_thread processing_thread;

int main(void)
{
	static struct gpio_callback button_cb_data;
	int ret;

	struct i2c_target_config target_cfg = {
		.address = I2C_ADDR,
		.callbacks = &target_callbacks,
	};

	if (i2c_target_register(i2c_target, &target_cfg) < 0) {
		LOG_ERR("Failed to register target");
		return -1;
	}

	/* Button/GPIO setup */
	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("Error: button device %s is not ready",
		       button.port->name);
		return 0;
	}
	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d",
		       ret, button.port->name, button.pin);
		return 0;
	}
	ret = gpio_pin_interrupt_configure_dt(&button,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure interrupt on %s pin %d",
			ret, button.port->name, button.pin);
		return 0;
	}
	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);

	/* Thread initialization */
	k_thread_create(&processing_thread, processing_stack,
			THREAD_STACKSIZE, data_process,
			&in_msgq, &out_msgq, NULL,
			THREAD_PRIORITY, 0, K_FOREVER);
	k_thread_name_set(&processing_thread, "processing");
	k_thread_start(&processing_thread);

	/* DBG: Main loop */
	while (1) {
		char buffer[PROC_MSG_SIZE];

		k_msgq_get(&out_msgq, buffer, K_FOREVER);
		LOG_DBG("Received: %s", buffer);
		k_mutex_lock(&str_data_mutex, K_FOREVER);
		strncpy(str_data, buffer, PROC_MSG_SIZE);
		k_mutex_unlock(&str_data_mutex);
	}
	return 0;
}
