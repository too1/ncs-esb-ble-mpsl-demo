/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
MODIFIED SAMPLE TO INCLUDE EXTENSIONS ++
*/

#include <zephyr/kernel.h>
#include <zephyr/console/console.h>
#include <string.h>
#include <zephyr/sys/printk.h>
#include <zephyr/types.h>
#include <zephyr/logging/log.h>

#include <zephyr/drivers/gpio.h> 

#include <dk_buttons_and_leds.h>
#include "app_bt_lbs.h"
#include "app_timeslot.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define TIMESLOT_LED DK_LED4

//#define LED0_NODE DT_ALIAS(led0)
//static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* Callback function signalling that a timeslot is started or stopped */
void on_timeslot_start_stop(bool started)
{
	started ? dk_set_led_off(TIMESLOT_LED) : dk_set_led_on(TIMESLOT_LED);
}

void main(void)
{
	int err;

	err = dk_leds_init();
	if (err) {
		printk("LEDs init failed (err %d)\n", err);
		return;
	}

	printk("ESB BLE Multiprotocol Example\n");

	app_bt_init();

	timeslot_init(on_timeslot_start_stop);

	while (1) {
		k_sleep(K_MSEC(1000));
	}
}
