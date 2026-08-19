/*
 *    Copyright (C) 2026
 *    si468x_internal.h - Internal Opcodes and Declarations for libsi468x
 */

#ifndef __SI468X_INTERNAL_H__
#define __SI468X_INTERNAL_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Common Commands (All Modes) */
#define SI468X_CMD_RD_REPLY                  0x00
#define SI468X_CMD_POWER_UP                  0x01
#define SI468X_CMD_HOST_LOAD                 0x04
#define SI468X_CMD_LOAD_INIT                 0x06
#define SI468X_CMD_BOOT                      0x07
#define SI468X_CMD_GET_SYS_STATE             0x09
#define SI468X_CMD_GET_SYS_REVISIONS         0x10
#define SI468X_CMD_GET_EVENT_STATUS          0x12
#define SI468X_CMD_SET_PROPERTY              0x13
#define SI468X_CMD_GET_PROPERTY              0x14
#define SI468X_CMD_GET_AGC_STATUS            0x17

/* FM/FMHD Commands */
#define SI468X_CMD_FM_TUNE_FREQ              0x30
#define SI468X_CMD_FM_SEEK_START             0x31
#define SI468X_CMD_FM_RSQ_STATUS             0x32
#define SI468X_CMD_FM_RDS_STATUS             0x34
#define SI468X_CMD_HD_GET_STATION_INFO       0x94
#define SI468X_CMD_HD_GET_PSD_DECODE         0x95
#define SI468X_CMD_HD_GET_ALERT_MSG          0x96
#define SI468X_CMD_HD_TEST_GET_BER_INFO     0x98

/* Digital Service Commands */
#define SI468X_CMD_GET_DIGITAL_SERVICE_LIST  0x80
#define SI468X_CMD_START_DIGITAL_SERVICE     0x81
#define SI468X_CMD_STOP_DIGITAL_SERVICE      0x82
#define SI468X_CMD_GET_DIGITAL_SERVICE_DATA  0x84

/* DAB Commands */
#define SI468X_CMD_DAB_TUNE_FREQ             0xB0
#define SI468X_CMD_DAB_DIGRAD_STATUS         0xB2
#define SI468X_CMD_DAB_GET_EVENT_STATUS      0xB3
#define SI468X_CMD_DAB_SET_FREQ_LIST         0xB8
#define SI468X_CMD_DAB_GET_COMPONENT_INFO    0xBB
#define SI468X_CMD_DAB_GET_TIME              0xBC
#define SI468X_CMD_DAB_GET_AUDIO_INFO        0xBD
#define SI468X_CMD_DAB_GET_SUBCHAN_INFO      0xBE
#define SI468X_CMD_DAB_GET_FREQ_INFO         0xBF
#define SI468X_CMD_DAB_GET_SERVICE_INFO      0xC0
#define SI468X_CMD_DAB_GET_OE_SERVICES_INFO  0xC1
#define SI468X_CMD_DAB_TEST_GET_BER_INFO    0xE8

/* Property IDs */
#define SI468X_PROP_DIGITAL_IO_OUTPUT_SELECT 0x0200
#define SI468X_PROP_DIGITAL_IO_OUTPUT_FORMAT 0x0202
#define SI468X_PROP_AUDIO_ANALOG_VOLUME      0x0300
#define SI468X_PROP_PIN_CONFIG_ENABLE        0x0800
#define SI468X_PROP_FM_AUDIO_DE_EMPHASIS     0x3900
#define SI468X_PROP_FM_RDS_INTERRUPT_SOURCE  0x3C00
#define SI468X_PROP_FM_RDS_INT_FIFO_COUNT    0x3C01
#define SI468X_PROP_FM_RDS_CONFIG            0x3C02
#define SI468X_PROP_DAB_XPAD_ENABLE          0xB400

/* On-Chip PAD/XPAD Decoder properties */
#define SI468X_PROP_DAB_VALID_RSSI_TIME      0xB200
#define SI468X_PROP_DAB_VALID_RSSI_THRESHOLD 0xB201
#define SI468X_PROP_DAB_VALID_ACQ_TIME       0xB202
#define SI468X_PROP_DAB_VALID_SYNC_TIME      0xB203
#define SI468X_PROP_DAB_VALID_DETECT_TIME    0xB204

/* SPI transfer specs */
#define SI468X_SPI_SPEED_HZ                  10000000  /* 10 MHz limit */
#define SI468X_SPI_BITS_PER_WORD             8
#define SI468X_SPI_MODE                      0         /* Mode 0 */

/* Helper macros */
#define SI468X_CTS_MASK                      0x80

// Dynamic diagnostic logging routing overrides
#ifdef __cplusplus
#include <iostream>
extern "C" {
    extern int si468x_debug_active;
}
#define SI468X_LOG if (si468x_debug_active) std::clog
#define SI468X_ERR if (si468x_debug_active) std::cerr
#else
#define SI468X_LOG if (0)
#define SI468X_ERR if (0)
#endif

/* Private Internal Helper Functions */
void si468x_decode_short_label(const uint8_t* raw_label, int raw_len, uint16_t char_mask, uint8_t charset, char* short_label, int max_len);
int send_command(const uint8_t* cmd, size_t cmd_len, uint8_t* resp, size_t resp_len, int timeout_ms = 1000);

#ifdef __cplusplus
}
#endif

#endif /* __SI468X_INTERNAL_H__ */
