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

#include "hci_rpmsg_module.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	int err;

	LOG_INF("ESB BLE Multiprotocol Example - Netcore project");

	// Initialize the hci_rpmsg module, which handles the interface between the Bluetooth host and the controller
	hci_rpmsg_init();

	LOG_WRN("Change ESB_EVT_IRQ and ESB_EVT_IRQHandler in esb_peripherals.h to use SWI3 instead of SWI0!");

	while (1) {
		k_sleep(K_MSEC(2000));
		//LOG_INF("Alive");
	}
}
