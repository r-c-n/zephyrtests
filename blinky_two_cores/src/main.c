/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/device.h>
#include <zephyr/drivers/ipm.h>

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static const struct device *const ipm_handle =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_ipc));


/********** Core 1 code **********/
static inline void busy_wait(int loops)
{
	int i;

	for (i = 0; i < loops; i++)
		__asm__ volatile("nop");
}

#include <hardware/structs/sio.h>
static void core1_entry()
{
	int i = 0;

	while (1) {
		busy_wait(20000000);
		sio_hw->fifo_wr = i++;
	}
}
/********** End of Core 1 code **********/

#define CORE1_STACK_SIZE 256
char core1_stack[CORE1_STACK_SIZE];
uint32_t vector_table[16];
K_MSGQ_DEFINE(ip_msgq, sizeof(int), 4, 1);

static void platform_ipm_callback(const struct device *dev, void *context,
				  uint32_t id, volatile void *data)
{
	printf("Message received from mbox %d: 0x%0x\n", id, *(int *)data);
	k_msgq_put(&ip_msgq, (const void *)data, K_NO_WAIT);
}

void start_core1(void)
{
	uint32_t cmd[] = {
		0, 0, 1,
		(uintptr_t)vector_table,
		(uintptr_t)&core1_stack[CORE1_STACK_SIZE - 1],
		(uintptr_t)core1_entry};

	int i = 0;
	while (i < sizeof(cmd) / sizeof(cmd[0])) {
		int recv;

		printf("Sending to Core 1: 0x%0x (i = %d)\n", cmd[i], i);
		ipm_send(ipm_handle, 0, 0, &cmd[i], sizeof (cmd[i]));
		k_msgq_get(&ip_msgq, &recv, K_FOREVER);
		printf("Data received: 0x%0x\n", recv);
		i = cmd[i] == recv ? i + 1 : 0;
	}
}

int main(void)
{
	int ret;
	bool led_state = true;

	if (!gpio_is_ready_dt(&led)) {
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return 0;
	}

	/* setup IPM */
	if (!device_is_ready(ipm_handle)) {
		printf("IPM device is not ready\n");
		return 0;
	}
	ipm_register_callback(ipm_handle, platform_ipm_callback, NULL);
	ret = ipm_set_enabled(ipm_handle, 1);
	if (ret) {
		printf("ipm_set_enabled failed\n");
		return 0;
	}
	k_msgq_purge(&ip_msgq);

	start_core1();

	while (1) {
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
			return 0;
		}

		led_state = !led_state;
		printf("LED state: %s\n", led_state ? "ON" : "OFF");
		k_msleep(SLEEP_TIME_MS);
	}
	return 0;
}
