#ifndef __APP_TIMESLOT_H
#define __APP_TIMESLOT_H

typedef enum {APP_TS_STARTED, APP_TS_STOPPED} timeslot_callback_type_t;
typedef void (*timeslot_callback_t)(timeslot_callback_type_t type);

void timeslot_init(timeslot_callback_t callback);

void timeslot_req(void);

#endif