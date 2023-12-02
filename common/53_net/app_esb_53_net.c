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

/* This is used as a local payload destination buffer. The ESB stack will write
 * its received packet data there, and we will send it over to the other core
 * via nRF RPC.
 *
 * This struct resides in our local RAM and not in the shared memory region. The
 * shared memory region is only used by the IPC service. The IPC service API is
 * then used by nRF RPC (this file) and HCI over RPMSG (in `main.c`).
 */
static app_esb_data_t esb_rx_payload;


int esb_simple_init(app_esb_config_t *p_config, struct esb_simple_addr *p_addr)
{
    LOG_DBG("esb_simple_init. Mode %i", p_config->mode);
    return 0;
}

int esb_simple_rx(app_esb_data_t *p_rx_payload)
{
	LOG_DBG("RX cmd: %i", p_rx_payload->data[0]);

	return 0;
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
static void simple_init_rsp(int32_t err)
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
static void esb_simple_init_handler(const struct nrf_rpc_group *group,
				    struct nrf_rpc_cbor_ctx *ctx,
				    void *handler_data)
{
	LOG_DBG("");

	int32_t err = 0;
	app_esb_config_t config;
	struct esb_simple_addr addr;

	if (decode_struct(ctx, &config, sizeof(config))) {
		LOG_DBG("decoding config struct failed");
		err = -EBADMSG;
	}

	if (err || decode_struct(ctx, &addr, sizeof(addr))) {
		LOG_DBG("decoding addr struct failed");
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
		LOG_DBG("decode ok");
		err = esb_simple_init(&config, &addr);
		LOG_DBG("esb_simple_init err %d", err);
	}

	/* Encode the errcode and send it to the other core. */
	simple_init_rsp(err);
}

static void simple_rx_rsp(uint32_t remote_rx_payload_ptr, int32_t err)
{
	struct nrf_rpc_cbor_ctx ctx;

	LOG_DBG("");

	NRF_RPC_CBOR_ALLOC(&esb_group, ctx,
			   CBOR_BUF_SIZE +
			   sizeof(err) +
			   sizeof(remote_rx_payload_ptr) +
			   sizeof(esb_rx_payload));

	zcbor_int32_put(ctx.zs, err);
	zcbor_uint32_put(ctx.zs, remote_rx_payload_ptr);
	zcbor_bstr_encode_ptr(ctx.zs,
			      (const uint8_t *)&esb_rx_payload,
			      sizeof(esb_rx_payload));

	nrf_rpc_cbor_rsp_no_err(&esb_group, &ctx);
}

static void esb_simple_rx_handler(const struct nrf_rpc_group *group,
				  struct nrf_rpc_cbor_ctx *ctx,
				  void *handler_data)
{
	int err;
	uint32_t p_rx_payload;

	LOG_DBG("");

	if (zcbor_uint32_decode(ctx->zs, &p_rx_payload)) {
		err = 0;
	} else {
		err = -EBADMSG;
		p_rx_payload = 0;
	}

	nrf_rpc_cbor_decoding_done(&esb_group, ctx);

	if (!err) {
		err = esb_simple_rx(&p_rx_payload);
	}

	simple_rx_rsp(p_rx_payload, err);
}

/* Holds the address to the remote-side callback */
static app_esb_callback_t p_rx_cb_remote = NULL;

/* Holds the address of the remote-side payload struct */
/* Only needed for the ASYNC API. */
static app_esb_data_t *p_rx_payload_remote = NULL;

/* This is the callback passed to the esb_simple API, which
 * then calls the RPC remote callback (sends an event).
 *
 * On the remote (app core), the rpc event will then call
 * the function stored in p_rx_cb_remote.
 */
static void local_rx_cb(app_esb_data_t *p_rx_payload)
{
	int err;
	struct nrf_rpc_cbor_ctx ctx;

	NRF_RPC_CBOR_ALLOC(&esb_group, ctx,
			   CBOR_BUF_SIZE +
			   sizeof(err) +
			   sizeof(p_rx_cb_remote) +
			   sizeof(p_rx_payload_remote) +
			   sizeof(esb_rx_payload));

	/* Don't push NULL pointers to the other side */
	if (p_rx_payload_remote && p_rx_cb_remote) {
		err = 0;
	} else {
		err = -EFAULT;
	}

	/* Always encode the error */
	if (!zcbor_int32_put(ctx.zs, err)) {
		err = -EINVAL;
	}

	if (err || !zcbor_uint32_put(ctx.zs, (uint32_t)p_rx_cb_remote)) {
		err = -EINVAL;
	}

	if (err || !zcbor_uint32_put(ctx.zs, (uint32_t)p_rx_payload_remote)) {
		err = -EINVAL;
	}

	if (!err) {
		if (!zcbor_bstr_encode_ptr(ctx.zs,
					  (const uint8_t *)&esb_rx_payload,
					  sizeof(esb_rx_payload))) {
			err = -EINVAL;
		}
	}

	if (!err) {
		err = nrf_rpc_cbor_evt(&esb_group,
				       RPC_EVENT_RX_CB,
				       &ctx);
	}

	if (!err) {
		LOG_DBG("evt send ok");
	} else {
		LOG_DBG("evt send err %d", err);
	}
}

NRF_RPC_CBOR_CMD_DECODER(esb_group, esb_simple_init, RPC_COMMAND_INIT, esb_simple_init_handler, NULL);
NRF_RPC_CBOR_CMD_DECODER(esb_group, esb_simple_rx,   RPC_COMMAND_TX,   esb_simple_rx_handler,   NULL);

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


int app_esb_init(app_esb_mode_t mode, app_esb_callback_t callback)
{
    return 0;
}

int app_esb_send(uint8_t *buf, uint32_t length)
{
    return 0;
}