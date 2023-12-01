#include "app_esb.h"
#include "timeslot_handler.h"
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <esb.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_esb, LOG_LEVEL_INF);

static app_esb_callback_t m_callback;

static app_esb_event_t 	  m_event;

// Define a buffer of payloads (8 items by default) to store TX payloads in between timeslots
K_MSGQ_DEFINE(m_msgq_tx_payloads, sizeof(struct esb_payload), 8, 4);

static struct esb_payload rx_payload;

static app_esb_mode_t m_mode;
static bool m_active = false;

static int pull_packet_from_tx_msgq(void);

static void on_timeslot_start_stop(timeslot_callback_type_t type);

static void event_handler(struct esb_evt const *event)
{
	static struct esb_payload tmp_payload;
	switch (event->evt_id) {
		case ESB_EVENT_TX_SUCCESS:
			LOG_DBG("TX SUCCESS EVENT");

			// Remove the oldest item in the TX queue
			k_msgq_get(&m_msgq_tx_payloads, &tmp_payload, K_NO_WAIT);
			
			// Forward an event to the application 
			m_event.evt_type = APP_ESB_EVT_TX_SUCCESS;
			m_event.data_length = 0;
			m_callback(&m_event);

			// Check if there are more messages in the queue
			if(pull_packet_from_tx_msgq() == 0){
				LOG_DBG("PCK loaded in ESB TX callback");
			}
			break;

		case ESB_EVENT_TX_FAILED:
			LOG_DBG("TX FAILED EVENT");

			// Ignore this event for now, since the payload is retained in the queue and will be retransmitted at a later point
			
			//m_event.evt_type = APP_ESB_EVT_TX_FAIL;
			//m_event.data_length = 0;
			//m_callback(&m_event);
		
			esb_flush_tx();

			// Check if there are more messages in the queue
			if(pull_packet_from_tx_msgq() == 0){
				LOG_DBG("PCK loaded in ESB fail callback");
			}
			break;

		case ESB_EVENT_RX_RECEIVED:
			while (esb_read_rx_payload(&rx_payload) == 0) {
				LOG_DBG("Packet received, len %d : ", rx_payload.length);

				m_event.evt_type = APP_ESB_EVT_RX;
				m_event.buf = rx_payload.data;
				m_event.data_length = rx_payload.length;
				m_callback(&m_event);
			}
			break;
	}
}

static int clocks_start(void)
{
	int err;
	int res;
	struct onoff_manager *clk_mgr;
	struct onoff_client clk_cli;

	clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (!clk_mgr) {
		LOG_ERR("Unable to get the Clock manager");
		return -ENXIO;
	}

	sys_notify_init_spinwait(&clk_cli.notify);

	err = onoff_request(clk_mgr, &clk_cli);
	if (err < 0) {
		LOG_ERR("Clock request failed: %d", err);
		return err;
	}

	do {
		err = sys_notify_fetch_result(&clk_cli.notify, &res);
		if (!err && res) {
			LOG_ERR("Clock could not be started: %d", res);
			return res;
		}
	} while (err);

	LOG_DBG("HF clock started");
	return 0;
}

static int esb_initialize(app_esb_mode_t mode)
{
	int err;

	/* These are arbitrary default addresses. In end user products
	 * different addresses should be used for each set of devices.
	 */
	uint8_t base_addr_0[4] = {0xE7, 0xE7, 0xE7, 0xE7};
	uint8_t base_addr_1[4] = {0xC2, 0xC2, 0xC2, 0xC2};
	uint8_t addr_prefix[8] = {0xE7, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8};

	struct esb_config config = ESB_DEFAULT_CONFIG;

	config.protocol = ESB_PROTOCOL_ESB_DPL;
	config.retransmit_delay = 600;
	config.retransmit_count = 1;
	config.bitrate = ESB_BITRATE_2MBPS;
	config.event_handler = event_handler;
	config.mode = (mode == APP_ESB_MODE_PTX) ? ESB_MODE_PTX : ESB_MODE_PRX;
	config.tx_mode = ESB_TXMODE_MANUAL_START;
	config.selective_auto_ack = true;

	err = esb_init(&config);

	if (err) {
		return err;
	}

	err = esb_set_base_address_0(base_addr_0);
	if (err) {
		return err;
	}

	err = esb_set_base_address_1(base_addr_1);
	if (err) {
		return err;
	}

	err = esb_set_prefixes(addr_prefix, ARRAY_SIZE(addr_prefix));
	if (err) {
		return err;
	}

	NVIC_SetPriority(RADIO_IRQn, 0);

	if (mode == APP_ESB_MODE_PRX) {
		esb_start_rx();
	}

	return 0;
}

static int pull_packet_from_tx_msgq(void)
{
	int ret;
	static struct esb_payload tx_payload;
	if (k_msgq_peek(&m_msgq_tx_payloads, &tx_payload) == 0) {
		ret = esb_write_payload(&tx_payload);
		if (ret < 0) return ret;
		esb_start_tx();		

		return 0;
	}
	
	return -ENOMEM;
}


int app_esb_init(app_esb_mode_t mode, app_esb_callback_t callback)
{
	int ret;

	m_callback = callback;
	m_mode = mode;
	
	NRF_P0->DIRSET = BIT(28) | BIT(29) | BIT(30) | BIT(31) | BIT(4);
	NRF_P0->OUTCLR = BIT(28) | BIT(29) | BIT(30) | BIT(31);

	ret = clocks_start();
	if (ret < 0) {
		return ret;
	}

	timeslot_handler_init(on_timeslot_start_stop);

	return 0;
}

int app_esb_send(uint8_t *buf, uint32_t length)
{
	int ret = 0;
	static struct esb_payload tx_payload;
	tx_payload.pipe = 0;
	tx_payload.noack = false;
	memcpy(tx_payload.data, buf, length);
	tx_payload.length = length;
	ret = k_msgq_put(&m_msgq_tx_payloads, &tx_payload, K_NO_WAIT);
	if (ret == 0) {
		if (m_active) {
			pull_packet_from_tx_msgq();
		}
	}
	else {
		return -ENOMEM;
	}
	return 0;
}

static int app_esb_suspend(void)
{
	m_active = false;
	NRF_P0->OUTSET = BIT(29);
	if(m_mode == APP_ESB_MODE_PTX) {
		uint32_t irq_key = irq_lock();

		irq_disable(RADIO_IRQn);
		NVIC_DisableIRQ(RADIO_IRQn);

		NRF_RADIO->SHORTS = 0;

		NRF_RADIO->EVENTS_DISABLED = 0;
		NRF_RADIO->TASKS_DISABLE = 1;
		while(NRF_RADIO->EVENTS_DISABLED == 0);

		NRF_TIMER2->TASKS_STOP = 1;
		NRF_RADIO->INTENCLR = 0xFFFFFFFF;
		
		esb_disable();

		NVIC_ClearPendingIRQ(RADIO_IRQn);

		irq_unlock(irq_key);
	}
	else {
		esb_stop_rx();
	}
	NRF_P0->OUTCLR = BIT(29);

	// Todo: Figure out how to use the esb_suspend() function rather than having to disable at the end of every timeslot
	//esb_suspend();
	return 0;
}

static int app_esb_resume(void)
{
	NRF_P0->OUTSET = BIT(29);
	if(m_mode == APP_ESB_MODE_PTX) {
		int err = esb_initialize(m_mode);
		m_active = true;
		NRF_P0->OUTCLR = BIT(29);
		pull_packet_from_tx_msgq();
		return err;
	}
	else {
		int err = esb_initialize(m_mode);
		m_active = true;
		NRF_P0->OUTCLR = BIT(29);
		return err;
	}
}

/* Callback function signalling that a timeslot is started or stopped */
static void on_timeslot_start_stop(timeslot_callback_type_t type)
{
	switch (type) {
		case APP_TS_STARTED:
			NRF_P0->OUTSET = BIT(31);
			app_esb_resume();
			break;
		case APP_TS_STOPPED:
			NRF_P0->OUTCLR = BIT(31);
			app_esb_suspend();
			break;
	}
}
