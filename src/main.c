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
#include "app_esb.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define TIMESLOT_LED DK_LED4

/* Callback function signalling that a timeslot is started or stopped */
void on_timeslot_start_stop(timeslot_callback_type_t type)
{
	switch (type) {
		case APP_TS_STARTED:
			dk_set_led_off(TIMESLOT_LED);
			app_esb_resume();
			break;
		case APP_TS_STOPPED:
			dk_set_led_on(TIMESLOT_LED);
			app_esb_suspend();
			break;
	}
}

void on_esb_callback(app_esb_event_t *event)
{
	switch(event->evt_type) {
		case APP_ESB_EVT_TX_SUCCESS:
			LOG_INF("ESB TX success");
			break;
		case APP_ESB_EVT_TX_FAIL:
			LOG_INF("ESB TX failed");
			break;
		case APP_ESB_EVT_RX:
			LOG_INF("ESB RX");
			break;
		default:
			LOG_ERR("Unknown APP ESB event!");
			break;
	}
}

void main(void)
{
	int err;

	err = dk_leds_init();
	if (err) {
		LOG_ERR("LEDs init failed (err %d)", err);
		return;
	}

	LOG_INF("ESB BLE Multiprotocol Example");

	err = app_bt_init();
	if (err) {
		LOG_ERR("app_bt init failed (err %d)", err);
		return;
	}

	err = app_esb_init(APP_ESB_MODE_PTX, on_esb_callback);
	if (err) {
		LOG_ERR("app_esb init failed (err %d)", err);
		return;
	}

	timeslot_init(on_timeslot_start_stop);

	uint8_t esb_tx_buf[8] = {0};
	while (1) {
		err = app_esb_send(esb_tx_buf, 8);
		if (err < 0) {
			LOG_INF("ESB TX upload failed (err %i)", err);
		}
		else {
			LOG_INF("ESB TX upload %i", esb_tx_buf[0]);
			esb_tx_buf[0]++;
		}

		k_sleep(K_MSEC(2000));
	}
}
