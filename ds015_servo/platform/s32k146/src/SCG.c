/*
 * Copyright (c) 2020, NXP. All rights reserved.
 * Distributed under The MIT License.
 * Author: Abraham Rodriguez <abraham.rodriguez@nxp.com>
 */

#include "SCG.h"
#include "S32K146_bitfields.h"


void SCG_SOSC_8MHz_Init(void)
{
    /* System Oscillator (SOSC) initialization for 8 MHz external crystal */
    SCG->SCG_SOSCCSR_b.LK       = SCG_SOSCCSR_LK_0;         /* Ensure the register is unlocked */
    SCG->SCG_SOSCCSR_b.SOSCEN   = SCG_SOSCCSR_SOSCEN_0;     /* Disable SOSC for setup */
    SCG->SCG_SOSCCFG_b.EREFS    = SCG_SOSCCFG_EREFS_1;      /* Setup external crystal for SOSC reference */
    SCG->SCG_SOSCCFG_b.RANGE    = SCG_SOSCCFG_RANGE_10;     /* Select 8 MHz range */
    SCG->SCG_SOSCCSR_b.SOSCEN   = SCG_SOSCCSR_SOSCEN_1;     /* Enable SOSC reference */
    SCG->SCG_SOSCDIV_b.SOSCDIV2 = SCG_SOSCDIV_SOSCDIV2_001; /* Asynchronous source for FlexCAN */
    SCG->SCG_SOSCCSR_b.LK       = SCG_SOSCCSR_LK_1;         /* Lock the register from accidental writes */

    /* Poll for valid SOSC reference, needs 4096 cycles */
    while(!(SCG->SCG_SOSCCSR_b.SOSCVLD));

    return;
}

void SCG_SPLL_160MHz_Init(void)
{
    /* System PLL (SPLL) initialization for to 160 MHz reference */
    SCG->SCG_SPLLCSR_b.LK       = SCG_SPLLCSR_LK_0;         /* Ensure the register is unlocked */
    SCG->SCG_SPLLCSR_b.SPLLEN   = SCG_SPLLCSR_SPLLEN_0;     /* Disable PLL for setup */
    SCG->SCG_SPLLCFG_b.MULT     = 24;                       /* Select multiply factor of 40 for 160 MHz SPLL_CLK */
    SCG->SCG_SPLLDIV_b.SPLLDIV2 = SCG_SPLLDIV_SPLLDIV2_011; /* Divide by 4 for 160 MHz at SPLLDIV2 output for LPIT */
    SCG->SCG_SPLLCSR_b.SPLLEN   = SCG_SPLLCSR_SPLLEN_1;     /* Enable PLL */
    SCG->SCG_SPLLCSR_b.LK       = SCG_SPLLCSR_LK_1;         /* Lock register */

    /* Poll for valid SPLL reference */
    while(!(SCG->SCG_SPLLCSR_b.SPLLVLD));

    return;
}

void SCG_Normal_RUN_Init(void)
{
    /* Normal RUN configuration for output clocks, this register requires 32-bit writes */
    SCG->SCG_RCCR = SCG_RCCR_SCS_0110 | SCG_RCCR_DIVCORE_0001 | SCG_RCCR_DIVBUS_0001 | SCG_RCCR_DIVSLOW_0010;

    return;
}
