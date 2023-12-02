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
#include "53_app/radio_regs.h"
#include "app_esb.h"
#include <esb_rpc_ids.h>

#include <nrf_rpc/nrf_rpc_ipc.h>
#include <nrf_rpc_cbor.h>
#include <zcbor_common.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#define CBOR_BUF_SIZE 16

LOG_MODULE_REGISTER(rpc_app, LOG_LEVEL_INF);

/* This defines a transport for our RPC command group.
 * Here we use the IPC transport (nrf_rpc_ipc.h):
 * - we tell it to use the IPC device `ipc0` (in the devicetree)
 * - we tell it to use an endpoint named `nrf_rpc_ept`. There can be multiple endpoints,
 *   e.g. one for HCI and one for nRF RPC. Usually it's not one ept per API, it's one ept
 *   per library (hci uses one, 802154 another, nRF RPC another, and so on).
 */
NRF_RPC_IPC_TRANSPORT(esb_group_tr, DEVICE_DT_GET(DT_NODELABEL(ipc0)), "nrf_rpc_ept");

/* This defines the group for our API.
 *
 * Command groups are used to logically separate APIs called over nRF RPC: e.g.,
 * we can have a Bluetooth group (e.g. for Bluetooth host over nRF RPC, which is
 * not used in this sample), an ESB group and let's say a crypto group.
 *
 * This aids the application developer, as he now doesn't have to keep track of
 * all the registered nRF RPC command IDs. It also allows for more flexibility,
 * as modules making use of nRF RPC can be compiled in and out without needed to
 * edit the command IDs every time.
 */
NRF_RPC_GROUP_DEFINE(esb_group, "esb_group_id", &esb_group_tr, NULL, NULL, NULL);

/* - Pull an error code from the RPC CBOR buffer
 * - Place it in `handler_data`, retrieved in the ESB API and passed to the application
 * - Also return it (as a convenience)
 */
static int decode_error(const struct nrf_rpc_group *group,
			struct nrf_rpc_cbor_ctx *ctx,
			void *handler_data)
{
	int err;
	int32_t *p_err = (int32_t *)handler_data;

	if (!zcbor_int32_decode(ctx->zs, &err)) {
		err = -EBADMSG;
	}

	if (p_err) {
		*p_err = err;
	}

	return err;
}

/* Default response handler. Decodes the error returned by the ESB API (ran
 * on the other core) and frees the CBOR buffer.
 */
static void rsp_handler(const struct nrf_rpc_group *group,
			struct nrf_rpc_cbor_ctx *ctx,
			void *handler_data)
{
	decode_error(group, ctx, handler_data);

	nrf_rpc_cbor_decoding_done(&esb_group, ctx);
}

int esb_simple_init(app_esb_config_t *p_config, struct esb_simple_addr *p_addr)
{
	int32_t err;
	int err_rpc;
	struct nrf_rpc_cbor_ctx ctx;
	size_t config_len = sizeof(app_esb_config_t);
	size_t addr_len = sizeof(struct esb_simple_addr);

	NRF_RPC_CBOR_ALLOC(&esb_group, ctx, CBOR_BUF_SIZE + config_len + addr_len);

	/* Serialize the `config` struct to a byte array, encode it and place it
	 * in the CBOR buffer.
	 *
	 * We play fast and loose with the memory layout because we assume that
	 * the other core's FW was compiled with the exact same toolchain and
	 * compiler options, resulting in the same memory layout on the other
	 * side.
	 *
	 * Note: a gotcha is that the `zcbor_` APIs return `true` on success,
	 * whereas almost all zephyr (and other NCS) APIs return a `0` on success.
	 */
	if (!zcbor_bstr_encode_ptr(ctx.zs, (const uint8_t *)p_config, config_len)) {
		return -EINVAL;
	}

	/* Ditto for `addr`. */
	if (!zcbor_bstr_encode_ptr(ctx.zs, (const uint8_t *)p_addr, addr_len)) {
		return -EINVAL;
	}

	LOG_DBG("send cmd %p 0x%x %p %p %p", &esb_group, RPC_COMMAND_INIT, &ctx, rsp_handler, &err);

	err_rpc = nrf_rpc_cbor_cmd(&esb_group, RPC_COMMAND_INIT, &ctx, rsp_handler, &err);

	/* Return a fixed error code if the RPC transport had an error. Else,
	 * return the result of the API called on the other core.
	 */
	if (err_rpc) {
		return -EINVAL;
	} else {
		return err;
	}
}

static void simple_rx_rsp_handler(const struct nrf_rpc_group *group,
				  struct nrf_rpc_cbor_ctx *ctx,
				  void *handler_data)
{
	int err;
	uint32_t p_rx_payload;
	struct zcbor_string zst;

	LOG_DBG("");

	/* Try pulling the error code. */
	err = decode_error(group, ctx, handler_data);

	if (err || !zcbor_uint32_decode(ctx->zs, &p_rx_payload)) {
		err = -EBADMSG;
	}

	/* Don't write data to the null pointer. */
	if (!p_rx_payload) {
		err = -EFAULT;
	}

	if (err || !zcbor_bstr_decode(ctx->zs, &zst)) {
		err = -EBADMSG;
	}

	if (zst.len != sizeof(app_esb_data_t)) {
		LOG_ERR("struct size mismatch: expect %d got %d",
			sizeof(app_esb_data_t),
			zst.len);
		err = -EMSGSIZE;
	}

	if (!err) {
		memcpy((app_esb_data_t *)p_rx_payload, zst.value, zst.len);
		LOG_DBG("decoding ok: rx_payload 0x%x", p_rx_payload);
	} else {
		LOG_DBG("%s: decoding error %d", __func__, err);
	}

	nrf_rpc_cbor_decoding_done(&esb_group, ctx);
}

/* RPC command helper. We use this to encode and send `rpc_simple_` API
 * commands over nRF RPC to the other side.
 */
static int rpc_radio_cmd(enum rpc_command cmd,
			 app_esb_data_t *p_rx_payload,
			 app_esb_callback_t *p_rx_cb)
{
	int32_t err;
	int err_rpc;
	struct nrf_rpc_cbor_ctx ctx;

	/* Allocate an RPC CBOR buffer */
	NRF_RPC_CBOR_ALLOC(&esb_group, ctx, CBOR_BUF_SIZE + sizeof(p_rx_payload));

	/* Push the (local) payload destination pointer to the command buffer.
	 * This will be returned to us by the other side when receiving the response.
	 */
	if (!zcbor_uint32_put(ctx.zs, (uint32_t)p_rx_payload)) {
		return -EINVAL;
	}

	/* If a (local again) callback function pointer was supplied, also push
	 * it to the command buffer. It will be returned to us for each RX
	 * event, that way our RPC RX event handler will be able to call the
	 * user-supplied callback. This is only encoded and used for the async
	 * API.
	 */
	if (p_rx_cb) {
		if (!zcbor_uint32_put(ctx.zs, (uint32_t)p_rx_cb)) {
			return -EINVAL;
		}
	}

	/* Send the command, parse the response using a handler.
	 *
	 * The other side will get the command, execute its command handler, and
	 * reply with a response.
	 *
	 * On our side, the response handler, here `simple_rx_rsp_handler`, will
	 * be called to parse the response buffer.
	 *
	 * It is only after `simple_rx_rsp_handler` returns that this call to
	 * `nrf_rpc_cbor_cmd` will return.
	 */
	LOG_DBG("send cmd %p 0x%x %p %p %p", &esb_group, cmd, &ctx, simple_rx_rsp_handler, &err);
	err_rpc = nrf_rpc_cbor_cmd(&esb_group, cmd, &ctx, simple_rx_rsp_handler, &err);
	LOG_DBG("cmd returned");

	if (err_rpc) {
		return -EINVAL;
	} else {
		return err;
	}
}

int esb_simple_rx(app_esb_data_t *p_rx_payload)
{
	LOG_DBG("");

	return rpc_radio_cmd(RPC_COMMAND_TX, p_rx_payload, NULL);
}

static void rx_cb_handler(const struct nrf_rpc_group *group,
			  struct nrf_rpc_cbor_ctx *ctx,
			  void *handler_data)
{
	int err;
	uint32_t p_rx_payload;
	uint32_t p_rx_cb;
	struct zcbor_string zst;

	LOG_DBG("");

	/* Try pulling the error code. */
	err = decode_error(group, ctx, handler_data);

	if (err || !zcbor_uint32_decode(ctx->zs, &p_rx_cb)) {
		err = -EBADMSG;
	}

	/* If the callback pointer is not valid, decoding the payload is
	 * pointless, as the application cannot be notified of the new data.
	 */
	if (!p_rx_cb) {
		err = -EFAULT;
	}

	if (err || !zcbor_uint32_decode(ctx->zs, &p_rx_payload)) {
		err = -EBADMSG;
	}

	/* Don't write data to the null pointer. */
	if (!p_rx_payload) {
		err = -EFAULT;
	}

	if (err || !zcbor_bstr_decode(ctx->zs, &zst)) {
		err = -EBADMSG;
	}

	if (zst.len != sizeof(app_esb_event_t)) {
		LOG_ERR("struct size mismatch: expect %d got %d",
			sizeof(app_esb_event_t),
			zst.len);
		err = -EMSGSIZE;
	}

	if (!err) {
		memcpy((app_esb_event_t *)p_rx_payload, zst.value, zst.len);
	}

	nrf_rpc_cbor_decoding_done(&esb_group, ctx);

	/* Notify the app new data has been received. */
	if (!err) {
		LOG_DBG("decoding ok: rx_cb 0x%x rx_payload 0x%x", p_rx_cb, p_rx_payload);
		((app_esb_callback_t)p_rx_cb)((app_esb_event_t *)p_rx_payload);
	} else {
		LOG_ERR("%s: decoding error %d", __func__, err);
	}

}

/* Register the RX event handler (function above). This will be sent from the
 * other side whenever we are in async mode and a packet has been received.
 */
NRF_RPC_CBOR_EVT_DECODER(esb_group, rx_cb_handler, RPC_EVENT_RX_CB, rx_cb_handler, NULL);

/* Initialize nRF RPC right after kernel boots, but before the application is
 * run.
 */
static void err_handler(const struct nrf_rpc_err_report *report)
{
	LOG_ERR("nRF RPC error %d. Enable nRF RPC logs for details.", report->code);

	k_oops();
}

static int serialization_init(void)
{
	int err;

	LOG_DBG("esb rpc init begin");

	err = nrf_rpc_init(err_handler);
	if (err) {
		return -EINVAL;
	}

	LOG_DBG("esb rpc init ok");

	return 0;
}

SYS_INIT(serialization_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

int app_esb_init(app_esb_mode_t mode, app_esb_callback_t callback)
{
    static app_esb_config_t config;
    static struct esb_simple_addr addr;
    config.mode = mode;
    int err = esb_simple_init(&config, &addr);
    if (err < 0) {
        return err;
    }

    return 0;
}

int app_esb_send(uint8_t *buf, uint32_t length)
{
    static app_esb_data_t dummy_payload;
    memcpy(dummy_payload.data, buf, length);
    esb_simple_rx(&dummy_payload);
    return 0;
}
