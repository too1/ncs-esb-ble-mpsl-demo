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
#define TIMESLOT_LENGTH_US           50000
#define TIMESLOT_EXT_MARGIN_MARGIN	 50
#define TIMESLOT_ESB_DISABLE_MARGIN  500
#define TIMER_EXPIRY_US_EARLY 		 (TIMESLOT_LENGTH_US - MPSL_TIMESLOT_EXTENSION_MARGIN_MIN_US - TIMESLOT_EXT_MARGIN_MARGIN)

#define MPSL_THREAD_PRIO             CONFIG_MPSL_THREAD_COOP_PRIO
#define STACKSIZE                    CONFIG_MAIN_STACK_SIZE
#define THREAD_PRIORITY              K_LOWEST_APPLICATION_THREAD_PRIO

static timeslot_callback_t m_callback;
static volatile bool m_in_timeslot = false;

// Requests and callbacks to be run serialized from an SWI interrupt
enum mpsl_timeslot_call {
	SWI_OPEN_SESSION,
	SWI_MAKE_REQUEST,
	SWI_CLOSE_SESSION,
	SWI_TS_STARTED,
	SWI_TS_STOPPED,
};

// Timeslot request
static mpsl_timeslot_request_t timeslot_request_earliest = {
	.request_type = MPSL_TIMESLOT_REQ_TYPE_EARLIEST,
	.params.earliest.hfclk = MPSL_TIMESLOT_HFCLK_CFG_NO_GUARANTEE,
	.params.earliest.priority = MPSL_TIMESLOT_PRIORITY_NORMAL,
	.params.earliest.length_us = TIMESLOT_LENGTH_US,
	.params.earliest.timeout_us = TIMESLOT_REQUEST_TIMEOUT_US
};

static mpsl_timeslot_signal_return_param_t signal_callback_return_param;

// Ring buffer for forwarding timeslot callbacks to the application
RING_BUF_DECLARE(callback_ring_buf, 10);

static void callback_ring_buf_put(uint8_t data)
{
	uint32_t input_data_len = ring_buf_put(&callback_ring_buf, &data, 1);
	if (input_data_len != 1) {
		LOG_ERR("Full ring buffer, enqueue data with length %d", input_data_len);
		k_oops();
	}
	NVIC_SetPendingIRQ(SWI1_EGU1_IRQn);
}

static void set_timeslot_active_status(bool active)
{
	if (active) {
		if (!m_in_timeslot) {
			m_in_timeslot = true;
			callback_ring_buf_put(SWI_TS_STARTED);
		}
	} else {
		if (m_in_timeslot) {
			m_in_timeslot = false;
			callback_ring_buf_put(SWI_TS_STOPPED);
		}
	}
}

static mpsl_timeslot_signal_return_param_t *mpsl_timeslot_callback(mpsl_timeslot_session_id_t session_id, uint32_t signal_type)
{
	(void) session_id; // unused parameter

	mpsl_timeslot_signal_return_param_t *p_ret_val = NULL;
	switch (signal_type) {
		case MPSL_TIMESLOT_SIGNAL_START:
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
			p_ret_val = &signal_callback_return_param;

			// Reset the radio to make sure no configuration remains from BLE
			NVIC_ClearPendingIRQ(RADIO_IRQn);
			NRF_RADIO->POWER = RADIO_POWER_POWER_Disabled << RADIO_POWER_POWER_Pos;
			NRF_RADIO->POWER = RADIO_POWER_POWER_Enabled << RADIO_POWER_POWER_Pos;
			NVIC_ClearPendingIRQ(RADIO_IRQn);

			nrf_timer_bit_width_set(NRF_TIMER0, NRF_TIMER_BIT_WIDTH_32);
			
			nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL0, TIMER_EXPIRY_US_EARLY - TIMESLOT_ESB_DISABLE_MARGIN);
			nrf_timer_int_enable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);

			nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL1, TIMER_EXPIRY_US_EARLY);
			nrf_timer_int_enable(NRF_TIMER0, NRF_TIMER_INT_COMPARE1_MASK);

			set_timeslot_active_status(true);
			break;

		case MPSL_TIMESLOT_SIGNAL_TIMER0:
			if(nrf_timer_event_check(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE0)) {
				nrf_timer_int_disable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);
				nrf_timer_event_clear(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE0);
				
				signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;

				set_timeslot_active_status(false);
			}
			else if(nrf_timer_event_check(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE1)) {
				nrf_timer_int_disable(NRF_TIMER0, NRF_TIMER_INT_COMPARE1_MASK);
				nrf_timer_event_clear(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE1);

				signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_REQUEST;
				signal_callback_return_param.params.request.p_next = &timeslot_request_earliest;
			}
			p_ret_val = &signal_callback_return_param;
			break;

		case MPSL_TIMESLOT_SIGNAL_EXTEND_SUCCEEDED:
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;

			// Set next trigger time to be the current + Timer expiry early
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
			set_timeslot_active_status(false);
			break;

		case MPSL_TIMESLOT_SIGNAL_RADIO:
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
			p_ret_val = &signal_callback_return_param;

			// We have to manually call the RADIO IRQ handler when the RADIO signal occurs
			if(m_in_timeslot) RADIO_IRQHandler();
			else {
				NVIC_ClearPendingIRQ(RADIO_IRQn);
				NVIC_DisableIRQ(RADIO_IRQn);
			}
			break;

		case MPSL_TIMESLOT_SIGNAL_OVERSTAYED:
			LOG_WRN("something overstayed!");
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_END;
			p_ret_val = &signal_callback_return_param;
			set_timeslot_active_status(false);
			break;

		case MPSL_TIMESLOT_SIGNAL_CANCELLED:
			LOG_DBG("something cancelled!");
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
			p_ret_val = &signal_callback_return_param;
			set_timeslot_active_status(false);
			
			// In this case returning SIGNAL_ACTION_REQUEST causes hardfault. We have to request a new timeslot instead, from thread context. 
			callback_ring_buf_put(SWI_MAKE_REQUEST);
			break;

		case MPSL_TIMESLOT_SIGNAL_BLOCKED:
			LOG_INF("something blocked!");
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
			p_ret_val = &signal_callback_return_param;
			set_timeslot_active_status(false);

			// Request a new timeslot in this case
			callback_ring_buf_put(SWI_MAKE_REQUEST);
			break;

		case MPSL_TIMESLOT_SIGNAL_INVALID_RETURN:
			LOG_WRN("something gave invalid return");
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_END;
			p_ret_val = &signal_callback_return_param;
			set_timeslot_active_status(false);
			break;

		case MPSL_TIMESLOT_SIGNAL_SESSION_IDLE:
			LOG_INF("idle");

			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
			p_ret_val = &signal_callback_return_param;
			set_timeslot_active_status(false);
			break;

		case MPSL_TIMESLOT_SIGNAL_SESSION_CLOSED:
			LOG_INF("Session closed");

			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
			p_ret_val = &signal_callback_return_param;
			set_timeslot_active_status(false);
			break;

		default:
			LOG_ERR("unexpected signal: %u", signal_type);
			k_oops();
			break;
	}
	
	return p_ret_val;
}


ISR_DIRECT_DECLARE(swi1_isr)
{
	uint8_t type = 0;
	int err;
	
	// Initialize to invalid session id
	mpsl_timeslot_session_id_t session_id = 0xFFu;
	
	while (!ring_buf_is_empty(&callback_ring_buf)) {
		if (ring_buf_get(&callback_ring_buf, &type, 1) == 1) {
			switch (type) {
				case SWI_OPEN_SESSION:
					err = mpsl_timeslot_session_open(mpsl_timeslot_callback, &session_id);
					if (err) {
						LOG_ERR("Timeslot session open error: %d", err);
						k_oops();
					}
					break;

				case SWI_MAKE_REQUEST:
					err = mpsl_timeslot_request(session_id, &timeslot_request_earliest);
					if (err) {
						LOG_ERR("Timeslot request error: %d", err);
						k_oops();
					}
					break;

				case SWI_CLOSE_SESSION:
					err = mpsl_timeslot_session_close(session_id);
					if (err) {
						LOG_ERR("Timeslot session close error: %d", err);
						k_oops();
					}
					break;	

				case SWI_TS_STARTED:
					m_callback(APP_TS_STARTED);
					break;

				case SWI_TS_STOPPED:
					m_callback(APP_TS_STOPPED);
					break;

				default:
					LOG_ERR("Callback: Unknown type: %d", signal_type);
					break;
			}
		}
	}

	ISR_DIRECT_PM();
	return 1;
}

void timeslot_init(timeslot_callback_t callback)
{
	m_callback = callback;

	callback_ring_buf_put(SWI_OPEN_SESSION);

	callback_ring_buf_put(SWI_MAKE_REQUEST);

#if defined(CONFIG_SOC_SERIES_NRF53X)
	IRQ_DIRECT_CONNECT(SWI1_IRQn, 1, swi1_isr, 0);
	irq_enable(SWI1_IRQn);
#elif defined(CONFIG_SOC_SERIES_NRF52X)
	IRQ_DIRECT_CONNECT(SWI1_EGU1_IRQn, 1, swi1_isr, 0);
	irq_enable(SWI1_EGU1_IRQn);
#endif
}
