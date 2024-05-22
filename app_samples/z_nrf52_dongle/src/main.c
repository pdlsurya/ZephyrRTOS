/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/console/console.h>

#define MY_STACK_SIZE 2048
#define MY_PRIORITY 1

K_CONDVAR_DEFINE(condVar);
K_MUTEX_DEFINE(mutex);
/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS 100

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

uint8_t logCnt = 0;

int main(void)
{

	while (1)
	{
		k_mutex_lock(&mutex, K_FOREVER);

		k_condvar_signal(&condVar);

		k_mutex_unlock(&mutex);
		k_sleep(K_MSEC(500));
	}
	return 0;
}

int myThread1(void *, void *, void *)
{

	int ret;
	if (!gpio_is_ready_dt(&led1))
		return 0;
	ret = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
	if (ret < 0)
		return 0;

	while (1)
	{

		k_mutex_lock(&mutex, K_FOREVER);
		k_condvar_wait(&condVar, &mutex, K_FOREVER);
		ret = gpio_pin_toggle_dt(&led1);
		if (ret < 0)
			return 0;
		k_mutex_unlock(&mutex);
	}
	return 0;
}

int myThread0(void *, void *, void *)
{
	int ret;
	if (!gpio_is_ready_dt(&led0))
		return 0;
	ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
	if (ret < 0)
		return 0;
	while (1)
	{
		k_mutex_lock(&mutex, K_FOREVER);
		k_condvar_wait(&condVar, &mutex, K_FOREVER);
		ret = gpio_pin_toggle_dt(&led0);
		if (ret < 0)
			return 0;
		k_mutex_unlock(&mutex);
	}
	return 0;
}

K_THREAD_DEFINE(my_tid, MY_STACK_SIZE,
				myThread0, NULL, NULL, NULL,
				MY_PRIORITY, 0, 0);

K_THREAD_DEFINE(oled_tid, MY_STACK_SIZE,
				myThread1, NULL, NULL, NULL,
				MY_PRIORITY, 0, 0);