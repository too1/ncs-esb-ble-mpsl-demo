#ifndef __APP_ESB_H
#define __APP_ESB_H

#include <zephyr/kernel.h>

typedef enum {APP_ESB_EVT_TX_SUCCESS, APP_ESB_EVT_TX_FAIL, APP_ESB_EVT_RX} app_esb_event_type_t;
typedef enum {APP_ESB_MODE_PTX, APP_ESB_MODE_PRX} app_esb_mode_t;

typedef struct {
	app_esb_event_type_t evt_type;
	uint8_t *buf;
	uint32_t data_length;
} app_esb_event_t;

typedef struct {
	uint8_t data[32];
	uint32_t len;
} app_esb_data_t;

typedef struct {
	app_esb_mode_t mode;
} app_esb_config_t;

typedef void (*app_esb_callback_t)(app_esb_event_t *event);

struct esb_simple_addr {
	uint8_t base_0[4];
	uint8_t base_1[4];
	uint8_t prefix[8];
};

int app_esb_init(app_esb_mode_t mode, app_esb_callback_t callback);

int app_esb_send(uint8_t *buf, uint32_t length);

#endif