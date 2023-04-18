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
			NRF_P1->OUTSET = BIT(5);
			app_esb_resume();
			break;
		case APP_TS_STOPPED:
			dk_set_led_on(TIMESLOT_LED);
			NRF_P1->OUTCLR = BIT(5);
			app_esb_suspend();
			break;
	}
}

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
				LOG_ERR("Packet content error! Counter: %i, last counter %i", counter, last_counter);
			}
			LOG_INF("ESB RX: 0x%.2X-0x%.2X-0x%.2X-0x%.2X", event->buf[0], event->buf[1], event->buf[2], event->buf[3]);
			last_counter = counter;
			break;
		default:
			LOG_ERR("Unknown APP ESB event!");
			break;
	}
}

#define PPI_CH_TX_EN 0
#define PPI_CH_RX_EN 1
#define PPI_CH_TRX_DISABLE 2
#define PPI_CH_TGL		3
#define GPIOTE_CH_TX	0
#define GPIOTE_CH_RX	1
#define GPIOTE_CH_TGL   2
#define PIN_TX (32 + 3)
#define PIN_RX (32 + 4)
#define PIN_TGL (32 + 8)
#define PIN_RESET 7

static void debug_gpio_init(void)
{
	// Set P1.01-P1.05 as outputs
	NRF_P1->DIRSET = 0xf << 5;
	NRF_GPIOTE->CONFIG[GPIOTE_CH_TX] = GPIOTE_CONFIG_MODE_Task << GPIOTE_CONFIG_MODE_Pos |
							GPIOTE_CONFIG_OUTINIT_Low << GPIOTE_CONFIG_OUTINIT_Pos |
							PIN_TX << GPIOTE_CONFIG_PSEL_Pos;
	NRF_GPIOTE->CONFIG[GPIOTE_CH_RX] = GPIOTE_CONFIG_MODE_Task << GPIOTE_CONFIG_MODE_Pos |
							GPIOTE_CONFIG_OUTINIT_Low << GPIOTE_CONFIG_OUTINIT_Pos |
							PIN_RX << GPIOTE_CONFIG_PSEL_Pos;
	NRF_GPIOTE->CONFIG[GPIOTE_CH_TGL] = GPIOTE_CONFIG_MODE_Task << GPIOTE_CONFIG_MODE_Pos |
							GPIOTE_CONFIG_OUTINIT_Low << GPIOTE_CONFIG_OUTINIT_Pos |
							PIN_TGL << GPIOTE_CONFIG_PSEL_Pos;
	NRF_PPI->CH[PPI_CH_TX_EN].EEP = (uint32_t)&NRF_RADIO->EVENTS_TXREADY;
	NRF_PPI->CH[PPI_CH_TX_EN].TEP = (uint32_t)&NRF_GPIOTE->TASKS_SET[GPIOTE_CH_TX];
	NRF_PPI->CH[PPI_CH_RX_EN].EEP = (uint32_t)&NRF_RADIO->EVENTS_RXREADY;
	NRF_PPI->CH[PPI_CH_RX_EN].TEP = (uint32_t)&NRF_GPIOTE->TASKS_SET[GPIOTE_CH_RX];
	NRF_PPI->CH[PPI_CH_TRX_DISABLE].EEP = (uint32_t)&NRF_RADIO->EVENTS_DISABLED;
	NRF_PPI->CH[PPI_CH_TRX_DISABLE].TEP = (uint32_t)&NRF_GPIOTE->TASKS_CLR[GPIOTE_CH_TX];
	NRF_PPI->FORK[PPI_CH_TRX_DISABLE].TEP = (uint32_t)&NRF_GPIOTE->TASKS_CLR[GPIOTE_CH_RX];
	NRF_PPI->CH[PPI_CH_TGL].EEP = (uint32_t)&NRF_RADIO->EVENTS_ADDRESS;
	NRF_PPI->CH[PPI_CH_TGL].TEP = (uint32_t)&NRF_GPIOTE->TASKS_OUT[GPIOTE_CH_TGL];
	NRF_PPI->CHENSET = BIT(PPI_CH_TX_EN) | BIT(PPI_CH_RX_EN) |
					   BIT(PPI_CH_TRX_DISABLE) | BIT(PPI_CH_TGL); 
	

	NRF_P1->PIN_CNF[PIN_RESET] = GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos |
				GPIO_PIN_CNF_DRIVE_D0H1 << GPIO_PIN_CNF_DRIVE_Pos |
				GPIO_PIN_CNF_PULL_Pulldown << GPIO_PIN_CNF_PULL_Pos;
				
	// Pulse after reset
	NRF_P1->OUTSET = BIT(PIN_RESET);
	k_msleep(1);
	NRF_P1->OUTCLR = BIT(PIN_RESET);
}

void main(void)
{
	int err;

	err = dk_leds_init();
	if (err) {
		LOG_ERR("LEDs init failed (err %d)", err);
		return;
	}

	LOG_INF("ESB PRX BLE Multiprotocol Example");

	err = app_bt_init();
	if (err) {
		LOG_ERR("app_bt init failed (err %d)", err);
		return;
	}

	err = app_esb_init(APP_ESB_MODE_PRX, on_esb_callback);
	if (err) {
		LOG_ERR("app_esb init failed (err %d)", err);
		return;
	}

	debug_gpio_init();

	timeslot_init(on_timeslot_start_stop);

	while (1) {


		k_sleep(K_MSEC(2000));
	}
}
