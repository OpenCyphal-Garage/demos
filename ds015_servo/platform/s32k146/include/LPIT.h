/*
 * Copyright (c) 2020, NXP. All rights reserved.
 * Distributed under The MIT License.
 * Author: Abraham Rodriguez <abraham.rodriguez@nxp.com>
 */

#ifndef TIMER_LPIT_H_
#define TIMER_LPIT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


// Initializes a 64-bit time-stamping monotonic timer using 2 LPIT0 channels, 0 and 1 in chain mode
void LPIT0_Timestamping_Timer_Init(void);

uint64_t LPIT0_GetTimestamp(void);

void LPIT0_Ch2_IRQ_Config(uint32_t interrupt_freq, uint8_t interrupt_priority, void (*callback)());

#ifdef __cplusplus
}
#endif

#endif /* TIMER_LPIT_H_ */


