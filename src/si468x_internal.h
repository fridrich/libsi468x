/*
 *    Copyright (C) 2026
 *    si468x_internal.h - Internal Opcodes and Declarations for libsi468x
 */

#ifndef __SI468X_INTERNAL_H__
#define __SI468X_INTERNAL_H__

#include <stdint.h>
#include <stddef.h>
#include "si468x.h"

/* Debug logging; C++ headers must stay outside extern "C" */
#ifdef __cplusplus
#include <iostream>
#define SI468X_LOG if (si468x_debug_active) std::clog
#define SI468X_ERR if (si468x_debug_active) std::cerr
#else
#define SI468X_LOG if (0)
#define SI468X_ERR if (0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Common Commands (All Modes) */
#define SI468X_CMD_RD_REPLY                  0x00
#define SI468X_CMD_POWER_UP                  0x01
#define SI468X_CMD_HOST_LOAD                 0x04
#define SI468X_CMD_LOAD_INIT                 0x06
#define SI468X_CMD_BOOT                      0x07
#define SI468X_CMD_GET_PART_INFO             0x08
#define SI468X_CMD_GET_SYS_STATE             0x09
#define SI468X_CMD_READ_OFFSET               0x10 /* not a revision query */
#define SI468X_CMD_GET_FUNC_INFO             0x12 /* firmware version; not event status */
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
#define SI468X_CMD_DAB_GET_ENSEMBLE_INFO     0xB4
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
#define SI468X_PROP_FM_SEEK_FREQUENCY_SPACING 0x3102
#define SI468X_PROP_FM_VALID_RSSI_THRESHOLD   0x3202
#define SI468X_PROP_FM_VALID_SNR_THRESHOLD    0x3204
#define SI468X_PROP_FM_RDS_INTERRUPT_SOURCE  0x3C00
#define SI468X_PROP_FM_RDS_INT_FIFO_COUNT    0x3C01
#define SI468X_PROP_FM_RDS_CONFIG            0x3C02
#define SI468X_PROP_DAB_XPAD_ENABLE          0xB400

/* DAB_VALID_* acquisition timing (not PAD; only 0xB400 DAB_XPAD_ENABLE concerns XPAD) */
#define SI468X_PROP_DAB_VALID_RSSI_TIME      0xB200
#define SI468X_PROP_DAB_VALID_RSSI_THRESHOLD 0xB201
#define SI468X_PROP_DAB_VALID_ACQ_TIME       0xB202
#define SI468X_PROP_DAB_VALID_SYNC_TIME      0xB203
#define SI468X_PROP_DAB_VALID_DETECT_TIME    0xB204

/* SPI transfer specs */
#define SI468X_SPI_SPEED_HZ                  10000000  /* 10 MHz limit */
#define SI468X_SPI_BITS_PER_WORD             8
#define SI468X_SPI_MODE                      0         /* Mode 0 */

/* Reply read lengths: 1 (dummy resp[0]) + reply length; _HDR = fixed part of variable replies */
#define SI468X_REP_PART_INFO        24
#define SI468X_REP_FUNC_INFO        13
#define SI468X_REP_GET_PROPERTY      7
#define SI468X_REP_AGC_STATUS       24  /* Linux driver: 23 */
#define SI468X_REP_FM_RSQ_STATUS    24
#define SI468X_REP_FM_RDS_STATUS    21
#define SI468X_REP_DSL_HDR           7  /* GET_DIGITAL_SERVICE_LIST, + SIZE */
#define SI468X_REP_DSD_HDR          25  /* GET_DIGITAL_SERVICE_DATA, + BYTE_COUNT */
#define SI468X_REP_HD_STATION_HDR    7  /* + LENGTH */
#define SI468X_REP_HD_PSD_HDR        9  /* + LENGTH */
#define SI468X_REP_HD_ALERT_HDR      7  /* + LENGTH */
#define SI468X_REP_HD_BER           45
#define SI468X_REP_DIGRAD_STATUS    24
#define SI468X_REP_DAB_EVENT_STATUS  9
#define SI468X_REP_ENSEMBLE_INFO    27
#define SI468X_REP_ANN_SUPPORT_HDR   9  /* + 2 * NUM_IDS */
#define SI468X_REP_ANN_INFO         17
#define SI468X_REP_LINKING_HDR       9  /* + SIZE */
#define SI468X_REP_COMPONENT_INFO   34
#define SI468X_REP_TIME             12
#define SI468X_REP_AUDIO_INFO       11  /* Linux driver: 21; Q1 */
#define SI468X_REP_SUBCHAN_INFO     13
#define SI468X_REP_FREQ_INFO_HDR     9  /* + 12 * N */
#define SI468X_REP_SERVICE_INFO     27
#define SI468X_REP_OE_SERVICES_HDR   9  /* + 2 * NUM_EIDS */
#define SI468X_REP_DAB_BER          13
#define SI468X_REP_MAX_VAR        4096  /* bound for chip-declared payload sizes */

/* Helper macros */
#define SI468X_CTS_MASK                      0x80

/* Private Internal Helper Functions */
extern int si468x_debug_active;
int si468x_parse_service_list(const uint8_t* resp, size_t len, si468x_service_t* list, int max_services);
void si468x_decode_short_label(const uint8_t* raw_label, int raw_len, uint16_t char_mask, uint8_t charset, char* short_label, int max_len);
/* Test seam: replaces spidev when set; nullptr restores spidev. Not in the installed header. */
typedef int (*si468x_transport_fn)(const uint8_t* tx, uint8_t* rx, size_t len, void* ctx);
void si468x_set_test_transport(si468x_transport_fn fn, void* ctx);
#define SC_ERR_OK 1 /* ERR is an expected outcome; caller inspects resp[1] */
int send_command(const uint8_t* cmd, size_t cmd_len, uint8_t* resp, size_t resp_len, int timeout_ms = 1000, unsigned flags = 0);
int read_reply(uint8_t* resp, size_t len);
int wait_stc(int timeout_ms);
uint8_t si468x_last_status(int i);

#ifdef __cplusplus
}
#endif

#endif /* __SI468X_INTERNAL_H__ */
