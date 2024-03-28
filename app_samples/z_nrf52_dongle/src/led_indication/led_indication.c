#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include "my_timer.h"
#include "led_indication.h"

#define NORMAL_BLINK_INTERVAL MS_TO_TICKS(1000)
#define FAST_BLINK_BASE_INTERVAL MS_TO_TICKS(1000)
#define FAST_BLINK_CORE_INTERVAL MS_TO_TICKS(80)

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led3)

static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

MY_TIMER_DEF(normal_blink_timer);
MY_TIMER_DEF(fast_blink_timer_base);
MY_TIMER_DEF(fast_blink_timer_core);

void normal_blink_timer_handler()
{
	gpio_pin_toggle_dt(&led1);
}
void fast_blink_timer_base_handler()
{
	myTimer_start(&fast_blink_timer_core, FAST_BLINK_CORE_INTERVAL);
}

void fast_blink_timer_core_handler()
{
	gpio_pin_toggle_dt(&led1);
	static uint8_t blink_count = 0;
	blink_count++;
	if (blink_count == 4)
	{
		myTimer_stop(&fast_blink_timer_core);
		blink_count = 0;
	}
}

void led_indication_set(led_indication_t indication_type)
{
	switch (indication_type)
	{

	case LED_INDICATION_NORMAL_BLINK:
		myTimer_create(&normal_blink_timer, normal_blink_timer_handler, MY_TIMER_MODE_REPEATED);
		myTimer_start(&normal_blink_timer, NORMAL_BLINK_INTERVAL);
		break;

	case LED_INDICATION_FAST_BLINK:
		myTimer_create(&fast_blink_timer_base, fast_blink_timer_base_handler, MY_TIMER_MODE_REPEATED);
		myTimer_create(&fast_blink_timer_core, fast_blink_timer_core_handler, MY_TIMER_MODE_REPEATED);
		myTimer_start(&fast_blink_timer_base, FAST_BLINK_BASE_INTERVAL);
		break;

	default:
		break;
	}
}

void led_indication_clear(led_indication_t indication_type)
{
	switch (indication_type)
	{
	case LED_INDICATION_NORMAL_BLINK:
		myTimer_stop(&normal_blink_timer);
		break;

	case LED_INDICATION_FAST_BLINK:
		myTimer_stop(&fast_blink_timer_base);
		myTimer_stop(&fast_blink_timer_core);
		break;

	default:
		break;
	}
}

int led_indication_init()
{
	if (!gpio_is_ready_dt(&led1))
		return -1;

	return gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
}
