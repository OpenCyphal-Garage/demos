/*
 * Copyright (c) 2019, NXP. All rights reserved.
 * Distributed under the BSD-3-Clause-LBNL license, available in the file LICENSE.
 * Author: Abraham Rodriguez <abraham.rodriguez@nxp.com>
 */

/** @file
 * Driver for the media layer of Libuavcan v1 targeting
 * the NXP S32K14 family of automotive grade MCU's running
 * CAN-FD at 4Mbit/s data phase and 2Mbit/s in nominal phase.
 */

#ifndef S32K_LIBUAVCAN_HPP_INCLUDED
#define S32K_LIBUAVCAN_HPP_INCLUDED

/*
 * Integration Note, this driver utilizes the next modules.
 * LPIT: Channels 0,1 and 3
 * FlexCAN: All message buffers from each instance.
 *          ISR priority not set, thus, it is determined by its position in the vector.
 *
 * Sets the MCU clocking in Normal RUN mode with the next prescalers, 8Mhz external crystal is assumed:
 * CORE_CLK:  80Mhz
 * SYS_CLK:   80Mhz
 * BUS_CLK:   40Mhz
 * FLASH_CLK: 26.67Mhz
 *
 * Dividers:
 * SOSCDIV2 = 8
 *
 * LPIT source = SOSCDIV2 (1Mhz)
 * FlexCAN source = SYS_CLK (80Mhz)
 *
 * Asynchronous dividers not mentioned are left unset and SCG registers are locked
 *
 * Pin configuration: (Compatible with S32K14x EVB'S)
 * CAN0 RX: PTE4
 * CAN0 TX: PTE5
 * CAN1 RX: PTA12
 * CAN1 TX: PTA13
 * CAN2 RX: PTB12
 * CAN2 TX: PTB13
 * PTE10: CAN0 transceiver STB (UAVCAN node board only)
 * PTE11: CAN1 transceiver STB (UAVCAN node board only)
 *
 * S32K146 and S32K148 although having multiple CANFD instances
 * their evb's have only one transceiver, the other instances's
 * digital signals are set out to pin headers.
 */

/* Macro for additional configuration needed when using TJA1044 transceiver
 * used in NXP's UAVCAN node board, set to 0 when using other boards  */
#define UAVCAN_NODE_BOARD_USED 0

/* Include desired target S32K14x registers and features header files,
 * defaults to S32K146 from NXP's UAVCAN node board */
#include "S32K142.h"

/* libuavcan core header files */
#include "libuavcan/media/can.hpp"
#include "libuavcan/media/interfaces.hpp"
#include "libuavcan/platform/memory.hpp"

/* STL queue for the intermediate ISR buffer */
#include <deque>

/* Preprocessor conditionals for deducing the number of CANFD FlexCAN instances in target MCU */
#if defined(MCU_S32K142) || defined(MCU_S32K144)
#    define TARGET_S32K_CANFD_COUNT (1u)
#    define DISCARD_COUNT_ARRAY 0
#elif defined(MCU_S32K146)
#    define TARGET_S32K_CANFD_COUNT (2u)
#    define DISCARD_COUNT_ARRAY 0, 0
#elif defined(MCU_S32K148)
#    define TARGET_S32K_CANFD_COUNT (3u)
#    define DISCARD_COUNT_ARRAY 0, 0, 0
#else
#    error "No NXP S32K compatible MCU header file included"
#endif

namespace libuavcan
{
namespace media
{
/* Number of capable CANFD FlexCAN instances, defined in constructor, defaults to 0 */
constexpr static std::uint_fast8_t S32K_CANFD_Count = TARGET_S32K_CANFD_COUNT;

/* Frame capacity for the intermediate ISR buffer, each frame adds 80 bytes of required .bss memory */
constexpr static std::size_t S32K_Frame_Capacity = 40u;

/* Intermediate buffer for ISR reception with static memory pool for each instance */
std::deque<CAN::Frame<CAN::TypeFD::MaxFrameSizeBytes>,
           platform::memory::PoolAllocator<S32K_Frame_Capacity, sizeof(CAN::Frame<CAN::TypeFD::MaxFrameSizeBytes>)>>
    g_frame_ISRbuffer[S32K_CANFD_Count];

/* Counter for the number of discarded messages due to the RX buffer being full */
std::uint32_t g_S32K_discarded_frames_count[S32K_CANFD_Count] = {DISCARD_COUNT_ARRAY};

/* Number of filters supported by a single FlexCAN instance */
constexpr static std::uint8_t S32K_Filter_Count = 5u;

/* Lookup table for NVIC IRQ numbers for each FlexCAN instance */
constexpr static std::uint32_t S32K_FlexCAN_NVIC_Indices[][2u] = {{2u, 0x20000}, {2u, 0x1000000}, {2u, 0x80000000}};

/* Array of FlexCAN instances for dereferencing from */
constexpr static CAN_Type* FlexCAN[] = CAN_BASE_PTRS;

/* Lookup table for FlexCAN indices in PCC register */
constexpr static std::uint8_t PCC_FlexCAN_Index[] = {36u, 37u, 43u};

/**
 * Function for block polling a bit flag until its set with a timeout of 1 second using a LPIT timer
 *
 * @param flagRegister Register where the flag is located.
 * @param flagMask Mask to AND'nd with the register for isolating the flag.
 */
libuavcan::Result flagPollTimeout_Set(volatile std::uint32_t& flagRegister, std::uint32_t flag_Mask)
{
    constexpr std::uint32_t cycles_timeout = 0xFFFFF; /* Timeout of 1/(1Mhz) * 2^20 = 1.04 seconds approx */
    volatile std::uint32_t  delta          = 0;       /* Declaration of delta for comparision */

    /* Disable LPIT channel 3 for loading */
    LPIT0->CLRTEN |= LPIT_CLRTEN_CLR_T_EN_3(1);

    /* Load LPIT with its maximum value */
    LPIT0->TMR[3].TVAL = LPIT_TMR_CVAL_TMR_CUR_VAL_MASK;

    /* Enable LPIT channel 3 for timeout start */
    LPIT0->SETTEN |= LPIT_SETTEN_SET_T_EN_3(1);

    /* Start of timed block */
    while (delta < cycles_timeout)
    {
        /* Check if the flag has been set */
        if (flagRegister & flag_Mask)
        {
            return libuavcan::Result::Success;
        }

        /* Get current value of delta */
        delta = LPIT_TMR_CVAL_TMR_CUR_VAL_MASK - (LPIT0->TMR[3].CVAL);
    }

    /* If this section is reached, means timeout ocurred and return error status is returned */
    return libuavcan::Result::Failure;
}

/**
 * Function for block polling a bit flag until its cleared with a timeout of 1 second using a LPIT timer
 *
 * @param flagRegister Register where the flag is located.
 * @param flagMask Mask to AND'nd with the register for isolating the flag.
 */
libuavcan::Result flagPollTimeout_Clear(volatile std::uint32_t& flagRegister, std::uint32_t flag_Mask)
{
    constexpr std::uint32_t cycles_timeout = 0xFFFFF; /* Timeout of 1/(1Mhz) * 2^20 = 1.04 seconds approx */
    volatile std::uint32_t  delta          = 0;       /* Declaration of delta for comparision */

    /* Disable LPIT channel 3 for loading */
    LPIT0->CLRTEN |= LPIT_CLRTEN_CLR_T_EN_3(1);

    /* Load LPIT with its maximum value */
    LPIT0->TMR[3].TVAL = LPIT_TMR_CVAL_TMR_CUR_VAL_MASK;

    /* Enable LPIT channel 3 for timeout start */
    LPIT0->SETTEN |= LPIT_SETTEN_SET_T_EN_3(1);

    /* Start of timed block */
    while (delta < cycles_timeout)
    {
        /* Check if the flag has been set */
        if (!(flagRegister & flag_Mask))
        {
            return libuavcan::Result::Success;
        }

        /* Get current value of delta */
        delta = LPIT_TMR_CVAL_TMR_CUR_VAL_MASK - (LPIT0->TMR[3].CVAL);
    }

    /* If this section is reached, means timeout ocurred and return error status is returned */
    return libuavcan::Result::Failure;
}

/**
 * S32K CanFD driver layer InterfaceGroup
 * Class instantiation with the next template parameters:
 *
 * FrameT = Frame: MTUBytesParam = MaxFrameSizeBytes (64 bytes)
 *                 FlagBitsCompareMask = 0x00 (default)
 * MaxTxFrames = 1 (default)
 * MaxRxFrames = 1 (default)
 */
class S32K_InterfaceGroup : public InterfaceGroup<CAN::Frame<CAN::TypeFD::MaxFrameSizeBytes>>
{
public:
    /* Size in words (4 bytes) of the offset between message buffers */
    constexpr static std::uint8_t MB_Size_Words = 18u;

    /* Offset in words for reaching the payload of a message buffer */
    constexpr static std::uint8_t MB_Data_Offset = 2u;

    /**
     * Get the number of CAN-FD capable FlexCAN modules in current S32K14 MCU
     * @return 1-* depending of the target MCU.
     */
    virtual std::uint_fast8_t getInterfaceCount() const override { return S32K_CANFD_Count; }

    /**
     * Send a frame through a particular available FlexCAN instance
     * @param  interface_index  The index of the interface in the group to write the frames to.
     * @param  frames           1..MaxTxFrames frames to write into the system queues for immediate transmission.
     * @param  frames_len       The number of frames in the frames array that should be sent
     *                          (starting from frame 0).
     * @param  out_frames_written
     *                          Will return MaxTxFrames in current implementation if the frame was sent successfully
     * @return libuavcan::Result::Success if all frames were written.
     * @return libuavcan::Result::BadArgument if interface_index or frames_len are out of bound.
     */
    virtual libuavcan::Result write(std::uint_fast8_t interface_index,
                                    const FrameType (&frames)[TxFramesLen],
                                    std::size_t  frames_len,
                                    std::size_t& out_frames_written) override
    {
        /* Initialize return value status */
        libuavcan::Result Status = libuavcan::Result::Success;

        /* Input validation */
        if ((frames_len > TxFramesLen) || (interface_index > S32K_CANFD_Count))
        {
            Status = libuavcan::Result::BadArgument;
        }

        if (isSuccess(Status))
        {
            /* Read the CODE of the Control and Status word of the TX message buffers from the specified instance */
            std::uint32_t CODE_MB0 =
                ((FlexCAN[interface_index - 1]->RAMn[0 * MB_Size_Words] & CAN_RAMn_DATA_BYTE_0(0xF)) >>
                 CAN_RAMn_DATA_BYTE_0_SHIFT);

            std::uint32_t CODE_MB1 =
                ((FlexCAN[interface_index - 1]->RAMn[1 * MB_Size_Words] & CAN_RAMn_DATA_BYTE_0(0xF)) >>
                 CAN_RAMn_DATA_BYTE_0_SHIFT);

            /* Initialize flag used for MB semaphore */
            std::uint8_t flag = 0;

            /* Check if Tx Message buffer status CODE is inactive (0b1000) and transmit through MB0*/
            if ((0x8 == CODE_MB0) || !CODE_MB0)
            {
                /* Ensure interrupt flag for MB0 is cleared (write to clear register) */
                FlexCAN[interface_index - 1]->IFLAG1 |= CAN_IFLAG1_BUF0I_MASK;

                /* Get data length of the frame wished to be written */
                std::uint_fast8_t payloadLength = frames[0].getDataLength();

                /* Fill up payload from MSB to LSB in function of frame's dlc */
                for (std::uint8_t i = 0; i < (payloadLength >> 2); i++)
                {
                    /* Build up each 32 bit word with 4 indices from frame.data uint8_t array */
                    FlexCAN[interface_index - 1]->RAMn[0 * MB_Size_Words + MB_Data_Offset + i] =
                        (static_cast<std::uint32_t>(frames[0].data[(i << 2) + 0] << 24)) |
                        (static_cast<std::uint32_t>(frames[0].data[(i << 2) + 1] << 16)) |
                        (static_cast<std::uint32_t>(frames[0].data[(i << 2) + 2] << 8)) |
                        (frames[0].data[(i << 2) + 3] << 0);
                }

                /* Fill up payload of frame's bytes that dont fill upa 32-bit word,(0,1,2,3,5,6,7 byte data length)*/
                for (std::uint8_t i = 0; i < (payloadLength & 0x3); i++)
                {
                    FlexCAN[interface_index - 1]->RAMn[0 * MB_Size_Words + MB_Data_Offset + (payloadLength >> 2)] |=
                        static_cast<std::uint32_t>(frames[0].data[((payloadLength >> 2) << 2) + i] << ((3 - i) << 3));
                }

                /* Fill up frame ID */
                FlexCAN[interface_index - 1]->RAMn[0 * MB_Size_Words + 1] = frames[0].id & CAN_WMBn_ID_ID_MASK;

                /* Fill up word 0 of frame and transmit it
                 * Extended Data Length       (EDL) = 1
                 * Bit Rate Switch            (BRS) = 1
                 * Error State Indicator      (ESI) = 0
                 * Message Buffer Code       (CODE) = 12 ( Transmit data frame )
                 * Substitute Remote Request  (SRR) = 0
                 * ID Extended Bit            (IDE) = 1
                 * Remote Tx Request          (RTR) = 0
                 * Data Length Code           (DLC) = frame.getdlc()
                 * Counter Time Stamp  (TIME STAMP) = 0 ( Handled by hardware )
                 */
                FlexCAN[interface_index - 1]->RAMn[0 * MB_Size_Words + 0] =
                    CAN_RAMn_DATA_BYTE_1(0x20) | CAN_WMBn_CS_DLC(frames[0].getDLC()) | CAN_RAMn_DATA_BYTE_0(0xCC);

                /* Set the return status as successfull */
                Status = libuavcan::Result::Success;

                /* Argument assignment to 1 frame transmitted successfully */
                out_frames_written = 1;

                /* Ensure the interrupt flag is cleared after a successfull transmission */
                FlexCAN[interface_index - 1]->IFLAG1 |= CAN_IFLAG1_BUF0I_MASK;

                /* Turn on flag for not retransmitting on next MB*/
                flag = 1;
            }
            /* Transmit through MB1 if MB0 was busy */
            else if (((0x8 == CODE_MB1) || !CODE_MB1) && !flag)
            {
                /* Ensure interrupt flag for MB1 is cleared (write to clear register) */
                FlexCAN[interface_index - 1]->IFLAG1 |= CAN_IFLAG1_BUF4TO1I(1);

                /* Get data length of the frame wished to be written */
                std::uint_fast8_t payloadLength = frames[0].getDataLength();

                /* Fill up payload from MSB to LSB in function of frame's dlc */
                for (std::uint8_t i = 0; i < (payloadLength >> 2); i++)
                {
                    /* Build up each 32 bit word with 4 indices from frame.data uint8_t array */
                    FlexCAN[interface_index - 1]->RAMn[1 * MB_Size_Words + MB_Data_Offset + i] =
                        (static_cast<std::uint32_t>(frames[0].data[(i << 2) + 0] << 24)) |
                        (static_cast<std::uint32_t>(frames[0].data[(i << 2) + 1] << 16)) |
                        (static_cast<std::uint32_t>(frames[0].data[(i << 2) + 2] << 8)) |
                        (frames[0].data[(i << 2) + 3] << 0);
                }

                /* Fill up payload of frame's bytes that dont fill upa 32-bit word,(0,1,2,3,5,6,7 byte data length)*/
                for (std::uint8_t i = 0; i < (payloadLength & 0x3); i++)
                {
                    FlexCAN[interface_index - 1]->RAMn[1 * MB_Size_Words + MB_Data_Offset + (payloadLength >> 2)] |=
                        static_cast<std::uint32_t>(frames[0].data[((payloadLength >> 2) << 2) + i] << ((3 - i) << 3));
                }

                /* Fill up frame ID */
                FlexCAN[interface_index - 1]->RAMn[1 * MB_Size_Words + 1] = frames[0].id & CAN_WMBn_ID_ID_MASK;

                /* Fill up word 0 of frame and transmit it
                 * Extended Data Length       (EDL) = 1
                 * Bit Rate Switch            (BRS) = 1
                 * Error State Indicator      (ESI) = 0
                 * Message Buffer Code       (CODE) = 12 ( Transmit data frame )
                 * Substitute Remote Request  (SRR) = 0
                 * ID Extended Bit            (IDE) = 1
                 * Remote Tx Request          (RTR) = 0
                 * Data Length Code           (DLC) = frame.getdlc()
                 * Counter Time Stamp  (TIME STAMP) = 0  ( Handled by hardware )
                 */
                FlexCAN[interface_index - 1]->RAMn[1 * MB_Size_Words + 0] =
                    CAN_RAMn_DATA_BYTE_1(0x20) | CAN_WMBn_CS_DLC(frames[0].getDLC()) | CAN_RAMn_DATA_BYTE_0(0xCC);

                /* Set the return status as successful */
                Status = libuavcan::Result::Success;

                /* Argument assignment to 1 Frame transmitted successfully */
                out_frames_written = 1u;

                /* Ensure the interrupt flag is cleared after a successfull transmission */
                FlexCAN[interface_index - 1]->IFLAG1 |= CAN_IFLAG1_BUF4TO1I(1);
            }
        }

        /* Return status code */
        return Status;
    }

    /**
     * Read from an intermediate ISR Frame buffer of an FlexCAN instance.
     * @param  interface_index  The index of the interface in the group to read the frames from.
     * @param  out_frames       A buffer of frames to read.
     * @param  out_frames_read  On output the number of frames read into the out_frames array.
     * @return libuavcan::Result::Success If no errors occurred.
     * @return libuavcan::Result::BadArgument If interface_index is out of bound.
     */
    virtual libuavcan::Result read(std::uint_fast8_t interface_index,
                                   FrameType (&out_frames)[RxFramesLen],
                                   std::size_t& out_frames_read) override
    {
        /* Initialize return value and out_frames_read output reference value */
        libuavcan::Result Status = libuavcan::Result::Success;
        out_frames_read          = 0;

        /* Input validation */
        if (interface_index > S32K_CANFD_Count)
        {
            Status = libuavcan::Result::BadArgument;
        }

        if (isSuccess(Status))
        {
            /* Check if the ISR buffer isn't empty */
            if (!g_frame_ISRbuffer[interface_index - 1].empty())
            {
                /* Get the front element of the queue buffer */
                out_frames[0] = g_frame_ISRbuffer[interface_index - 1].front();

                /* Pop the front element of the queue buffer */
                g_frame_ISRbuffer[interface_index - 1].pop_front();

                /* Default minimal RX number of frames read */
                out_frames_read = RxFramesLen;

                /* If read is successful, status is success */
                Status = libuavcan::Result::Success;
            }
        }

        /* Return status code */
        return Status;
    }

    /**
     * Reconfigure reception filters for dynamic subscription of nodes
     * NOTE: Since filter Iindex to reconfigure isn't provided, only up
     *       to which filter to modify, the filters in the range
     *       (filter_config_length, S32K_Filter_Count ] are left unaltered.
     * @param  filter_config         The filtering to apply equally to all members of the group.
     * @param  filter_config_length  The length of the @p filter_config argument.
     * @return libuavcan::Result::Success if the group's receive filtering was successfully reconfigured.
     * @return libuavcan::Result::Failure if a register didn't get configured as desired.
     * @return libuavcan::Result::BadArgument if filter_config_length is out of bound.
     */
    virtual libuavcan::Result reconfigureFilters(const typename FrameType::Filter* filter_config,
                                                 std::size_t                       filter_config_length) override
    {
        /* Initialize return value status */
        libuavcan::Result Status = libuavcan::Result::Success;

        /* Input validation */
        if (filter_config_length > S32K_Filter_Count)
        {
            Status = libuavcan::Result::BadArgument;
        }

        if (isSuccess(Status))
        {
            for (std::uint8_t i = 0; i < S32K_CANFD_Count; i++)
            {
                /* Enter freeze mode for filter reconfiguration */
                FlexCAN[i]->MCR |= (CAN_MCR_HALT_MASK | CAN_MCR_FRZ_MASK);

                /* Block for freeze mode entry, halts any transmission or  */
                if (isSuccess(Status))
                {
                    Status = flagPollTimeout_Set(FlexCAN[i]->MCR, CAN_MCR_FRZACK_MASK);

                    for (std::uint8_t j = 0; j < filter_config_length; j++)
                    {
                        /* Setup reception MB's mask from input argument */
                        FlexCAN[i]->RXIMR[j + 2] = filter_config[j].mask;

                        /* Setup word 0 (4 Bytes) for ith MB
                         * Extended Data Length      (EDL) = 1
                         * Bit Rate Switch           (BRS) = 1
                         * Error State Indicator     (ESI) = 0
                         * Message Buffer Code      (CODE) = 4 ( Active for reception and empty )
                         * Substitute Remote Request (SRR) = 0
                         * ID Extended Bit           (IDE) = 1
                         * Remote Tx Request         (RTR) = 0
                         * Data Length Code          (DLC) = 0 ( Valid for transmission only )
                         * Counter Time Stamp (TIME STAMP) = 0 ( Handled by hardware )
                         */
                        FlexCAN[i]->RAMn[(j + 2) * MB_Size_Words] =
                            CAN_RAMn_DATA_BYTE_0(0xC4) | CAN_RAMn_DATA_BYTE_1(0x20);

                        /* Setup Message buffers 2-7 29-bit extended ID from parameter */
                        FlexCAN[i]->RAMn[(j + 2) * MB_Size_Words + 1] = filter_config[j].id;
                    }

                    /* Freeze mode exit request */
                    FlexCAN[i]->MCR &= ~(CAN_MCR_HALT_MASK | CAN_MCR_FRZ_MASK);

                    /* Block for freeze mode exit */
                    if (isSuccess(Status))
                    {
                        Status = flagPollTimeout_Clear(FlexCAN[i]->MCR, CAN_MCR_FRZACK_MASK);

                        /* Block until module is ready */
                        if (isSuccess(Status))
                        {
                            Status = flagPollTimeout_Clear(FlexCAN[i]->MCR, CAN_MCR_NOTRDY_MASK);
                        }
                    }
                }
            }
        }

        /* Return status code */
        return Status;
    }

    /**
     * Block with timeout for available Message buffers
     * @param [in]     timeout                  The amount of time to wait for and available message buffer.
     * @param [in]     ignore_write_available   If set to true, will check availability only for RX MB's
     *
     * @return  libuavcan::Result::SuccessTimeout if timeout occurred and no required MB's became available.
     *          libuavcan::Result::Success if an interface is ready for read, and if
     *          @p ignore_write_available is false, or write.
     */
    virtual libuavcan::Result select(libuavcan::duration::Monotonic timeout, bool ignore_write_available) override
    {
        /* Obtain timeout from object */
        std::uint32_t cycles_timeout = static_cast<std::uint32_t>(timeout.toMicrosecond());

        /* Initialization of delta variable for comparison */
        volatile std::uint32_t delta = 0;

        /* Initialize flag variable for MB reading mutex in funtion of ignore_write_available */
        std::uint32_t flag = 0;

        /* Disable LPIT channel 3 for loading */
        LPIT0->CLRTEN |= LPIT_CLRTEN_CLR_T_EN_3(1);

        /* Load LPIT with its maximum value */
        LPIT0->TMR[3].TVAL = LPIT_TMR_CVAL_TMR_CUR_VAL_MASK;

        /* Enable LPIT channel 3 for timeout start */
        LPIT0->SETTEN |= LPIT_SETTEN_SET_T_EN_3(1);

        /* Start of timed block */
        while (delta < cycles_timeout)
        {
            for (std::uint8_t i = 0; i < S32K_CANFD_Count; i++)
            {
                /* Get CODE from Control and Status word of each MB */
                std::uint32_t flagMB0 = (FlexCAN[i]->RAMn[0 * MB_Size_Words]) & CAN_RAMn_DATA_BYTE_0(0xF);
                std::uint32_t flagMB1 = (FlexCAN[i]->RAMn[1 * MB_Size_Words]) & CAN_RAMn_DATA_BYTE_0(0xF);
                std::uint32_t flagMB2 = (FlexCAN[i]->RAMn[2 * MB_Size_Words]) & CAN_RAMn_DATA_BYTE_0(0xF);
                std::uint32_t flagMB3 = (FlexCAN[i]->RAMn[3 * MB_Size_Words]) & CAN_RAMn_DATA_BYTE_0(0xF);
                std::uint32_t flagMB4 = (FlexCAN[i]->RAMn[4 * MB_Size_Words]) & CAN_RAMn_DATA_BYTE_0(0xF);
                std::uint32_t flagMB5 = (FlexCAN[i]->RAMn[5 * MB_Size_Words]) & CAN_RAMn_DATA_BYTE_0(0xF);
                std::uint32_t flagMB6 = (FlexCAN[i]->RAMn[6 * MB_Size_Words]) & CAN_RAMn_DATA_BYTE_0(0xF);

                /* Global unlock of message buffers by reading the module timer */
                (void) FlexCAN[i]->TIMER;

                /* If ignore = true, check only RX buffers (2th-6th) */
                if (ignore_write_available)
                {
                    /* Any CODE must not be BUSY (0b0001) */
                    flag = (1 != flagMB2) || (1 != flagMB3) || (1 != flagMB4) || (1 != flagMB5) || (1 != flagMB6);
                }

                /* All MB's CODE get checked for availability if ignore = true */
                else
                {
                    /* Check inactive message buffer IMB flag, checks CODE not 1 for Rx or 0b1000 for Tx */
                    flag = (8 == flagMB0) || (8 == flagMB1) || (1 != flagMB2) || (1 != flagMB3) || (1 != flagMB4) ||
                           (1 != flagMB5) || (1 != flagMB6);
                }

                if (flag)
                {
                    /* If timeout didn't ocurred, return success code */
                    return libuavcan::Result::Success;
                }
            }

            /* Get current value of delta */
            delta = LPIT_TMR_CVAL_TMR_CUR_VAL_MASK - (LPIT0->TMR[3].CVAL);
        }

        /* If this section is reached, means timeout ocurred and return timeout status */
        return libuavcan::Result::SuccessTimeout;
    }
};

/**
 * S32K CanFD driver layer InterfaceManager
 * Class instantiation with the next template parameters
 *
 * InterfaceGroupT = S32K_InterfaceGroup (previously instantiated class)
 * InterfaceGroupPtrT = S32K_InterfaceGroup* (raw pointer)
 */
class S32K_InterfaceManager : private InterfaceManager<S32K_InterfaceGroup, S32K_InterfaceGroup*>
{
public:
    /* S32K_InterfaceGroup type object member which address is returned from the next factory method */
    InterfaceGroupType S32K_InterfaceGroupObj;

    /**
     * Initializes the peripherals needed for libuavcan driver layer in current MCU
     * @param  filter_config         The filtering to apply equally to all FlexCAN instances.
     * @param  filter_config_length  The length of the @p filter_config argument.
     * @param  out_group             A pointer to set to the started group. This will be nullptr if the start method
     *                               fails.
     * @return libuavcan::Result::Success if the group was successfully started and a valid pointer was returned.
     * @return libuavcan::Result::Failure if the initialization fails at some point.
     *         The caller should assume that @p out_group is an invalid pointer if any failure is returned.
     * @return libuavcan::Result::BadArgument if filter_config_length is out of bound.
     */
    virtual libuavcan::Result startInterfaceGroup(const typename InterfaceGroupType::FrameType::Filter* filter_config,
                                                  std::size_t            filter_config_length,
                                                  InterfaceGroupPtrType& out_group) override
    {
        /* Initialize return values */
        libuavcan::Result Status = libuavcan::Result::Success;
        out_group                = nullptr;

        /* Input validation */
        if (filter_config_length > S32K_Filter_Count)
        {
            Status = libuavcan::Result::BadArgument;
        }

        /* SysClock initialization for feeding 80Mhz to FlexCAN */

        /* System Oscillator (SOSC) initialization for 8Mhz external crystal */
        SCG->SOSCCSR &= ~SCG_SOSCCSR_LK_MASK;     /* Ensure the register is unlocked */
        SCG->SOSCCSR &= ~SCG_SOSCCSR_SOSCEN_MASK; /* Disable SOSC for setup */
        SCG->SOSCCFG = SCG_SOSCCFG_EREFS_MASK |   /* Setup external crystal for SOSC reference */
                       SCG_SOSCCFG_RANGE(2);      /* Select 8Mhz range */
        SCG->SOSCDIV |= SCG_SOSCDIV_SOSCDIV2(4);  /* Divider of 8 for LPIT clock source, gets 1Mhz reference */
        SCG->SOSCCSR = SCG_SOSCCSR_SOSCEN_MASK;   /* Enable SOSC reference */
        SCG->SOSCCSR |= SCG_SOSCCSR_LK_MASK;      /* Lock the register from accidental writes */

        /* Poll for valid SOSC reference, needs 4096 cycles */
        while (!(SCG->SOSCCSR & SCG_SOSCCSR_SOSCVLD_MASK))
        {
        };

        /* System PLL (SPLL) initialization for to 160Mhz reference */
        SCG->SPLLCSR &= ~SCG_SPLLCSR_LK_MASK;     /* Ensure the register is unlocked */
        SCG->SPLLCSR &= ~SCG_SPLLCSR_SPLLEN_MASK; /* Disable PLL for setup */
        SCG->SPLLCFG = SCG_SPLLCFG_MULT(24);      /* Select multiply factor of 40 for 160Mhz SPLL_CLK */
        SCG->SPLLCSR |= SCG_SPLLCSR_SPLLEN_MASK;  /* Enable PLL */
        SCG->SPLLCSR |= SCG_SPLLCSR_LK_MASK;      /* Lock register from accidental writes */

        /* Poll for valid SPLL reference */
        while (!(SCG->SPLLCSR & SCG_SPLLCSR_SPLLVLD_MASK))
        {
        };

        /* Normal RUN configuration for output clocks */
        SCG->RCCR = SCG_RCCR_SCS(6) |     /* Select SPLL as system clock source */
                    SCG_RCCR_DIVCORE(1) | /* Additional dividers for Normal Run mode */
                    SCG_RCCR_DIVBUS(1) | SCG_RCCR_DIVSLOW(2);

        /* CAN frames timestamping 64-bit timer initialization using chained LPIT channel 0 and 1 */

        /* Clock source option 1: (SOSCDIV2) at 1Mhz clearing previous bit configuration */
        PCC->PCCn[PCC_LPIT_INDEX] |= PCC_PCCn_PCS(1);
        PCC->PCCn[PCC_LPIT_INDEX] |= PCC_PCCn_CGC(1); /* Clock gating to LPIT module */

        /* Enable module */
        LPIT0->MCR |= LPIT_MCR_M_CEN(1);

        /* Select 32-bit periodic Timer for both chained channels and timeouts timer (default)  */
        LPIT0->TMR[0].TCTRL |= LPIT_TMR_TCTRL_MODE(0);
        LPIT0->TMR[1].TCTRL |= LPIT_TMR_TCTRL_MODE(0);
        LPIT0->TMR[3].TCTRL |= LPIT_TMR_TCTRL_MODE(0);

        /* Select chain mode for channel 1, this becomes the most significant 32 bits */
        LPIT0->TMR[1].TCTRL |= LPIT_TMR_TCTRL_CHAIN(1);

        /* Setup max reload value for both channels 0xFFFFFFFF */
        LPIT0->TMR[0].TVAL = LPIT_TMR_TVAL_TMR_VAL_MASK;
        LPIT0->TMR[1].TVAL = LPIT_TMR_TVAL_TMR_VAL_MASK;

        /* Start the timers */
        LPIT0->SETTEN |= LPIT_SETTEN_SET_T_EN_0(1) | LPIT_SETTEN_SET_T_EN_1(1);

        /* Verify that the least significant 32-bit timer is counting (not locked at 0) */
        while (!(LPIT0->TMR[0].CVAL & LPIT_TMR_CVAL_TMR_CUR_VAL_MASK))
        {
        };

        /* FlexCAN instances initialization */
        for (std::uint8_t i = 0; i < S32K_CANFD_Count; i++)
        {
            PCC->PCCn[PCC_FlexCAN_Index[i]] = PCC_PCCn_CGC_MASK; /* FlexCAN0 clock gating */
            FlexCAN[i]->MCR |= CAN_MCR_MDIS_MASK;       /* Disable FlexCAN0 module for clock source selection */
            FlexCAN[i]->CTRL1 |= CAN_CTRL1_CLKSRC_MASK; /* Select SYS_CLK as source (80Mhz)*/
            FlexCAN[i]->MCR &= ~CAN_MCR_MDIS_MASK;      /* Enable FlexCAN and automatic transition to freeze mode*/

            /* Block for freeze mode entry */
            while (!(FlexCAN[i]->MCR & CAN_MCR_FRZACK_MASK))
            {
            };

            /* Next configurations are only permitted in freeze mode */
            FlexCAN[i]->MCR |= CAN_MCR_FDEN_MASK |          /* Habilitate CANFD feature */
                               CAN_MCR_FRZ_MASK;            /* Enable freeze mode entry when HALT bit is asserted */
            FlexCAN[i]->CTRL2 |= CAN_CTRL2_ISOCANFDEN_MASK; /* Activate the use of ISO 11898-1 CAN-FD standard */

            /* CAN Bit Timing (CBT) configuration for a nominal phase of 1 Mbit/s with 80 time quantas,
               in accordance with Bosch 2012 specification, sample point at 83.75% */
            FlexCAN[i]->CBT |= CAN_CBT_BTF_MASK |     /* Enable extended bit timing configurations for CAN-FD
                                                                 for setting up separetely nominal and data phase */
                               CAN_CBT_EPRESDIV(0) |  /* Prescaler divisor factor of 1 */
                               CAN_CBT_EPROPSEG(46) | /* Propagation segment of 47 time quantas */
                               CAN_CBT_EPSEG1(18) |   /* Phase buffer segment 1 of 19 time quantas */
                               CAN_CBT_EPSEG2(12) |   /* Phase buffer segment 2 of 13 time quantas */
                               CAN_CBT_ERJW(12);      /* Resynchronization jump width same as PSEG2 */

            /* CAN-FD Bit Timing (FDCBT) for a data phase of 4 Mbit/s with 20 time quantas,
               in accordance with Bosch 2012 specification, sample point at 75% */
            FlexCAN[i]->FDCBT |= CAN_FDCBT_FPRESDIV(0) | /* Prescaler divisor factor of 1 */
                                 CAN_FDCBT_FPROPSEG(7) | /* Propagation semgment of 7 time quantas
                                                            (only register that doesn't add 1) */
                                 CAN_FDCBT_FPSEG1(6) |   /* Phase buffer segment 1 of 7 time quantas */
                                 CAN_FDCBT_FPSEG2(4) |   /* Phase buffer segment 2 of 5 time quantas */
                                 CAN_FDCBT_FRJW(4);      /* Resynchorinzation jump width same as PSEG2 */

            /* Additional CAN-FD configurations */
            FlexCAN[i]->FDCTRL |= CAN_FDCTRL_FDRATE_MASK | /* Enable bit rate switch in data phase of frame */
                                  CAN_FDCTRL_TDCEN_MASK |  /* Enable transceiver delay compensation */
                                  CAN_FDCTRL_TDCOFF(5) |   /* Setup 5 cycles for data phase sampling delay */
                                  CAN_FDCTRL_MBDSR0(3);    /* Setup 64 bytes per message buffer (7 MB's) */

            /* Message buffers are located in a dedicated RAM inside FlexCAN, they aren't affected by reset,
             * so they must be explicitly initialized, they total 128 slots of 4 words each, which sum
             * to 512 bytes, each MB is 72 byte in size ( 64 payload and 8 for headers )
             */
            for (std::uint8_t j = 0; j < CAN_RAMn_COUNT; j++)
            {
                FlexCAN[i]->RAMn[j] = 0;
            }

            /* Clear the reception masks before configuring the ones needed */
            for (std::uint8_t j = 0; j < CAN_RXIMR_COUNT; j++)
            {
                FlexCAN[i]->RXIMR[j] = 0;
            }

            /* Setup maximum number of message buffers as 7, 0th and 1st for transmission and 2nd-6th for RX */
            FlexCAN[i]->MCR &= ~CAN_MCR_MAXMB_MASK; /* Clear previous configuracion of MAXMB, default is 0xF */
            FlexCAN[i]->MCR |= CAN_MCR_MAXMB(6) |
                               CAN_MCR_SRXDIS_MASK | /* Disable self-reception of frames if ID matches */
                               CAN_MCR_IRMQ_MASK;    /* Enable individual message buffer masking */

            /* Setup Message buffers 2nd-6th for reception and set filters */
            for (std::uint8_t j = 0; j < filter_config_length; j++)
            {
                /* Setup reception MB's mask from input argument */
                FlexCAN[i]->RXIMR[j + 2] = filter_config[j].mask;

                /* Setup word 0 (4 Bytes) for ith MB
                 * Extended Data Length      (EDL) = 1
                 * Bit Rate Switch           (BRS) = 1
                 * Error State Indicator     (ESI) = 0
                 * Message Buffer Code      (CODE) = 4 ( Active for reception and empty )
                 * Substitute Remote Request (SRR) = 0
                 * ID Extended Bit           (IDE) = 1
                 * Remote Tx Request         (RTR) = 0
                 * Data Length Code          (DLC) = 0 ( Valid for transmission only )
                 * Counter Time Stamp (TIME STAMP) = 0 ( Handled by hardware )
                 */
                FlexCAN[i]->RAMn[(j + 2) * S32K_InterfaceGroup::MB_Size_Words] =
                    CAN_RAMn_DATA_BYTE_0(0xC4) | CAN_RAMn_DATA_BYTE_1(0x20);

                /* Setup Message buffers 2-7 29-bit extended ID from parameter */
                FlexCAN[i]->RAMn[(j + 2) * S32K_InterfaceGroup::MB_Size_Words + 1] = filter_config[j].id;
            }

            /* Enable interrupt in NVIC for FlexCAN reception with default priority (ID = 81) */
            S32_NVIC->ISER[S32K_FlexCAN_NVIC_Indices[i][0]] = S32K_FlexCAN_NVIC_Indices[i][1];

            /* Enable interrupts of reception MB's (0b1111100) */
            FlexCAN[i]->IMASK1 = CAN_IMASK1_BUF31TO0M(124);

            /* Exit from freeze mode */
            FlexCAN[i]->MCR &= ~(CAN_MCR_HALT_MASK | CAN_MCR_FRZ_MASK);

            /* Block for freeze mode exit */
            while (FlexCAN[i]->MCR & CAN_MCR_FRZACK_MASK)
            {
            };

            /* Block for module ready flag */
            while (FlexCAN[i]->MCR & CAN_MCR_NOTRDY_MASK)
            {
            };
        }

        /* Port initialization */
        PCC->PCCn[PCC_PORTE_INDEX] |= PCC_PCCn_CGC_MASK; /* Clock gating to PORT E */
        PORTE->PCR[4] |= PORT_PCR_MUX(5);                /* CAN0_RX at PORT E pin 4 */
        PORTE->PCR[5] |= PORT_PCR_MUX(5);                /* CAN0_TX at PORT E pin 5 */

#if defined(MCU_S32K146) || defined(MCU_S32K148)
        PCC->PCCn[PCC_PORTA_INDEX] |= PCC_PCCn_CGC_MASK; /* Clock gating to PORT A */
        PORTA->PCR[12] |= PORT_PCR_MUX(3);               /* CAN1_RX at PORT A pin 12 */
        PORTA->PCR[13] |= PORT_PCR_MUX(3);               /* CAN1_TX at PORT A pin 13 */

        /* Set to LOW the standby (STB) pin in both transceivers of the UAVCAN node board */
        if (UAVCAN_NODE_BOARD_USED)
        {
            PORTE->PCR[11] |= PORT_PCR_MUX(1); /* MUX to GPIO */
            PTE->PDDR |= 1 << 11;              /* Set direction as output */
            PTE->PCOR |= 1 << 11;              /* Set the pin LOW */

            PORTE->PCR[10] |= PORT_PCR_MUX(1);
            PTE->PDDR |= 1 << 10;
            PTE->PCOR |= 1 << 10;
        }

#endif

#if defined(MCU_S32K148)
        PCC->PCCn[PCC_PORTB_INDEX] |= PCC_PCCn_CGC_MASK; /* Clock gating to PORT B */
        PORTB->PCR[12] |= PORT_PCR_MUX(4);               /* CAN2_RX at PORT B pin 12 */
        PORTB->PCR[13] |= PORT_PCR_MUX(4);               /* CAN2_TX at PORT B pin 13 */
#endif

        /* If function ended successfully, return address of object member of type S32K_InterfaceGroup */
        out_group = &S32K_InterfaceGroupObj;

        /* Return code for start of S32K_InterfaceGroup */
        return Status;
    }

    /**
     * Deinitializes the peripherals needed for the current libuavcan driver layer.
     * @param inout_group Pointer that will be set to null
     * @return libuavcan::Result::Success. If the used peripherals were deinitialized properly.
     */
    virtual libuavcan::Result stopInterfaceGroup(InterfaceGroupPtrType& inout_group) override
    {
        /* Initialize return value status */
        libuavcan::Result Status = libuavcan::Result::Success;

        /* FlexCAN module deinitialization */
        for (std::uint8_t i = 0; i < S32K_CANFD_Count; i++)
        {
            /* Disable FlexCAN module */
            FlexCAN[i]->MCR |= CAN_MCR_MDIS_MASK;

            if (isSuccess(Status))
            {
                /* Poll for Low Power ACK, waits for current transmission/reception to finish */
                Status = flagPollTimeout_Set(FlexCAN[i]->MCR, CAN_MCR_LPMACK_MASK);

                if (isSuccess(Status))
                {
                    /* Disable FlexCAN clock gating */
                    PCC->PCCn[PCC_FlexCAN_Index[i]] &= ~PCC_PCCn_CGC_MASK;
                }
            }
        }

        /* Assign to null the pointer output argument */
        inout_group = nullptr;

        /* Return status code of successful stop of S32K_InterfaceGroup */
        return Status;
    }

    /**
     * Return the number of filters that the current UAVCAN node can support
     * @return The maximum number of frame filters available for filter groups managed by this object,
     *         i.e. the number of combinations of ID and mask that each FlexCAN instance supports
     */
    virtual std::size_t getMaxFrameFilters() const override { return S32K_Filter_Count; }

    /* FlexCAN ISR for frame reception */
    static void S32K_libuavcan_ISR(std::uint8_t instance)
    {
        /* Before anything, get a timestamp  */
        std::uint64_t LPIT_timestamp_ISR = static_cast<std::uint64_t>(
            (static_cast<std::uint64_t>(0xFFFFFFFF - LPIT0->TMR[1].CVAL) << 32) | (0xFFFFFFFF - LPIT0->TMR[0].CVAL));

        time::Monotonic timestamp_ISR = time::Monotonic::fromMicrosecond(LPIT_timestamp_ISR);

        /* Initialize variable for finding which MB received */
        std::uint8_t MB_index = 0;

        /* Check which RX MB caused the interrupt (0b1111100) mask for 2nd-6th MB */
        switch (FlexCAN[instance]->IFLAG1 & 124)
        {
        case 0x4:
            MB_index = 2;
            break;
        case 0x8:
            MB_index = 3;
            break;
        case 0x10:
            MB_index = 4;
            break;
        case 0x20:
            MB_index = 5;
            break;
        case 0x40:
            MB_index = 6;
            break;
        }

        if (MB_index)
        {
            /* Receive a frame only if the buffer its under its capacity */
            if (g_frame_ISRbuffer[instance].size() <= S32K_Frame_Capacity)
            {
                /* Parse the Message buffer, read of the control and status word locks the MB */

                /* Get the raw DLC from the message buffer that received a frame */
                std::uint32_t dlc_ISR_raw =
                    ((FlexCAN[instance]->RAMn[MB_index * S32K_InterfaceGroup::MB_Size_Words + 0]) &
                     CAN_WMBn_CS_DLC_MASK) >>
                    CAN_WMBn_CS_DLC_SHIFT;

                /* Create CAN::FrameDLC type variable from the raw dlc */
                CAN::FrameDLC dlc_ISR = CAN::FrameDLC(dlc_ISR_raw);

                /* Convert from dlc to data length in bytes */
                std::uint8_t payloadLength_ISR = S32K_InterfaceGroup::FrameType::dlcToLength(dlc_ISR);

                /* Get the id */
                std::uint32_t id_ISR =
                    (FlexCAN[instance]->RAMn[MB_index * S32K_InterfaceGroup::MB_Size_Words + 1]) & CAN_WMBn_ID_ID_MASK;

                /* Array for parsing from native uint32_t to uint8_t */
                std::uint8_t data_ISR_byte[payloadLength_ISR];

                /* Parse the full words of the MB in bytes */
                for (std::uint8_t i = 0; i < payloadLength_ISR; i++)
                {
                    data_ISR_byte[i] = (FlexCAN[instance]->RAMn[MB_index * S32K_InterfaceGroup::MB_Size_Words +
                                                                S32K_InterfaceGroup::MB_Data_Offset + (i >> 2)] &
                                        (0xFF << ((3 - (i & 0x3)) << 3))) >>
                                       ((3 - (i & 0x3)) << 3);
                }

                /* Parse remaining bytes that don't complete up to a word if there are */
                for (std::uint8_t i = 0; i < (payloadLength_ISR & 0x3); i++)
                {
                    data_ISR_byte[payloadLength_ISR - (payloadLength_ISR & 0x3) + i] =
                        (FlexCAN[instance]->RAMn[MB_index * S32K_InterfaceGroup::MB_Data_Offset +
                                                 S32K_InterfaceGroup::MB_Data_Offset + (payloadLength_ISR >> 2)] &
                         (0xFF << ((3 - i) << 3))) >>
                        ((3 - i) << 3);
                }

                /* Create Frame object with constructor */
                CAN::Frame<CAN::TypeFD::MaxFrameSizeBytes> FrameISR(id_ISR, data_ISR_byte, dlc_ISR, timestamp_ISR);

                /* Insert the frame into the queue */
                g_frame_ISRbuffer[instance].push_back(FrameISR);
            }
            else
            {
                /* Increment the number of discarded frames due to full RX dequeue */
                g_S32K_discarded_frames_count[instance]++;
            }

            /* Unlock the MB by reading the timer register */
            (void) FlexCAN[instance]->TIMER;

            /* Clear MB interrupt flag (write 1 to clear)*/
            FlexCAN[instance]->IFLAG1 |= (1 << MB_index);
        }
    }
};

} /* END namespace media */
} /* END namespace libuavcan */

extern "C"
{
    /* ISR for FlexCAN0 successful reception */
    void CAN0_ORed_0_15_MB_IRQHandler()
    {
        /* Callback the static RX Interrupt Service Routine */
        libuavcan::media::S32K_InterfaceManager::S32K_libuavcan_ISR(0u);
    }

#if defined(MCU_S32K146) || defined(MCU_S32K148)
    /* ISR for FlexCAN1 successful reception) */
    void CAN1_ORed_0_15_MB_IRQHandler()
    {
        /* Callback the static RX Interrupt Service Routine */
        libuavcan::media::S32K_InterfaceManager::S32K_libuavcan_ISR(1u);
    }
#endif

#if defined(MCU_S32K148)
    /* ISR for FlexCAN2 successful reception */
    void CAN2_ORed_0_15_MB_IRQHandler()
    {
        /* Callback the static RX Interrupt Service Routine */
        libuavcan::media::S32K_InterfaceManager::S32K_libuavcan_ISR(2u);
    }
#endif
}

#endif
