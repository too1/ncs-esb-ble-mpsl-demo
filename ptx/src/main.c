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
#include "hci_rpmsg_module.h"
#include "app_timeslot.h"
#include "app_esb.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define TIMESLOT_LED DK_LED4

/* Callback function signalling that a timeslot is started or stopped */
void on_timeslot_start_stop(timeslot_callback_type_t type)
{
	switch (type) {
		case APP_TS_STARTED:
			NRF_P0->OUTCLR = BIT(31);
			//dk_set_led_off(TIMESLOT_LED);
			//LOG_INF("start");
			app_esb_resume();
			break;
		case APP_TS_STOPPED:
			NRF_P0->OUTSET = BIT(31);
			//dk_set_led_on(TIMESLOT_LED);
			//LOG_INF("stop");
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
			LOG_INF("ESB RX: 0x%.2x-0x%.2x-0x%.2x-0x%.2x", event->buf[0], event->buf[1], event->buf[2], event->buf[3]);
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

	err = app_esb_init(APP_ESB_MODE_PTX, on_esb_callback);
	if (err) {
		LOG_ERR("app_esb init failed (err %d)", err);
		return;
	}
	
	timeslot_init(on_timeslot_start_stop);

	// Initialize the rpmsg application. Will run from a thread in the background
	hci_rpmsg_run();

	LOG_INF("winthappen");

	uint8_t esb_tx_buf[8] = {0};
	int tx_counter = 0;

	while (1) {
		memcpy(esb_tx_buf, (uint8_t*)&tx_counter, 4);
		err = app_esb_send(esb_tx_buf, 8);
		if (err < 0) {
			LOG_INF("ESB TX upload failed (err %i)", err);
		}
		else {
			LOG_INF("ESB TX upload %.2x-%.2x", (tx_counter& 0xFF), ((tx_counter >> 8) & 0xFF));
			tx_counter++;
		}
		k_sleep(K_MSEC(100));
	}
}
