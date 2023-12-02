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

#ifdef CONFIG_BOARD_NRF5340DK_NRF5340_CPUNET
#include "hci_rpmsg_module.h"
#endif

#include "app_esb.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

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

int main(void)
{
	int err;

	LOG_INF("ESB BLE Multiprotocol Example");

#ifdef CONFIG_BOARD_NRF5340DK_NRF5340_CPUNET
	// Initialize the hci_rpmsg module, which handles the interface between the Bluetooth host and the controller
	hci_rpmsg_init();

	LOG_WRN("Change ESB_EVT_IRQ and ESB_EVT_IRQHandler in esb_peripherals.h to use SWI3 instead of SWI0!");
	k_msleep(5000);
#endif

	// Initialize the app_esb module, which handles timeslot and ESB configuration
	err = app_esb_init(APP_ESB_MODE_PTX, on_esb_callback);
	if (err) {
		LOG_ERR("app_esb init failed (err %d)", err);
		return err;
	}
	LOG_INF("ESB in timeslot started");

	uint8_t esb_tx_buf[8] = {0};
	int tx_counter = 0;

	while (1) {
#ifndef CONFIG_BOARD_NRF5340DK_NRF5340_CPUNET
		memcpy(esb_tx_buf, (uint8_t*)&tx_counter, 4);
		err = app_esb_send(esb_tx_buf, 8);
		if (err < 0) {
			LOG_INF("ESB TX upload failed (err %i)", err);
		}
		else {
			LOG_INF("ESB TX upload %.2x-%.2x", (tx_counter& 0xFF), ((tx_counter >> 8) & 0xFF));
			tx_counter++;
		}
#endif
		k_sleep(K_MSEC(100));
	}
}
