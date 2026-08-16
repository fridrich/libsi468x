/*
 *    Copyright (C) 2026
 *    si468x_internal.h - Internal Opcodes and Declarations for libsi468x
 */

#ifndef __SI468X_INTERNAL_H__
#define __SI468X_INTERNAL_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bootloader Opcodes */
#define SI468X_CMD_POWER_UP          0x01
#define SI468X_CMD_LOAD_INIT         0x06
#define SI468X_CMD_BOOT              0x07
#define SI468X_CMD_WRITE_FUT         0x04

/* Application Opcodes */
#define SI468X_CMD_GET_SYS_STATE     0x10
#define SI468X_CMD_SET_PROPERTY      0x12
#define SI468X_CMD_GET_PROPERTY      0x13
#define SI468X_CMD_GET_STATUS        0x14
#define SI468X_CMD_DAB_TUNE_FREQ     0xB0
#define SI468X_CMD_DAB_DIGRAD_STATUS 0xB2
#define SI468X_CMD_START_DIGITAL     0x81
#define SI468X_CMD_STOP_DIGITAL      0x82
#define SI468X_CMD_GET_DIGITAL_LIST  0xB5

/* Properties */
#define SI468X_PROP_AUDIO_VOLUME     0x0300
#define SI468X_PROP_PIN_CONFIG       0x0001

/* Pin config properties values */
#define SI468X_AUDIO_ROUTING_ANALOG  0
#define SI468X_AUDIO_ROUTING_I2S     1

/* SPI transfer specs */
#define SI468X_SPI_SPEED_HZ          10000000  /* 10 MHz limit */
#define SI468X_SPI_BITS_PER_WORD     8
#define SI468X_SPI_MODE              0         /* Mode 0 */

/* Helper macros */
#define SI468X_CTS_MASK              0x80

/* Private Internal Helper Functions (tested via unit tests) */
void si468x_decode_short_label(const char* long_label, uint16_t char_mask, char* short_label);
int send_command(const uint8_t* cmd, size_t cmd_len, uint8_t* resp, size_t resp_len, int timeout_ms = 1000);

#ifdef __cplusplus
}
#endif

#endif /* __SI468X_INTERNAL_H__ */
