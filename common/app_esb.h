#ifndef __APP_ESB_H
#define __APP_ESB_H

#include <zephyr/kernel.h>

typedef enum {APP_ESB_EVT_TX_SUCCESS, APP_ESB_EVT_TX_FAIL, APP_ESB_EVT_RX} app_esb_event_type_t;

typedef struct {
	app_esb_event_type_t evt_type;
	uint8_t *buf;
	uint32_t data_length;
} app_esb_event_t;

typedef void (*app_esb_callback_t)(app_esb_event_t *event);

typedef enum {APP_ESB_MODE_PTX, APP_ESB_MODE_PRX} app_esb_mode_t;

int app_esb_init(app_esb_mode_t mode, app_esb_callback_t callback);

int app_esb_send(uint8_t *buf, uint32_t length);

int app_esb_suspend(void);

int app_esb_resume(void);

void app_esb_safe_period_start_stop(bool started);

#endif