/*
 * Copyright (c) 2020, NXP. All rights reserved.
 * Distributed under The MIT License.
 * Author: Abraham Rodriguez <abraham.rodriguez@nxp.com>
 */

#include "LPIT.h"
#include "S32K146_bitfields.h"

static inline void S32_NVIC_EnableIRQ(IRQn_Type IRQn)
{
	S32_NVIC->ISER[(((uint32_t)(int32_t)IRQn) >> 5UL)] = (uint32_t)(1UL << (((uint32_t)(int32_t)IRQn) & 0x1FUL));

	return;
}

static inline void S32_NVIC_SetPriority(IRQn_Type IRQn, uint32_t priority)
{
    S32_NVIC->IP[((uint32_t)(int32_t)IRQn)] = (uint8_t)((priority << (8U - __NVIC_PRIO_BITS)) & (uint32_t)0xFFUL);

    return;
}

static void (*LPIT0_Ch2_callback_ptr)(void) = 0;

void LPIT0_Timestamping_Timer_Init(void)
{
    /* Clock gating to LPIT module and peripheral clock source select option 6: (SPLLDIV2) at 80Mhz */
	PCC->PCC_LPIT_b.PCS = PCC_PCC_LPIT_PCS_110;
    PCC->PCC_LPIT_b.CGC = PCC_PCC_LPIT_CGC_1;

    /* Enable module */
    LPIT0->LPIT0_MCR_b.M_CEN = LPIT0_MCR_M_CEN_1;

    /* Select 32-bit periodic Timer for both chained channels and timeouts timer (default)  */
    LPIT0->LPIT0_TCTRL0_b.MODE = LPIT0_TCTRL0_MODE_0;
	LPIT0->LPIT0_TCTRL1_b.MODE = LPIT0_TCTRL1_MODE_0;

    /* Select chain mode for channel 1, this becomes the most significant 32 bits */
    LPIT0->LPIT0_TCTRL1_b.CHAIN = LPIT0_TCTRL1_CHAIN_1;

    /* Setup max reload value for both channels 0xFFFFFFFF */
    LPIT0->LPIT0_TVAL0_b.TMR_VAL = 0xFFFFFFFF;
    LPIT0->LPIT0_TVAL1_b.TMR_VAL = 0xFFFFFFFF;

    /* Start the timers */
    LPIT0->LPIT0_SETTEN_b.SET_T_EN_0 = LPIT0_SETTEN_SET_T_EN_0_1;
    LPIT0->LPIT0_SETTEN_b.SET_T_EN_1 = LPIT0_SETTEN_SET_T_EN_1_1;

    /* Verify that the least significant 32-bit timer is counting (not locked at initial value of 0xFFFFFFFF) */
    while (LPIT0->LPIT0_CVAL0 == 0xFFFFFFFF){};

    return;

}

uint64_t LPIT0_GetTimestamp(void)
{
    uint64_t monotonic_timestamp = (uint64_t)
    		(((uint64_t)(0xFFFFFFFF - LPIT0->LPIT0_CVAL1 ) << 32) | (0xFFFFFFFF - LPIT0->LPIT0_CVAL0));

    return monotonic_timestamp;
}

void LPIT0_Ch2_IRQ_Config(uint32_t irq_period_milis, uint8_t interrupt_priority, void (*callback)())
{
	// Select 32-bit periodic timer mode for channel 2
	LPIT0->LPIT0_TCTRL2_b.MODE = LPIT0_TCTRL2_MODE_0;

	// Set reload value, having a 80Mhz clock feeding the timer.
    LPIT0->LPIT0_TVAL2 = (irq_period_milis*80000) - 1u;

    // Enable interrupt in LPIT0 module
    LPIT0->LPIT0_MIER_b.TIE2 = LPIT0_MIER_TIE2_1;

    // Enable LPIT0 interrupt within NVIC and set the priority
    S32_NVIC_EnableIRQ(LPIT0_Ch2_IRQn);
    S32_NVIC_SetPriority(LPIT0_Ch2_IRQn, interrupt_priority);

    /* Start the timer channel */
    LPIT0->LPIT0_SETTEN_b.SET_T_EN_2 = LPIT0_SETTEN_SET_T_EN_2_1;

	// install callback
	LPIT0_Ch2_callback_ptr = callback;

	return;

}

#ifdef __cplusplus
extern "C" {
#endif


void LPIT0_Ch2_IRQHandler(void)
{
	// Clear flag of Ch2, (W1C) register
	LPIT0->LPIT0_MSR_b.TIF2 = LPIT0_MSR_TIF2_1;

	// Execute callback
	LPIT0_Ch2_callback_ptr();

	return;
}

#ifdef __cplusplus
}
#endif
