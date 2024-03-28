/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/display/sh1106_gfx.h>
#include <zephyr/display/nordic_logo.h>
#include <stddef.h>
#include <zephyr/console/console.h>
#include "SdFat.h"
#include "my_timer.h"
#include "led_indication.h"

#define MY_STACK_SIZE 2048
#define MY_PRIORITY 2

K_SEM_DEFINE(sem, 0, 1);

const struct device *sh1106 = DEVICE_DT_GET(DT_NODELABEL(sh1106));
/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS 500

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

#define TIMER_PERIOD MS_TO_TICKS(1000)
/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

MY_TIMER_DEF(led_timer);
void led_timer_cb()
{
	gpio_pin_toggle_dt(&led1);
}

int main(void)
{
	myTimer_init();
	led_indication_init();
	led_indication_set(LED_INDICATION_FAST_BLINK);

	while (1)
	{
		k_sleep(K_MSEC(500));
	}

	return 0;
}

void myThread1(void *, void *, void *)
{

	if (!device_is_ready(sh1106))
	{
		printk("Failed to initialize sh1106 display\n");
		while (1)
			;
	}

	console_getline_init();

	oled_displayOn(sh1106);
	oled_clearDisplay(sh1106);
	oled_printString(sh1106, "Powered by Zephyr OS.", 0, 0, 6, false);

	oled_display(sh1106);
	k_msleep(2000);
	oled_clearDisplay(sh1106);
	oled_displayBmp(sh1106, logo);
	oled_display(sh1106);
	oled_clearDisplay(sh1106);
	if (!SdFat_init())
		printk("SdFat init failed\n");
	else
		printk("SdFat init success\n");

	while (1)
	{
		char *s = "";
		s = console_getline();

		oled_printLog(sh1106, s);
		oled_display(sh1106);
		listDir(s);
	}
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

		ret = gpio_pin_toggle_dt(&led0);
		if (ret < 0)
			return 0;
		k_msleep(SLEEP_TIME_MS);
		// k_sem_give(&sem);
	}
	return 0;
}
K_THREAD_DEFINE(my_tid, MY_STACK_SIZE,
				myThread0, NULL, NULL, NULL,
				MY_PRIORITY, 0, 0);

K_THREAD_DEFINE(oled_tid, MY_STACK_SIZE,
				myThread1, NULL, NULL, NULL,
				MY_PRIORITY, 0, 0);
