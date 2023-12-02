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

#ifdef CONFIG_BOARD_NRF5340DK_NRF5340_CPUNET
#include "hci_rpmsg_module.h"
#else
#include "app_bt_lbs.h"
#endif

#include "app_esb.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

void on_esb_callback(app_esb_event_t *event)
{
	static uint32_t last_counter = 0;
	static uint32_t counter;
	switch(event->evt_type) {
		case APP_ESB_EVT_TX_SUCCESS:
			LOG_INF("ESB TX success");
			break;
		case APP_ESB_EVT_TX_FAIL:
			LOG_INF("ESB TX failed");
			break;
		case APP_ESB_EVT_RX:
			memcpy((uint8_t*)&counter, event->buf, sizeof(counter));
			if(counter != (last_counter + 1)) {
				LOG_WRN("Packet content error! Counter: %i, last counter %i", counter, last_counter);
			}
			LOG_INF("ESB RX: 0x%.2X-0x%.2X-0x%.2X-0x%.2X", event->buf[0], event->buf[1], event->buf[2], event->buf[3]);
			last_counter = counter;
			break;
		default:
			LOG_ERR("Unknown APP ESB event!");
			break;
	}
}

int main(void)
{
	int err;

	LOG_INF("ESB PRX BLE Multiprotocol Example");

#ifndef CONFIG_BOARD_NRF5340DK_NRF5340_CPUNET
	err = app_bt_init();
	if (err) {
		LOG_ERR("app_bt init failed (err %d)", err);
		return err;
	}
#endif

#ifdef CONFIG_BOARD_NRF5340DK_NRF5340_CPUNET
	// Initialize the hci_rpmsg module, which handles the interface between the Bluetooth host and the controller
	hci_rpmsg_init();

	LOG_WRN("Change ESB_EVT_IRQ and ESB_EVT_IRQHandler in esb_peripherals.h to use SWI3 instead of SWI0!");
	k_msleep(5000);
#endif

	err = app_esb_init(APP_ESB_MODE_PRX, on_esb_callback);
	if (err) {
		LOG_ERR("app_esb init failed (err %d)", err);
		return err;
	}

	while (1) {
		k_sleep(K_MSEC(2000));
	}

	return 0;
}
