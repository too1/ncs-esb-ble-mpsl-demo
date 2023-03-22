#include "app_esb.h"
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <esb.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_esb, LOG_LEVEL_INF);

static app_esb_callback_t m_callback;

static app_esb_event_t 	  m_event;

static struct esb_payload rx_payload;

static bool m_active = false;

static void event_handler(struct esb_evt const *event)
{
	switch (event->evt_id) {
		case ESB_EVENT_TX_SUCCESS:
			LOG_DBG("TX SUCCESS EVENT");

			m_event.evt_type = APP_ESB_EVT_TX_SUCCESS;
			m_event.data_length = 0;
			m_callback(&m_event);
			break;
		case ESB_EVENT_TX_FAILED:
			LOG_DBG("TX FAILED EVENT");
			
			m_event.evt_type = APP_ESB_EVT_TX_FAIL;
			m_event.data_length = 0;
			m_callback(&m_event);
			
			esb_flush_tx();
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

static int esb_initialize(void)
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
	config.retransmit_count = 8;
	config.bitrate = ESB_BITRATE_2MBPS;
	config.event_handler = event_handler;
	config.mode = ESB_MODE_PTX;
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

	return 0;
}


int app_esb_init(app_esb_mode_t mode, app_esb_callback_t callback)
{
	int ret;

	m_callback = callback;
	
	ret = clocks_start();
	if (ret < 0) {
		return ret;
	}

	ret = esb_initialize();
	if (ret < 0) {
		return ret;
	}

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
	if(m_active) {
		ret = esb_write_payload(&tx_payload);
		if (ret < 0) return ret;
		ret = esb_start_tx();
		return ret;
	}
	else {
		return -EBUSY;
	}
}

int app_esb_suspend(void)
{
	m_active = false;
	esb_disable();

	// Todo: Figure out how to use the esb_suspend() function rather than having to disable at the end of every timeslot
	//esb_suspend();
	return 0;
}

int app_esb_resume(void)
{
	int err = esb_initialize();
	m_active = true;
	return err;
}
