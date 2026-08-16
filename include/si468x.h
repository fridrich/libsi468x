/*
 *    Copyright (C) 2026
 *    si468x.h - Reusable C-API Driver for Silicon Labs Si468x Receivers
 */

#ifndef __SI468X_H__
#define __SI468X_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SI468X_SUCCESS          0
#define SI468X_ERROR_SPI       -1
#define SI468X_ERROR_GPIO      -2
#define SI468X_ERROR_FIRMWARE  -3
#define SI468X_ERROR_BOOT      -4
#define SI468X_ERROR_TIMEOUT   -5

/* Bootloader / Application firmware modes */
#define SI468X_BOOT_DAB         0
#define SI468X_BOOT_FMHD        1

typedef struct {
    uint8_t chip_id;
    uint8_t rom_id;
    uint16_t fw_version;
    uint16_t patch_version;
} si468x_chip_info_t;

typedef struct {
    uint32_t service_id;
    uint32_t component_id;
    char label[17];            /* 16-character DAB Label + null-terminator */
    char short_label[9];       /* 8-character Short Label + null-terminator */
    uint16_t bitrate;
    uint8_t audio_type;        /* 0 for DAB, 1 for DAB+, etc. */
} si468x_service_t;

typedef struct {
    uint8_t rssi;              /* RSSI in dBµV */
    uint8_t snr;               /* SNR in dB */
    int16_t freq_offset;       /* Frequency offset in kHz */
    uint8_t sync_status;       /* 1 if synced, 0 otherwise */
} si468x_signal_status_t;

typedef struct {
    uint32_t frequency_hz;     /* Currently tuned frequency in Hz */
    uint8_t rssi;              /* RSSI in dBµV */
    uint8_t snr;               /* SNR in dB */
    uint8_t multipath;         /* Multipath metric (0-100) */
    int8_t freq_offset;        /* Frequency offset in kHz */
    uint8_t hd_synced;         /* 1 if HD Radio is synced, 0 otherwise */
    uint8_t rds_synced;        /* 1 if RDS is synced, 0 otherwise */
} si468x_fm_status_t;

/*
 * Initialize the hardware, reset the chip, upload the patch, stream the
 * selected application firmware (statically embedded DAB vs FMHD), and boot.
 *
 * Params:
 *   spi_device: Path to the SPI character device (e.g. "/dev/spidev0.0")
 *   rst_pin: GPIO pin number used for RSTB reset line (e.g. 16)
 *   boot_mode: SI468X_BOOT_DAB (for DAB mode) or SI468X_BOOT_FMHD (for FM-HD)
 */
int si468x_init(const char* spi_device, int rst_pin, int boot_mode);

/*
 * Shut down the chip, close open file handles, and drive RSTB low (safety state).
 */
int si468x_shutdown(void);

/*
 * Cleanly clear the co-processor's internal service list database.
 * Returns SI468X_SUCCESS on success, or negative error code.
 */
int si468x_clear_service_list(void);

/*
 * Tune the hardware receiver to a specified frequency in Hz (e.g. 227360000 for 12C).
 */
int si468x_set_frequency(uint32_t frequency_hz);

/*
 * Get the current tuned frequency in Hz.
 */
uint32_t si468x_get_frequency(void);

/*
 * Direct the DSP to demux and decode the chosen digital service component.
 */
int si468x_play_service(uint32_t service_id, uint32_t component_id);

/*
 * Stop decoding the active digital service.
 */
int si468x_stop_service(void);

/*
 * Set the output volume level (0 is muted, 63 is max).
 */
int si468x_set_volume(uint8_t volume);

/*
 * Query the on-chip service database and retrieve decoded services.
 * Returns the number of services written to the list array.
 */
int si468x_get_service_list(si468x_service_t* list, int max_services);

/*
 * Query the on-chip DSP for digital radio signal metrics (RSSI, SNR, Frequency Offset, and Sync status).
 */
int si468x_get_signal_status(si468x_signal_status_t* status);

/*
 * Configure the audio output path.
 * Pass 1 to enable I2S digital output (analog out will be deactivated).
 * Pass 0 to enable Analog output to the 3.5mm headphone jack.
 */
int si468x_set_audio_output(int enable_i2s);

/*
 * Retrieve the current tuned DAB ensemble label and Ensemble ID (EId/UEID).
 * Returns SI468X_SUCCESS on success, or negative error code.
 */
int si468x_get_ensemble_info(char* label, uint16_t* ueid);

/*
 * Retrieve detailed component specifications dynamically from the co-processor over SPI (Opcode 0x82).
 * Returns SI468X_SUCCESS on success, or negative error code.
 */
int si468x_get_component_info(uint32_t service_id, uint32_t component_id, char* label, char* short_label, uint8_t* subchannel_id);

/*
 * Retrieve the current live Dynamic Label Segment (DLS) scrolling text (up to 128 characters, UTF-8 encoded)
 * from the actively playing DAB+ service component.
 *
 * Returns 1 if a new/updated DLS label is available (and copies it to out_text),
 * returns 0 if the DLS label has not changed, or a negative error code on failure.
 */
int si468x_get_dls_text(char* out_text, int max_len);

/*
 * Expose chip revision and part information.
 */
int si468x_get_chip_info(si468x_chip_info_t* info);

/*
 * Set custom Band III frequency table for DAB scanning.
 */
int si468x_set_frequency_table(const uint32_t* freqs, int count);

/*
 * Tune to an analog/HD FM frequency in kHz (e.g. 98100 for 98.1 MHz).
 */
int si468x_tune_fm(uint32_t frequency_khz);

/*
 * Query the on-chip DSP for FM/FMHD signal status and RDS metrics.
 */
int si468x_get_fm_status(si468x_fm_status_t* status);

/*
 * Read and decode the live FM RDS RadioText (up to 64 characters, null-terminated).
 * Returns 1 if updated text is available, 0 if unchanged, or negative error code.
 */
int si468x_get_rds_text(char* out_text, int max_len);

#ifdef __cplusplus
}
#endif

#endif /* __SI468X_H__ */
