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
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/types.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>

#include <mpsl_timeslot.h>
#include <mpsl.h>
#include <hal/nrf_timer.h>
#include <zephyr/drivers/gpio.h> 

#include <dk_buttons_and_leds.h>
#include "app_bt_lbs.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define TIMESLOT_LED                DK_LED4

#define TIMESLOT_REQUEST_DISTANCE_US (1000000)
#define TIMESLOT_LENGTH_US           (5000)
#define EXTRA 						 10
#define TIMER_EXPIRY_US_EARLY (TIMESLOT_LENGTH_US- MPSL_TIMESLOT_EXTENSION_MARGIN_MIN_US - EXTRA)
#define TIMER_EXPIRY_US (TIMESLOT_LENGTH_US - 50)
#define TIMER_EXPIRY_US_MULTI 		 (50)

#define MPSL_THREAD_PRIO             CONFIG_MPSL_THREAD_COOP_PRIO
#define STACKSIZE                    CONFIG_MAIN_STACK_SIZE
#define THREAD_PRIORITY              K_LOWEST_APPLICATION_THREAD_PRIO

static bool request_in_cb = true;

uint16_t number_of_times_extended = 0;

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static bool request_extension = false; 

static uint32_t extension_time_us = 5000;
static uint8_t timeslot_number = 0;

/* MPSL API calls that can be requested for the non-preemptible thread */
enum mpsl_timeslot_call {
	OPEN_SESSION,
	MAKE_REQUEST,
	CLOSE_SESSION,
};

/* Timeslot requests */
static mpsl_timeslot_request_t timeslot_request_earliest = {
	.request_type = MPSL_TIMESLOT_REQ_TYPE_EARLIEST,
	.params.earliest.hfclk = MPSL_TIMESLOT_HFCLK_CFG_NO_GUARANTEE,
	.params.earliest.priority = MPSL_TIMESLOT_PRIORITY_NORMAL,
	.params.earliest.length_us = TIMESLOT_LENGTH_US,
	.params.earliest.timeout_us = 1000000
};

static mpsl_timeslot_signal_return_param_t signal_callback_return_param;

/* Two ring buffers for printing the signal type with different priority from timeslot callback */
RING_BUF_DECLARE(callback_high_priority_ring_buf, 10);
RING_BUF_DECLARE(callback_low_priority_ring_buf, 10);


/* Message queue for requesting MPSL API calls to non-preemptible thread */
K_MSGQ_DEFINE(mpsl_api_msgq, sizeof(enum mpsl_timeslot_call), 10, 4);

/* Not really implementented, but could display when in the timeslot using this */
int display_timeslot(bool mode){
	mode ? dk_set_led_off(TIMESLOT_LED) : dk_set_led_on(TIMESLOT_LED);
	return 0;
}

ISR_DIRECT_DECLARE(swi1_isr)
{
	uint8_t signal_type = 0;

	while (!ring_buf_is_empty(&callback_high_priority_ring_buf)) {
		if (ring_buf_get(&callback_high_priority_ring_buf, &signal_type, 1) == 1) {
			switch (signal_type) {
			case MPSL_TIMESLOT_SIGNAL_START:
				LOG_INF("Callback: Timeslot start\n");
				break;
			case MPSL_TIMESLOT_SIGNAL_TIMER0:
				LOG_INF("Callback: Timer0 signal\n");
				break;
			case MPSL_TIMESLOT_SIGNAL_EXTEND_FAILED: 
				LOG_INF("Callback: Wops\n");
				break;			
			default:
				LOG_INF("Callback: Other signal: %d\n", signal_type);
				break;
			}
		}
	}

	while (!ring_buf_is_empty(&callback_low_priority_ring_buf)) {
		if (ring_buf_get(&callback_low_priority_ring_buf, &signal_type, 1) == 1) {
			switch (signal_type) {
			case MPSL_TIMESLOT_SIGNAL_SESSION_IDLE:
				LOG_INF("Callback: Session idle\n");
				break;
			case MPSL_TIMESLOT_SIGNAL_SESSION_CLOSED:
				LOG_INF("Callback: Session closed\n");
				break;
			case MPSL_TIMESLOT_SIGNAL_EXTEND_FAILED: 
				LOG_INF("Wops, extension fail in ISRbuffer 2\n");
				break;
			default:
				LOG_INF("Callback: Other signal: %d\n", signal_type);
				break;
			}
		}
	}

	return 1;
}

static mpsl_timeslot_signal_return_param_t *mpsl_timeslot_callback(
	mpsl_timeslot_session_id_t session_id,
	uint32_t signal_type)
{
	(void) session_id; /* unused parameter */
	uint8_t input_data = (uint8_t)signal_type;
	uint32_t input_data_len;

	mpsl_timeslot_signal_return_param_t *p_ret_val = NULL;

	switch (signal_type) {

	case MPSL_TIMESLOT_SIGNAL_START:
		/* No return action */
		timeslot_number+=1;
		signal_callback_return_param.callback_action =
			MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
		p_ret_val = &signal_callback_return_param;

		/* Setup timer to trigger an interrupt (and thus the TIMER0
		 * signal) before timeslot end.
		 */
		if (request_in_cb && request_extension ) {
			/*	Extension requested. CC set to expire within a smaller timeframe in order to request an expansion of the timeslot */
			nrf_timer_bit_width_set(NRF_TIMER0, NRF_TIMER_BIT_WIDTH_32);
			nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL0, TIMER_EXPIRY_US_EARLY);
			nrf_timer_int_enable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);
		}
		input_data_len = ring_buf_put(&callback_high_priority_ring_buf, &input_data, 1);
		if (input_data_len != 1) {
			LOG_ERR("Full ring buffer, enqueue data with length %d", input_data_len);
			k_oops();
		}
		/*Add led blink call to another function*/
		display_timeslot(true);

		break;

	case MPSL_TIMESLOT_SIGNAL_TIMER0:
		/* Clear event */
		nrf_timer_int_disable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);
		nrf_timer_event_clear(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE0);

		signal_callback_return_param.params.extend.length_us = extension_time_us; 
		signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_EXTEND;

		p_ret_val = &signal_callback_return_param;
		input_data_len = ring_buf_put(&callback_high_priority_ring_buf, &input_data, 1);
		if (input_data_len != 1) {
			LOG_ERR("Full ring buffer, enqueue data with length %d", input_data_len);
			k_oops();
		}
		break;

	case MPSL_TIMESLOT_SIGNAL_EXTEND_SUCCEEDED:
		signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;

		/* Set next trigger time to be the current + Timer expiry early */
		uint32_t current_cc = nrf_timer_cc_get(NRF_TIMER0, NRF_TIMER_CC_CHANNEL0);
		uint32_t next_trigger_time = current_cc + extension_time_us;
		nrf_timer_bit_width_set(NRF_TIMER0, NRF_TIMER_BIT_WIDTH_32);
		nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL0, next_trigger_time);
		nrf_timer_int_enable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);

		LOG_INF("Extend Successfull number: %d, CC: %d, next: %d\n", 
													number_of_times_extended++, 
													current_cc, 
													next_trigger_time);

		//display_timeslot(true);
		p_ret_val = &signal_callback_return_param;
		break;

	case MPSL_TIMESLOT_SIGNAL_EXTEND_FAILED:
		LOG_INF("Extension failed!");
		signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_REQUEST;
		signal_callback_return_param.params.request.p_next = &timeslot_request_earliest;

		p_ret_val = &signal_callback_return_param;
		display_timeslot(false);
		break;

	case MPSL_TIMESLOT_SIGNAL_RADIO:
		LOG_INF("something failed!");
		signal_callback_return_param.callback_action =
			MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
		p_ret_val = &signal_callback_return_param;
		display_timeslot(false);
		break;

	case MPSL_TIMESLOT_SIGNAL_OVERSTAYED:
		LOG_INF("something overstayed!");
		signal_callback_return_param.callback_action =
			MPSL_TIMESLOT_SIGNAL_ACTION_END;
		p_ret_val = &signal_callback_return_param;
		display_timeslot(false);
		break;

	case MPSL_TIMESLOT_SIGNAL_CANCELLED:
		LOG_INF("something cancelled!");
		signal_callback_return_param.callback_action =
			MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
		p_ret_val = &signal_callback_return_param;
		display_timeslot(false);
		break;

	case MPSL_TIMESLOT_SIGNAL_BLOCKED:
		LOG_INF("something blocked!");
		signal_callback_return_param.callback_action =
			MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
		p_ret_val = &signal_callback_return_param;
		display_timeslot(false);
		break;

	case MPSL_TIMESLOT_SIGNAL_INVALID_RETURN:
		LOG_INF("something gave invalid return\n");
		signal_callback_return_param.callback_action =
			MPSL_TIMESLOT_SIGNAL_ACTION_END;
		p_ret_val = &signal_callback_return_param;
		display_timeslot(false);
		break;

	case MPSL_TIMESLOT_SIGNAL_SESSION_IDLE:
		LOG_INF("idle");

		input_data_len = ring_buf_put(&callback_low_priority_ring_buf, &input_data, 1);
		if (input_data_len != 1) {
			LOG_ERR("Full ring buffer, enqueue data with length %d", input_data_len);
			k_oops();
		}
		signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
		p_ret_val = &signal_callback_return_param;
		display_timeslot(false);
		break;

	case MPSL_TIMESLOT_SIGNAL_SESSION_CLOSED:
		LOG_INF("Session closed");
		input_data_len = ring_buf_put(&callback_low_priority_ring_buf, &input_data, 1);
		if (input_data_len != 1) {
			LOG_ERR("Full ring buffer, enqueue data with length %d", input_data_len);
			k_oops();
			p_ret_val = &signal_callback_return_param;
		}
		signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
		display_timeslot(false);
		break;

	default:
		LOG_ERR("unexpected signal: %u", signal_type);
		k_oops();
		break;
	}
	
#if defined(CONFIG_SOC_SERIES_NRF53X)
	NVIC_SetPendingIRQ(SWI1_IRQn);
#elif defined(CONFIG_SOC_SERIES_NRF52X)
	NVIC_SetPendingIRQ(SWI1_EGU1_IRQn);
#endif
	return p_ret_val;
}

static void timeslot_init(void)
{
	int err;
	enum mpsl_timeslot_call api_call;

	request_in_cb = true;
	request_extension = true;
	
	api_call = OPEN_SESSION;
	err = k_msgq_put(&mpsl_api_msgq, &api_call, K_FOREVER);
	if (err) {
		LOG_ERR("Message sent error: %d", err);
		k_oops();
	}

	api_call = MAKE_REQUEST;
	err = k_msgq_put(&mpsl_api_msgq, &api_call, K_FOREVER);
	if (err) {
		LOG_ERR("Message sent error: %d", err);
		k_oops();
	}

}

/* To ensure thread safe operation, call all MPSL APIs from a non-preemptible
 * thread.
 */
static void mpsl_nonpreemptible_thread(void)
{
	int err;
	enum mpsl_timeslot_call api_call = 0;

	/* Initialize to invalid session id */
	mpsl_timeslot_session_id_t session_id = 0xFFu;

	while (1) {
		if (k_msgq_get(&mpsl_api_msgq, &api_call, K_FOREVER) == 0) {
			switch (api_call) {
			case OPEN_SESSION:
				err = mpsl_timeslot_session_open(
					mpsl_timeslot_callback,
					&session_id);
				if (err) {
					LOG_ERR("Timeslot session open error: %d", err);
					k_oops();
				}
				break;
			case MAKE_REQUEST:
				err = mpsl_timeslot_request(
					session_id,
					&timeslot_request_earliest);
				if (err) {
					LOG_ERR("Timeslot request error: %d", err);
					k_oops();
				}
				break;
			case CLOSE_SESSION:
				err = mpsl_timeslot_session_close(session_id);
				if (err) {
					LOG_ERR("Timeslot session close error: %d", err);
					k_oops();
				}
				break;
			default:
				LOG_ERR("Wrong timeslot API call");
				k_oops();
				break;
			}
		}
	}
}

void main(void)
{

	int err = console_init();
	if (err) {
		LOG_ERR("Initialize console device error");
		k_oops();
	}

	err = dk_leds_init();
	if (err) {
		printk("LEDs init failed (err %d)\n", err);
		return;
	}

	printk("ESB BLE Multiprotocol Example\n");

#if defined(CONFIG_SOC_SERIES_NRF53X)
	IRQ_DIRECT_CONNECT(SWI1_IRQn, 1, swi1_isr, 0);
	irq_enable(SWI1_IRQn);
#elif defined(CONFIG_SOC_SERIES_NRF52X)
	IRQ_DIRECT_CONNECT(SWI1_EGU1_IRQn, 1, swi1_isr, 0);
	irq_enable(SWI1_EGU1_IRQn);
#endif

	app_bt_init();

	timeslot_init();

	while (1) {
		k_sleep(K_MSEC(1000));
	}
}

K_THREAD_DEFINE(mpsl_nonpreemptible_thread_id, STACKSIZE,
		mpsl_nonpreemptible_thread, NULL, NULL, NULL,
		K_PRIO_COOP(MPSL_THREAD_PRIO), 0, 0);
