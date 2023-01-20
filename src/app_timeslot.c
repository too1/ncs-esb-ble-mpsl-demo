#include <zephyr/kernel.h>
#include "app_timeslot.h"
#include <zephyr/irq.h>
#include <zephyr/sys/ring_buffer.h>
#include <hal/nrf_timer.h>

#include <mpsl_timeslot.h>
#include <mpsl.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(timeslot, LOG_LEVEL_INF);

#define TIMESLOT_REQUEST_TIMEOUT_US  1000000
#define TIMESLOT_LENGTH_US           5000
#define TIMESLOT_EXT_MARGIN_MARGIN	 10
#define TIMER_EXPIRY_US_EARLY 		 (TIMESLOT_LENGTH_US - MPSL_TIMESLOT_EXTENSION_MARGIN_MIN_US - TIMESLOT_EXT_MARGIN_MARGIN)

#define MPSL_THREAD_PRIO             CONFIG_MPSL_THREAD_COOP_PRIO
#define STACKSIZE                    CONFIG_MAIN_STACK_SIZE
#define THREAD_PRIORITY              K_LOWEST_APPLICATION_THREAD_PRIO

static timeslot_callback_t m_callback;

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
	.params.earliest.timeout_us = TIMESLOT_REQUEST_TIMEOUT_US
};

static mpsl_timeslot_signal_return_param_t signal_callback_return_param;

/* Ring buffer for forwarding timeslot callbacks to the application */
RING_BUF_DECLARE(callback_ring_buf, 10);

/* Message queue for requesting MPSL API calls to non-preemptible thread */
K_MSGQ_DEFINE(mpsl_api_msgq, sizeof(enum mpsl_timeslot_call), 10, 4);

static void timeslot_session_open(void)
{
	int err;
	enum mpsl_timeslot_call api_call = OPEN_SESSION;
	err = k_msgq_put(&mpsl_api_msgq, &api_call, K_FOREVER);
	if (err) {
		LOG_ERR("Message sent error: %d", err);
		k_oops();
	}
}
static void timeslot_request_new(void)
{
	int err;
	enum mpsl_timeslot_call api_call = MAKE_REQUEST;
	err = k_msgq_put(&mpsl_api_msgq, &api_call, K_FOREVER);
	if (err) {
		LOG_ERR("Message sent error: %d", err);
		k_oops();
	}
}

ISR_DIRECT_DECLARE(swi1_isr)
{
	uint8_t signal_type = 0;

	while (!ring_buf_is_empty(&callback_ring_buf)) {
		if (ring_buf_get(&callback_ring_buf, &signal_type, 1) == 1) {
			switch (signal_type) {
				case MPSL_TIMESLOT_SIGNAL_START:
					LOG_DBG("Callback: Timeslot start");
					break;
				case MPSL_TIMESLOT_SIGNAL_TIMER0:
					LOG_DBG("Callback: Timer0 signal");
					break;
				case MPSL_TIMESLOT_SIGNAL_EXTEND_FAILED: 
					LOG_DBG("Callback: Wops");
					break;			
				default:
					LOG_DBG("Callback: Other signal: %d", signal_type);
					break;
			}
		}
	}

	return 1;
}

static void callback_ring_buf_put(uint8_t data)
{
	uint32_t input_data_len = ring_buf_put(&callback_ring_buf, &data, 1);
	if (input_data_len != 1) {
		LOG_ERR("Full ring buffer, enqueue data with length %d", input_data_len);
		k_oops();
	}
}

static mpsl_timeslot_signal_return_param_t *mpsl_timeslot_callback(mpsl_timeslot_session_id_t session_id, uint32_t signal_type)
{
	(void) session_id; /* unused parameter */

	mpsl_timeslot_signal_return_param_t *p_ret_val = NULL;

	switch (signal_type) {
		case MPSL_TIMESLOT_SIGNAL_START:
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
			p_ret_val = &signal_callback_return_param;

			/*	Extension requested. CC set to expire within a smaller timeframe in order to request an expansion of the timeslot */
			nrf_timer_bit_width_set(NRF_TIMER0, NRF_TIMER_BIT_WIDTH_32);
			nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL0, TIMER_EXPIRY_US_EARLY);
			nrf_timer_int_enable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);

			callback_ring_buf_put((uint8_t)MPSL_TIMESLOT_SIGNAL_START);

			m_callback(true);
			break;

		case MPSL_TIMESLOT_SIGNAL_TIMER0:
			/* Clear event */
			nrf_timer_int_disable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);
			nrf_timer_event_clear(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE0);

			signal_callback_return_param.params.extend.length_us = TIMESLOT_LENGTH_US; 
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_EXTEND;

			p_ret_val = &signal_callback_return_param;
			break;

		case MPSL_TIMESLOT_SIGNAL_EXTEND_SUCCEEDED:
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;

			/* Set next trigger time to be the current + Timer expiry early */
			uint32_t current_cc = nrf_timer_cc_get(NRF_TIMER0, NRF_TIMER_CC_CHANNEL0);
			uint32_t next_trigger_time = current_cc + TIMESLOT_LENGTH_US;
			nrf_timer_bit_width_set(NRF_TIMER0, NRF_TIMER_BIT_WIDTH_32);
			nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL0, next_trigger_time);
			nrf_timer_int_enable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);

			p_ret_val = &signal_callback_return_param;
			break;

		case MPSL_TIMESLOT_SIGNAL_EXTEND_FAILED:
			LOG_DBG("Extension failed!");
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_REQUEST;
			signal_callback_return_param.params.request.p_next = &timeslot_request_earliest;

			p_ret_val = &signal_callback_return_param;
			m_callback(false);
			break;

		case MPSL_TIMESLOT_SIGNAL_RADIO:

			break;

		case MPSL_TIMESLOT_SIGNAL_OVERSTAYED:
			LOG_WRN("something overstayed!");
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_END;
			p_ret_val = &signal_callback_return_param;
			m_callback(false);
			break;

		case MPSL_TIMESLOT_SIGNAL_CANCELLED:
			LOG_DBG("something cancelled!");
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
			p_ret_val = &signal_callback_return_param;
			m_callback(false);
			
			// In this case returning SIGNAL_ACTION_REQUEST causes hardfault. We have to request a new timeslot instead, from thread context. 
			timeslot_request_new();
			break;

		case MPSL_TIMESLOT_SIGNAL_BLOCKED:
			LOG_INF("something blocked!");
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
			p_ret_val = &signal_callback_return_param;
			m_callback(false);

			// Request a new timeslot in this case
			timeslot_request_new();
			break;

		case MPSL_TIMESLOT_SIGNAL_INVALID_RETURN:
			LOG_WRN("something gave invalid return");
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_END;
			p_ret_val = &signal_callback_return_param;
			m_callback(false);
			break;

		case MPSL_TIMESLOT_SIGNAL_SESSION_IDLE:
			LOG_INF("idle");
			callback_ring_buf_put((uint8_t)MPSL_TIMESLOT_SIGNAL_SESSION_IDLE);

			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
			p_ret_val = &signal_callback_return_param;
			m_callback(false);
			break;

		case MPSL_TIMESLOT_SIGNAL_SESSION_CLOSED:
			LOG_INF("Session closed");
			callback_ring_buf_put((uint8_t)MPSL_TIMESLOT_SIGNAL_SESSION_CLOSED);

			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
			p_ret_val = &signal_callback_return_param;
			m_callback(false);
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

void timeslot_init(timeslot_callback_t callback)
{
	m_callback = callback;

	timeslot_session_open();

	timeslot_request_new();

#if defined(CONFIG_SOC_SERIES_NRF53X)
	IRQ_DIRECT_CONNECT(SWI1_IRQn, 1, swi1_isr, 0);
	irq_enable(SWI1_IRQn);
#elif defined(CONFIG_SOC_SERIES_NRF52X)
	IRQ_DIRECT_CONNECT(SWI1_EGU1_IRQn, 1, swi1_isr, 0);
	irq_enable(SWI1_EGU1_IRQn);
#endif
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
					err = mpsl_timeslot_session_open(mpsl_timeslot_callback, &session_id);
					if (err) {
						LOG_ERR("Timeslot session open error: %d", err);
						k_oops();
					}
					break;
				case MAKE_REQUEST:
					err = mpsl_timeslot_request(session_id, &timeslot_request_earliest);
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

K_THREAD_DEFINE(mpsl_nonpreemptible_thread_id, STACKSIZE,
		mpsl_nonpreemptible_thread, NULL, NULL, NULL,
		K_PRIO_COOP(MPSL_THREAD_PRIO), 0, 0);