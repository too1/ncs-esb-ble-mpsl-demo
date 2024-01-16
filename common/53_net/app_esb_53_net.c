/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/init.h>

#include <nrf.h>
#include <esb.h>
#include "app_esb.h"
#include <esb_rpc_ids.h>

#include <nrf_rpc/nrf_rpc_ipc.h>
#include <nrf_rpc_cbor.h>
#include <zcbor_common.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#define CBOR_BUF_SIZE 16

LOG_MODULE_REGISTER(rpc_net, LOG_LEVEL_DBG);

/* See rpc_app.c (in the cpuapp/ dir) for an explanation. */
NRF_RPC_IPC_TRANSPORT(esb_group_tr, DEVICE_DT_GET(DT_NODELABEL(ipc0)), "nrf_rpc_ept");
NRF_RPC_GROUP_DEFINE(esb_group, "esb_group_id", &esb_group_tr, NULL, NULL, NULL);

static void rpc_esb_event_send(uint32_t evt_type, uint8_t  *rx_payload_buf, uint32_t rx_payload_length);

static void work_send_evt_tx_success_func(struct k_work *item);
static void work_send_evt_tx_fail_func(struct k_work *item);
static void work_send_evt_rx_received_func(struct k_work *item);

K_WORK_DEFINE(m_work_send_evt_tx_success, work_send_evt_tx_success_func);
K_WORK_DEFINE(m_work_send_evt_tx_fail, work_send_evt_tx_fail_func);
K_WORK_DEFINE(m_work_send_evt_rx_received, work_send_evt_rx_received_func);

static uint8_t *last_rx_buf;
static uint32_t last_rx_length;

void on_esb_callback(app_esb_event_t *event)
{
	switch(event->evt_type) {
		case APP_ESB_EVT_TX_SUCCESS:
			LOG_INF("ESB TX success");
			k_work_submit(&m_work_send_evt_tx_success);
			break;
		case APP_ESB_EVT_TX_FAIL:
			LOG_INF("ESB TX failed");
			k_work_submit(&m_work_send_evt_tx_fail);
			break;
		case APP_ESB_EVT_RX:
			LOG_INF("ESB RX: 0x%.2x-0x%.2x-0x%.2x-0x%.2x", event->buf[0], event->buf[1], event->buf[2], event->buf[3]);
			last_rx_buf = event->buf;
			last_rx_length = event->data_length;
			k_work_submit(&m_work_send_evt_rx_received);
			break;
		default:
			LOG_ERR("Unknown APP ESB event!");
			break;
	}
}

static void work_send_evt_tx_success_func(struct k_work *item)
{
	rpc_esb_event_send(APP_ESB_EVT_TX_SUCCESS, 0, 0);
}

static void work_send_evt_tx_fail_func(struct k_work *item)
{
	rpc_esb_event_send(APP_ESB_EVT_TX_FAIL, 0, 0);
}

static void work_send_evt_rx_received_func(struct k_work *item)
{
	rpc_esb_event_send(APP_ESB_EVT_RX, last_rx_buf, last_rx_length);
}

static int decode_struct(struct nrf_rpc_cbor_ctx *ctx, void *struct_ptr, size_t expected_size)
{
	struct zcbor_string zst;
	int err;

	if (zcbor_bstr_decode(ctx->zs, &zst)) {
		err = 0;
	} else {
		err = -EBADMSG;
	}

	if (expected_size != zst.len) {
		LOG_ERR("struct size mismatch: expect %d got %d", expected_size, zst.len);
		err = -EMSGSIZE;
	}

	if (!err) {
		memcpy(struct_ptr, zst.value, zst.len);
	} else {
		LOG_ERR("decoding failed");
	}

	return err;
}

/* Encode and send the return value (errcode) of `esb_simple_init`. */
static void rpc_rsp(int32_t err)
{
	struct nrf_rpc_cbor_ctx ctx;

	NRF_RPC_CBOR_ALLOC(&esb_group, ctx, CBOR_BUF_SIZE);

	zcbor_int32_put(ctx.zs, err);

	nrf_rpc_cbor_rsp_no_err(&esb_group, &ctx);
}

/* `esb_simple_init` RPC command handler.
 *
 * This will get called when the other core sends an nRF RPC command for the
 * group `esb_group` with command ID `RPC_COMMAND_INIT`.
 *
 * This command handler is registered using `NRF_RPC_CBOR_CMD_DECODER` at the
 * bottom of this file.
 */
static void rpc_esb_init_handler(const struct nrf_rpc_group *group,
				    struct nrf_rpc_cbor_ctx *ctx,
				    void *handler_data)
{
	LOG_DBG("");

	int32_t err = 0;
	app_esb_config_t config;

	if (decode_struct(ctx, &config, sizeof(config))) {
		LOG_DBG("decoding config struct failed");
		err = -EBADMSG;
	}

	/* Call this as soon as the data has been pulled from the RPC CBOR buffer.
	 *
	 * This is important because nRF RPC will not be able to process another
	 * command (sent from the other core) until `nrf_rpc_cbor_decoding_done`
	 * has been called.
	 *
	 * The underlying reason is that nRF RPC over IPC will process incoming
	 * items in a workqueue, and an item is only marked as processed when
	 * this function is called (freeing the workqueue for the next one).
	 */
	nrf_rpc_cbor_decoding_done(group, ctx);

	if (!err) {
		LOG_DBG("app_esb_init. Mode %i", config.mode);
		err = app_esb_init(config.mode, on_esb_callback);
		if (err) {
			LOG_ERR("app_esb init failed (err %d)", err);
		}
	}

	/* Encode the errcode and send it to the other core. */
	rpc_rsp(err);
}

static void rpc_esb_tx_handler(const struct nrf_rpc_group *group,
				  struct nrf_rpc_cbor_ctx *ctx,
				  void *handler_data)
{
	int err = 0;
	app_esb_data_t tx_payload;

	if (decode_struct(ctx, &tx_payload, sizeof(tx_payload))) {
		LOG_DBG("decoding app_esb_data_t struct failed");
		err = -EBADMSG;
	}

	nrf_rpc_cbor_decoding_done(&esb_group, ctx);

	if (!err) {
		LOG_INF("Send TX packet, data 0 0x%.2x, len %i", tx_payload.data[0], tx_payload.len);
		err = app_esb_send(&tx_payload);
		if (err < 0) {
			LOG_ERR("app_esb_send: error %i", err);
		}
	}

	rpc_rsp(err);
}

/* This is the callback passed to the esb_simple API, which
 * then calls the RPC remote callback (sends an event).
 *
 * On the remote (app core), the rpc event will then call
 * the function stored in p_rx_cb_remote.
 */
static void rpc_esb_event_send(uint32_t evt_type, uint8_t *rx_buf, uint32_t rx_length)
{
	int err = 0;
	struct nrf_rpc_cbor_ctx ctx;

	NRF_RPC_CBOR_ALLOC(&esb_group, ctx,
			   CBOR_BUF_SIZE +
			   sizeof(err) +
			   sizeof(evt_type) +
			   sizeof(uint32_t) + 
			   rx_length);

	/* Always encode the error */
	if (!zcbor_int32_put(ctx.zs, err)) {
		err = -EINVAL;
	}

	if (err || !zcbor_uint32_put(ctx.zs, evt_type)) {
		err = -EINVAL;
	}

	if (err || !zcbor_uint32_put(ctx.zs, rx_length)) {
		err = -EINVAL;
	}

	if (rx_length > 0) {
		if (err || !zcbor_bstr_encode_ptr(ctx.zs, rx_buf, rx_length)) {
			err = -EINVAL;
		}
	}

	if (!err) {
		err = nrf_rpc_cbor_evt(&esb_group, RPC_EVENT_ESB_CB, &ctx);
	}

	if (!err) {
		LOG_DBG("evt send ok");
	} else {
		LOG_DBG("evt send err %d", err);
	}
}

NRF_RPC_CBOR_CMD_DECODER(esb_group, rpc_esb_init, RPC_COMMAND_ESB_INIT, rpc_esb_init_handler, NULL);
NRF_RPC_CBOR_CMD_DECODER(esb_group, rpc_esb_tx,   RPC_COMMAND_ESB_TX,   rpc_esb_tx_handler,   NULL);

static void err_handler(const struct nrf_rpc_err_report *report)
{
	LOG_ERR("nRF RPC error %d. Enable nRF RPC logs for details.", report->code);
	k_oops();
}

static int serialization_init(void)
{
	int err;

	LOG_DBG("nRF RPC init begin");

	err = nrf_rpc_init(err_handler);
	if (err) {
		return -EINVAL;
	}

	LOG_DBG("nRF RPC init ok");

	return 0;
}

SYS_INIT(serialization_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
