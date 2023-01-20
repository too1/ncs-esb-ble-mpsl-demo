#ifndef __APP_TIMESLOT_H
#define __APP_TIMESLOT_H

typedef void (*timeslot_callback_t)(bool started);

void timeslot_init(timeslot_callback_t callback);

#endif