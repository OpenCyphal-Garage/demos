/*
 * Copyright (c) 2020, NXP. All rights reserved.
 * Distributed under The MIT License.
 * Author: Abraham Rodriguez <abraham.rodriguez@nxp.com>
 */

#ifndef MEDIA_CANFD_H_
#define MEDIA_CANFD_H_


#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	CANFD_125KB_500KB_XTAL, // Calculate predef sample points
	CANFD_250KB_500KB_XTAL,
	CANFD_250KB_1MB_XTAL,
	CANFD_250KB_1MB_PLL,
	CANFD_500KB_1MB_PLL,
	CANFD_500KB_2MB_PLL,
	CANFD_1MB_2MB_PLL,
	CANFD_1MB_4MB_PLL
} CANFD_bitrate_profile_t;

#define CANFD_bitrate_profile_NUM (8u)

/*!
* @brief Status codes for the return value status
*/
typedef enum
{
	FAILURE = 0,
	SUCCESS = 1
} status_t;

#define MTU_CANFD (64u)

typedef struct {
	uint32_t EXTENDED_ID;
	size_t PAYLOAD_SIZE_BYTES;
	const void* PAYLOAD;
} fdframe_t;

// Select a profile that is coherent to the closcks sources configured by the application
status_t FlexCAN0_Init(CANFD_bitrate_profile_t profile, uint8_t irq_priority, void (*callback)());

/*!
* @brief Setup a message buffer for reception of a specific ID
*
* @param [uint32_t id] filter
*
* @return Success If the filters were installed correctly
* @return Failure If the setup couldn't be performed
*/
status_t FlexCAN0_Install_ID(uint32_t id, uint8_t mb_index);

status_t FlexCAN0_Send(fdframe_t* frame);

#ifdef __cplusplus
}
#endif

#endif /* MEDIA_CANFD_H_ */


