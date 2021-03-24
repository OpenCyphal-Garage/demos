/*
 * Copyright (c) 2020, NXP. All rights reserved.
 * Distributed under The MIT License.
 * Author: Abraham Rodriguez <abraham.rodriguez@nxp.com>
 */

#include "canfd.h"
#include "S32K146_bitfields.h"


/*!
* @brief Bit field declaration for the transmission and reception message buffers. See "Message Buffer Structure" in RM.
* 		 NOTE: Since this example manages CAN FD (8 bytes for header and 64 bytes for payload)
* 		 you have 7 Message Buffers available. The FIFO is not enabled.
*/
typedef struct
{
    struct
	{
      __IOM uint32_t TIMESTAMP  : 16;		/* Note that if you add from "TIMESTAMP" to "PRIO" */
      __IOM uint32_t DLC        : 4;			/* you get 64 bits = 8 bytes for header */
      __IOM uint32_t RTR        : 1;			/* 16 + 4 + 1 + ... + 3 = 64 */
      __IOM uint32_t IDE        : 1;
      __IOM uint32_t SRR        : 1;
             uint32_t            : 1;
      __IOM uint32_t CODE       : 4;
             uint32_t            : 1;
      __IOM uint32_t ESI        : 1;
      __IOM uint32_t BRS        : 1;
      __IOM uint32_t EDL        : 1;
      __IOM uint32_t EXT_ID     : 29;
      __IOM uint32_t PRIO       : 3;
      __IOM uint32_t payload[16];				/* 64 bytes (16 words) for payload */
    } FD_MessageBuffer[7];						/* 7 MBs available */
} CAN_MB_t;

/*!
* @brief Cast the structure above to the CAN0 memory area. You can go to the "S32K146_bitfields.h" file and see this memory area.
* 		 The file is inside the project's include folder.
* 		 CAN0_BASE + 0x80 is the specific address where the CAN0 MB structure is defined.
*/
#define CAN0_MB ((CAN_MB_t*)(CAN0_BASE + 0x80))

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

/*!
* @brief Structure for the CAN bit timings in nominal and data phases. See "Protocol Timing" in RM (FlexCAN Chapter)
*/
typedef struct
{
	uint8_t EPRESDIV;
    uint8_t EPROPSEG;
    uint8_t EPSEG1;
    uint8_t EPSEG2;
    uint8_t ERJW;
    uint8_t FPRESDIV;
    uint8_t FPROPSEG;
    uint8_t FPSEG1;
    uint8_t FPSEG2;
    uint8_t FRJW;
} FlexCAN_bit_timings_t;

// LUT for converting form DLC to byte length in bytes
const uint8_t FlexCANDLCToLength[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};

// LUT for converting form byte length to DLC in bytes
const uint8_t FlexCANLengthToDLC[65] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,                               // 0-8
    9,  9,  9,  9,                                                   // 9-12
    10, 10, 10, 10,                                                  // 13-16
    11, 11, 11, 11,                                                  // 17-20
    12, 12, 12, 12,                                                  // 21-24
    13, 13, 13, 13, 13, 13, 13, 13,                                  // 25-32
    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,  // 33-48
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,  // 49-64
};

FlexCAN_bit_timings_t CANFD_bitrate_profile_LUT[CANFD_bitrate_profile_NUM] =
{
	{
		.EPRESDIV = 0,
		.EPROPSEG = 0,
		.EPSEG1 = 0,
		.EPSEG2 = 0,
		.ERJW = 0,
		.FPRESDIV = 0,
		.FPROPSEG = 0,
		.FPSEG1 = 0,
		.FPSEG2 = 0,
		.FRJW = 0
	}
,
	{
		.EPRESDIV = 0,
		.EPROPSEG = 0,
		.EPSEG1 = 0,
		.EPSEG2 = 0,
		.ERJW = 0,
		.FPRESDIV = 0,
		.FPROPSEG = 0,
		.FPSEG1 = 0,
		.FPSEG2 = 0,
		.FRJW = 0
	}
,
	{
		.EPRESDIV = 0,
		.EPROPSEG = 0,
		.EPSEG1 = 0,
		.EPSEG2 = 0,
		.ERJW = 0,
		.FPRESDIV = 0,
		.FPROPSEG = 0,
		.FPSEG1 = 0,
		.FPSEG2 = 0,
		.FRJW = 0
	}
,
	{
		.EPRESDIV = 0,
		.EPROPSEG = 0,
		.EPSEG1 = 0,
		.EPSEG2 = 0,
		.ERJW = 0,
		.FPRESDIV = 0,
		.FPROPSEG = 0,
		.FPSEG1 = 0,
		.FPSEG2 = 0,
		.FRJW = 0
	}
,
	{
		.EPRESDIV = 0,
		.EPROPSEG = 0,
		.EPSEG1 = 0,
		.EPSEG2 = 0,
		.ERJW = 0,
		.FPRESDIV = 0,
		.FPROPSEG = 0,
		.FPSEG1 = 0,
		.FPSEG2 = 0,
		.FRJW = 0
	}
,
	{
		.EPRESDIV = 0,
		.EPROPSEG = 0,
		.EPSEG1 = 0,
		.EPSEG2 = 0,
		.ERJW = 0,
		.FPRESDIV = 0,
		.FPROPSEG = 0,
		.FPSEG1 = 0,
		.FPSEG2 = 0,
		.FRJW = 0
	}
,
	{
		.EPRESDIV = 0,
		.EPROPSEG = 0,
		.EPSEG1 = 0,
		.EPSEG2 = 0,
		.ERJW = 0,
		.FPRESDIV = 0,
		.FPROPSEG = 0,
		.FPSEG1 = 0,
		.FPSEG2 = 0,
		.FRJW = 0
	}
,
	{
		.EPRESDIV = 0,
		.EPROPSEG = 46,
		.EPSEG1 = 18,
		.EPSEG2 = 12,
		.ERJW = 12,
		.FPRESDIV = 0,
		.FPROPSEG = 7,
		.FPSEG1 = 6,
		.FPSEG2 = 4,
		.FRJW = 4
	}
};

static void (*FlexCAN0_reception_callback_ptr)(void) = 0;

static inline void S32_NVIC_EnableIRQ(IRQn_Type IRQn)
{
  S32_NVIC->ISER[(((uint32_t)(int32_t)IRQn) >> 5UL)] = (uint32_t)(1UL << (((uint32_t)(int32_t)IRQn) & 0x1FUL));

}

static inline void S32_NVIC_SetPriority(IRQn_Type IRQn, uint32_t priority)
{
    S32_NVIC->IP[((uint32_t)(int32_t)IRQn)] = (uint8_t)((priority << (8U - __NVIC_PRIO_BITS)) & (uint32_t)0xFFUL);
}

// Macro for swapping from little-endian to big- endian a 32-bit word
#if defined (__GNUC__) || defined (__ICCARM__) || defined (__ghs__) || defined (__ARMCC_VERSION)
#define S32_REV_BYTES(a, b) __asm volatile ("rev %0, %1" : "=r" (b) : "r" (a))
#endif

/** \brief  Enable interrupts
 */
#if defined (__GNUC__)
#define S32_ENABLE_INTERRUPTS() __asm volatile ("cpsie i" : : : "memory");
#else
#define S32_ENABLE_INTERRUPTS() __asm("cpsie i")
#endif


/** \brief  Disable interrupts
 */
#if defined (__GNUC__)
#define S32_DISABLE_INTERRUPTS() __asm volatile ("cpsid i" : : : "memory");
#else
#define S32_DISABLE_INTERRUPTS() __asm("cpsid i")
#endif

#define BIT_SRV_NOT_MSG(x) (((uint32_t)(x)) << 25)
#define BIT_R23(x)    	   (((uint32_t)(x)) << 23)
#define BIT_MSG_R7(x)      (((uint32_t)(x)) << 7)


status_t FlexCAN0_Init(CANFD_bitrate_profile_t profile, uint8_t irq_priority, void (*callback)())
{
	/* Look-up the timings profile */
	FlexCAN_bit_timings_t timings = CANFD_bitrate_profile_LUT[profile];

	/* FlexCAN0 clock gating */
    PCC->PCC_FlexCAN0_b.CGC   = PCC_PCC_FlexCAN0_CGC_1;

    /* Set Normal RUN clock mode for feeding SYS_CLK @ 80 MHz to FlexCAN */
    CAN0->CAN0_MCR_b.MDIS     = CAN0_MCR_MDIS_1;        	/* Disable FlexCAN module for clock source selection */
    CAN0->CAN0_CTRL1_b.CLKSRC = CAN0_CTRL1_CLKSRC_1;    	/* Select SYS_CLK as source (80 MHz) */
    CAN0->CAN0_MCR_b.MDIS     = CAN0_MCR_MDIS_0;        	/* Enable FlexCAN peripheral */
    CAN0->CAN0_MCR_b.HALT     = CAN0_MCR_HALT_1;        	/* Request freeze mode entry */
    CAN0->CAN0_MCR_b.FRZ      = CAN0_MCR_FRZ_1;			    /* Enter in freeze mode */

    /* Block for freeze mode entry */
    while(!(CAN0->CAN0_MCR_b.FRZACK));

    /* Enable local priority for transmission */
    CAN0->CAN0_MCR_b.LPRIOEN = CAN0_MCR_LPRIOEN_1;

    /* Enable CAN-FD feature in ISO 11898-1 compliance */
    CAN0->CAN0_MCR_b.FDEN = CAN0_MCR_FDEN_1;
    CAN0->CAN0_CTRL2_b.ISOCANFDEN = CAN0_CTRL2_ISOCANFDEN_1;

    /* CAN Bit Timing (CBT) configuration for a nominal phase of 1 Mbit/s with 80 time quantas, in accordance with Bosch 2012 specification, sample point at 83.75% */
    CAN0->CAN0_CBT_b.BTF 	  = CAN0_CBT_BTF_1;
    CAN0->CAN0_CBT_b.EPRESDIV = timings.EPRESDIV;
    CAN0->CAN0_CBT_b.EPROPSEG = timings.EPROPSEG;
    CAN0->CAN0_CBT_b.EPSEG1   = timings.EPSEG1;
    CAN0->CAN0_CBT_b.EPSEG2   = timings.EPSEG2;
    CAN0->CAN0_CBT_b.ERJW     = timings.ERJW;

    /* CAN-FD Bit Timing (FDCBT) for a data phase of 4 Mbit/s with 20 time quantas, in accordance with Bosch 2012 specification, sample point at 75% */
    CAN0->CAN0_FDCBT_b.FPRESDIV = timings.FPRESDIV;
    CAN0->CAN0_FDCBT_b.FPROPSEG = timings.FPROPSEG;
    CAN0->CAN0_FDCBT_b.FPSEG1   = timings.FPSEG1;
    CAN0->CAN0_FDCBT_b.FPSEG2   = timings.EPSEG2;
    CAN0->CAN0_FDCBT_b.FRJW     = timings.FRJW;

    CAN0->CAN0_FDCTRL_b.FDRATE = CAN0_FDCTRL_FDRATE_1;  	/* Enable bit rate switch in data phase of frame */
    CAN0->CAN0_FDCTRL_b.TDCEN  = CAN0_FDCTRL_TDCEN_1;   	/* Enable transceiver delay compensation */
    CAN0->CAN0_FDCTRL_b.TDCOFF = 5;                     	/* Setup 5 cycles for data phase sampling delay */
    CAN0->CAN0_FDCTRL_b.MBDSR0 = CAN0_FDCTRL_MBDSR0_11; 	/* Setup 64 bytes per message buffer (7 MB's) */

    CAN0->CAN0_MCR_b.SRXDIS = CAN0_MCR_SRXDIS_1; 			/* Disable self-reception of frames if ID matches */
    CAN0->CAN0_MCR_b.IRMQ   = CAN0_MCR_IRMQ_1;   			/* Enable individual message buffer ID masking */

    /* Exit from freeze mode */
    CAN0->CAN0_MCR_b.HALT = CAN0_MCR_HALT_0;
    CAN0->CAN0_MCR_b.FRZ  = CAN0_MCR_FRZ_0;

    /* Block for freeze mode exit */
    while(CAN0->CAN0_MCR_b.FRZACK){};

    /* Block for module ready flag */
    while(CAN0->CAN0_MCR_b.NOTRDY){};

	/* Enable and set interrupt priority for the reception ISR */
	S32_NVIC_EnableIRQ(CAN0_ORed_0_15_MB_IRQn );
	S32_NVIC_SetPriority(CAN0_ORed_0_15_MB_IRQn , irq_priority);

    /* Install ISR callback */
	FlexCAN0_reception_callback_ptr = callback;

    /* Success initialization */
    return SUCCESS;
}

/*!
* @brief Setup a message buffer for reception of a specific ID, 7 available MB
*
* @param [uint32_t id] filter
*
* @return Success If the filters were installed correctly
* @return Failure If the setup couldn't be performed
*/
status_t FlexCAN0_Install_ID(uint32_t id, uint8_t mb_index)
{
    /* Request freeze mode entry */
    CAN0->CAN0_MCR_b.HALT = CAN0_MCR_HALT_1;
    CAN0->CAN0_MCR_b.FRZ  = CAN0_MCR_FRZ_1;

    /* Block for freeze mode entry */
    while(!(CAN0->CAN0_MCR_b.FRZACK));

    /* Mask for receiving messages of the particular subject ID of UAVCAN, consult section 4.2.1 of spec */
    CAN0->CAN0_RXIMR[mb_index] = BIT_SRV_NOT_MSG(1) | BIT_R23(1) |
    		                         (0x1FFF << 8)  | BIT_MSG_R7(1);

    /* Configure reception message buffer. See "Message Buffer Structure" in RM */
    CAN0_MB->FD_MessageBuffer[mb_index].EDL =  1;			/* Extended data length */
    CAN0_MB->FD_MessageBuffer[mb_index].BRS =  1;			/* Bit-rate switch */
    CAN0_MB->FD_MessageBuffer[mb_index].ESI =  0;			/* N/A */
    CAN0_MB->FD_MessageBuffer[mb_index].CODE = 4;			/* When a frame is received successfully, this field is automatically updated to FULL */
    CAN0_MB->FD_MessageBuffer[mb_index].SRR =  0;			/* N/A */
    CAN0_MB->FD_MessageBuffer[mb_index].IDE =  1;			/* Extended ID */
    CAN0_MB->FD_MessageBuffer[mb_index].RTR =  0;			/* No remote request made */

    /* Configure the ID for receiving the message */
    CAN0_MB->FD_MessageBuffer[mb_index].EXT_ID = BIT_SRV_NOT_MSG(0) | BIT_R23(0) |
    											 (id << 8)          | BIT_MSG_R7(0);

    /* Enable interrupt for reception in the specific message buffer */
    CAN0->CAN0_IMASK1 |= (1u << mb_index);

    /* Exit from freeze mode */
    CAN0->CAN0_MCR_b.HALT = CAN0_MCR_HALT_0;
    CAN0->CAN0_MCR_b.FRZ  = CAN0_MCR_FRZ_0;

    /* Block for freeze mode exit */
    while(CAN0->CAN0_MCR_b.FRZACK);

    /* Block for module ready flag */
    while(CAN0->CAN0_MCR_b.NOTRDY);

    /* Success ID installation */
    return SUCCESS;
}
status_t FlexCAN0_Send(fdframe_t* frame)
{
	// Verify Inactive message buffer and Valid Priority Status flags
	if( !(CAN0->CAN0_ESR2_b.IMB && CAN0->CAN0_ESR2_b.VPS) )
	{
		return FAILURE; // there are no available message buffers, priority inversion may have occurred
	}

	// Get the lowest number index available message buffer
	uint8_t mb_index = CAN0->CAN0_ESR2_b.LPTM;

	// Leverage 32-bit native transfers
	uint32_t* native_FrameData = (uint32_t*)frame->PAYLOAD;

	/* get the frame's payload_length */
	uint32_t payloadLength = frame->PAYLOAD_SIZE_BYTES;

    /* Fill up the payload's bytes, including the ones that don't add up to a full word e.g. 1,2,3,5,6,7 byte data
     * length payloads */
    for (uint8_t i = 0; i < (payloadLength >> 2) + MIN(1, (payloadLength & 0x3)); i++)
    {
        /* FlexCAN natively transmits the bytes in big-endian order, in order to transmit little-endian for UAVCAN,
         * a byte swap is required */
    	S32_REV_BYTES(native_FrameData[i], CAN0_MB->FD_MessageBuffer[mb_index].payload[i]);
    }

    /* Set the frame's destination ID */
    CAN0_MB->FD_MessageBuffer[mb_index].EXT_ID = frame->EXTENDED_ID;

    /* Set the transmission priority, per the spec, are the bits [28-26] of the id field */
    CAN0_MB->FD_MessageBuffer[mb_index].PRIO = frame->EXTENDED_ID >> 26;

    /* Configure transmission message buffer. See "Message Buffer Structure" in RM */
    CAN0_MB->FD_MessageBuffer[mb_index].EDL =  1;			/* Extended data length */
    CAN0_MB->FD_MessageBuffer[mb_index].BRS =  1;			/* Bit-rate switch */
    CAN0_MB->FD_MessageBuffer[mb_index].ESI =  0;			/* N/A */
    CAN0_MB->FD_MessageBuffer[mb_index].SRR =  0;			/* N/A */
    CAN0_MB->FD_MessageBuffer[mb_index].IDE =  1;			/* Extended ID */
    CAN0_MB->FD_MessageBuffer[mb_index].RTR =  0;			/* No remote request made */
    CAN0_MB->FD_MessageBuffer[mb_index].DLC = 0xF;			/* 64 bytes of payload */
    CAN0_MB->FD_MessageBuffer[mb_index].CODE = 0xC; 	    /* After TX, the MB automatically returns to the INACTIVE state */

	return SUCCESS;
}



#ifdef __cplusplus
extern "C" {
#endif

void CAN0_ORed_0_15_MB_IRQHandler(void)
{
	// Perform the ISR atomically
	S32_DISABLE_INTERRUPTS()

	// Execute callback
	FlexCAN0_reception_callback_ptr();

	S32_ENABLE_INTERRUPTS()
}


#ifdef __cplusplus
}
#endif

