/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <sh1106_gfx.h>
#include <nordic_logo.h>
#include <stddef.h>
#include <zephyr/console/console.h>
#include <nrf24_ble.h>
#include <nrf24_drv.h>
#include <SdFat.h>

#define MY_STACK_SIZE 2048
#define MY_PRIORITY 2


K_SEM_DEFINE(sem, 0, 1);

const char *adv_name = "ZephyrBLE";
const uint8_t tx_address[5] = {0x01, 0x23, 0x45, 0x67, 0x89};
char tx_msg[32];

static ble_service_data_t service_data;
static ble_adv_config_t config;
uint8_t count;


const struct device *sh1106 = DEVICE_DT_GET(DT_NODELABEL(sh1106));
/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS 500

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);


int main(void)
{
	
	service_data.uuid = NRF_TEMPERATURE_SERVICE_UUID;
	service_data.data = count;
	config.ble_service_data = &service_data;
	config.flags = 0x05;
	config.adv_name = adv_name;

	if (!ble_begin(&config))
	{
		printk("Ble init failed\n");
	}
	else
		printk("BLE init success");

	ble_set_mode(BLE_MODE_ADVERTISE);
   
	k_msleep(3000);
	if (!SdFat_init())
		printk("SdFat init failed\n");
	else
		printk("SdFat init success\n");

	while (1)
	{

		/*
		if (ble_advertise())
		{
			printk("Adv success!\n");
			service_data.data = count;
			count++;
		}
		else
			printk("Adv failed\n");
		
		*/
		
		k_sleep(K_MSEC(50));
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
	k_msleep(2000);
	oled_clearDisplay(sh1106);

	while (1)
	{
		char *s = "";
		s = console_getline();
		// if (k_sem_take(&sem, K_USEC(10)) == 0)
		{
			oled_printLog(sh1106, s);
			oled_display(sh1106);
			listDir(s);
		}
	}
}

int myThread0(void *, void *, void *)
{
	int ret;
	if (!gpio_is_ready_dt(&led))
	{
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0)
	{
		return 0;
	}

	while (1)
	{

		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0)
			return 0;
		k_msleep(SLEEP_TIME_MS);
		// k_sem_give(&sem);
	}
	return 0;
}

K_THREAD_DEFINE(oled_tid, MY_STACK_SIZE,
				myThread1, NULL, NULL, NULL,
				MY_PRIORITY, 0, 0);

K_THREAD_DEFINE(my_tid, MY_STACK_SIZE,
				myThread0, NULL, NULL, NULL,
				MY_PRIORITY, 0, 0);