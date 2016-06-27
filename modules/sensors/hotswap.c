/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
 *   Copyright (C) 2015-2016  Henrique Silva <henrique.silva@lnls.br>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   @license GPL-3.0+ <http://spdx.org/licenses/GPL-3.0+>
 */

/**
 * @file hotswap.c
 * @author Henrique Silva <henrique.silva@lnls.br>, LNLS
 *
 * @brief Hotswap sensors implementation
 *
 * @ingroup HOTSWAP
 */

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "port.h"
#include "sdr.h"
#include "hotswap.h"
#include "task_priorities.h"
#include "ipmi.h"
#include "led.h"
#include "fru.h"
#include "utils.h"

static uint8_t hotswap_get_handle_status( void )
{
    return gpio_read_pin(HOT_SWAP_HANDLE_PORT, HOT_SWAP_HANDLE_PIN);
}

SDR_type_02h_t * hotswap_amc_pSDR;
sensor_t * hotswap_amc_sensor;

SDR_type_02h_t * hotswap_rtm_pSDR;
sensor_t * hotswap_rtm_sensor;

void hotswap_init( void )
{
    /* Create Hot Swap task */
    xTaskCreate( vTaskHotSwap, "Hot Swap", 150, (void *) NULL, tskHOTSWAP_PRIORITY, &vTaskHotSwap_Handle);

    SDR_type_02h_t * hotswap_pSDR;
    sensor_t * hotswap_sensor;

    /* Iterate through the SDR Table to find all the Hotswap entries */
    for ( hotswap_sensor = sdr_head; hotswap_sensor != NULL; hotswap_sensor = hotswap_sensor->next) {

        if ( hotswap_sensor->task_handle == NULL ) {
            continue;
        }

        /* Check if this task should update the selected SDR */
        if ( *(hotswap_sensor->task_handle) != vTaskHotSwap_Handle ) {
            continue;
        }

        hotswap_pSDR = (SDR_type_02h_t *) hotswap_sensor->sdr;

        if ( hotswap_pSDR->entityID == 0xC1 ) {
            hotswap_amc_sensor = hotswap_sensor;
            hotswap_amc_pSDR = hotswap_pSDR;
        } else if ( hotswap_pSDR->entityID == 0xC0 ) {
#ifdef MODULE_RTM
            hotswap_rtm_sensor = hotswap_sensor;
            hotswap_rtm_pSDR = hotswap_pSDR;
#endif
        }

    }
}

void vTaskHotSwap( void *Parameters )
{
    /* Init old_state with a different value, so that the uC always send its state on startup */
    static uint8_t old_state_amc = 0xFF;
    static uint8_t new_state_amc;
#ifdef MODULE_RTM
    static uint8_t old_state_rtm = 0xFF;
    static uint8_t new_state_rtm;
#endif

    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 50;

    /* Override Blue LED state so that if the handle is closed when the MMC is starting, the LED remains in the correct state */
    if ( gpio_read_pin(HOT_SWAP_HANDLE_PORT, HOT_SWAP_HANDLE_PIN) == 0 ) {
        LEDUpdate( FRU_AMC, LED_BLUE, LEDMODE_OVERRIDE, LEDINIT_OFF, 0, 0 );
    } else {
        LEDUpdate( FRU_AMC, LED_BLUE, LEDMODE_OVERRIDE, LEDINIT_ON, 0, 0 );
    }

    /* Initialise the xLastWakeTime variable with the current time. */
    xLastWakeTime = xTaskGetTickCount();

    for ( ;; ) {
        vTaskDelayUntil( &xLastWakeTime, xFrequency );

        new_state_amc = hotswap_get_handle_status();

        if ( new_state_amc ^ old_state_amc ) {
            if ( hotswap_send_event( hotswap_amc_sensor, new_state_amc ) == ipmb_error_success ) {
                hotswap_set_mask_bit( HOTSWAP_AMC, 1 << new_state_amc );
                hotswap_clear_mask_bit( HOTSWAP_AMC, 1 << (!new_state_amc) );
                old_state_amc = new_state_amc;
            }
        }

#ifdef MODULE_RTM
        new_state_rtm = rtm_get_hotswap_handle_status();

        if ( new_state_rtm ^ old_state_rtm ) {
            if ( hotswap_send_event( hotswap_rtm_sensor, new_state_rtm ) == ipmb_error_success ) {
                hotswap_set_mask_bit( HOTSWAP_RTM, 1 << new_state_rtm );
                hotswap_clear_mask_bit( HOTSWAP_RTM, 1 << (!new_state_rtm) );
                old_state_rtm = new_state_rtm;
            }
        }
#endif
    }
}

ipmb_error hotswap_send_event( sensor_t *sensor, uint8_t evt )
{
    return ipmi_event_send( sensor, ASSERTION_EVENT, &evt, sizeof( evt ) );
}

void hotswap_clear_mask_bit( uint8_t fru, uint8_t mask )
{
    if ( fru == HOTSWAP_AMC ) {
        hotswap_amc_sensor->readout_value &= ~mask;
    } else if ( fru == HOTSWAP_RTM ) {
        hotswap_rtm_sensor->readout_value &= ~mask;
    }
}

void hotswap_set_mask_bit( uint8_t fru, uint8_t mask )
{
    if ( fru == HOTSWAP_AMC ) {
        hotswap_amc_sensor->readout_value |= mask;
    } else if ( fru == HOTSWAP_RTM ) {
        hotswap_rtm_sensor->readout_value |= mask;
    }
}
