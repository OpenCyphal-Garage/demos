/*
 * Copyright (c) 2020, NXP. All rights reserved.
 * Distributed under The MIT License.
 * Author: Abraham Rodriguez <abraham.rodriguez@nxp.com>
 */

#ifndef CLOCKS_SCG_H_
#define CLOCKS_SCG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void SCG_SOSC_8MHz_Init		(void);
void SCG_SPLL_160MHz_Init	(void);
void SCG_Normal_RUN_Init	(void);

#ifdef __cplusplus
}
#endif

#endif /* CLOCKS_SCG_H_ */
