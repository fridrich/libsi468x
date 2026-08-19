/**
 * @file si468x.h
 * @brief Reusable C-API Driver for Silicon Labs Si468x Receivers.
 *
 * This header defines the public C interface of the libsi468x library,
 * supporting high-performance DAB/DAB+ and FM-HD analog/digital tuning,
 * signal quality metrics telemetry, and RDS/DLS text decoding.
 *
 * @copyright Copyright (C) 2026. All rights reserved.
 */

#ifndef __SI468X_H__
#define __SI468X_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Error Status Codes
 * @{
 */
#define SI468X_SUCCESS          0   /**< Operation completed successfully. */
#define SI468X_ERROR_SPI       -1   /**< General SPI bus transaction error or I/O failure. */
#define SI468X_ERROR_GPIO      -2   /**< GPIO pin export, direction setting, or write failure. */
#define SI468X_ERROR_FIRMWARE  -3   /**< Firmware image file read or memory allocation error. */
#define SI468X_ERROR_BOOT      -4   /**< Co-processor failed to boot or load successfully. */
#define SI468X_ERROR_TIMEOUT   -5   /**< Timeout waiting for CTS, STC, or hardware state change. */
/** @} */

/**
 * @name Bootloader Application Modes
 * @{
 */
#define SI468X_BOOT_DAB         0   /**< Boot co-processor in DAB/DAB+ digital radio mode. */
#define SI468X_BOOT_FMHD        1   /**< Boot co-processor in FM/FM-HD analog and digital mode. */
/** @} */

/**
 * @name Regional Reception Standards
 * @{
 */
#define SI468X_REGION_EUROPE    0   /**< Europe and standard international world (50 us de-emphasis). */
#define SI468X_REGION_US        1   /**< North America (75 us de-emphasis). */
/** @} */

/**
 * @name Audio Output Routing Paths
 * @{
 */
#define SI468X_AUDIO_ANALOG     0   /**< Route audio to the analog headphone jack only. */
#define SI468X_AUDIO_I2S        1   /**< Route audio to the I2S digital interface only. */
#define SI468X_AUDIO_SIMUL      2   /**< Route audio to both Analog and I2S outputs simultaneously. */
/** @} */

/**
 * @name DAB Service Audio Component Types
 * @{
 */
#define SI468X_AUDIO_TYPE_DAB      0    /**< Legacy MPEG Layer II (MP2) audio component standard. */
#define SI468X_AUDIO_TYPE_DAB_PLUS 15   /**< Modern HE-AAC v2 (DAB+ AAC) audio component standard. */
/** @} */

/**
 * @name Volume Boundaries
 * @{
 */
#define SI468X_VOLUME_MIN       0    /**< Minimum volume level (muted). */
#define SI468X_VOLUME_MAX       63   /**< Maximum hardware output volume level. */
/** @} */

/**
 * @brief Structure containing raw chip part revision and firmware specifications.
 */
typedef struct {
    uint8_t chip_id;        /**< Raw part ID (e.g. 0x05 for Si4685, 0x06 for Si4686). */
    uint8_t rom_id;         /**< Hardcoded ROM revision ID. */
    uint16_t fw_version;    /**< Application firmware version (e.g. 0x0500 represents 5.0). */
    uint16_t patch_version; /**< Uploaded boot patch version. */
} si468x_chip_info_t;

/**
 * @brief Structure representing a dynamically retrieved digital DAB service record.
 */
typedef struct {
    uint32_t service_id;        /**< 32-bit Service ID (SId) of the radio program. */
    uint32_t component_id;      /**< 32-bit global Component ID. */
    char label[17];             /**< 16-character full service name (null-terminated). */
    char short_label[9];        /**< 8-character abbreviated name (null-terminated). */
    uint16_t bitrate;           /**< Service audio stream compression bitrate in kbps. */
    uint8_t audio_type;         /**< Codec format (e.g. 0 for legacy DAB, 15/0x0F for DAB+ AAC). */
} si468x_service_t;

/**
 * @brief Structure representing detailed metadata for a specific DAB service (Opcode 0xC0).
 */
typedef struct {
    uint8_t pty;                /**< Program Type (e.g., News, Pop Music). Range 0-31. */
    uint8_t ecc;                /**< Extended Country Code. */
    char label[17];             /**< 16-character full service name (null-terminated). */
    uint8_t charset;            /**< Character Set Identifier (e.g., 0 for EBU Latin, 15 for UTF-8). */
} si468x_service_info_t;

/**
 * @brief Structure representing digital radio signal strength and quality metrics.
 */
typedef struct {
    uint8_t rssi;          /**< Received Signal Strength Indicator in dBµV. */
    uint8_t snr;           /**< Signal-to-Noise Ratio in dB. */
    int16_t freq_offset;   /**< Frequency offset from tuned carrier center in kHz. */
    uint8_t sync_status;   /**< Synchronization lock flag (1 if synced, 0 if unlocked). */
} si468x_signal_status_t;

/**
 * @brief Structure representing analog and HD FM tuner metrics, RDS status, and SNR telemetry.
 */
typedef struct {
    uint32_t frequency_hz;     /**< Currently tuned carrier frequency in Hz. */
    uint8_t rssi;              /**< Received Signal Strength Indicator in dBµV. */
    uint8_t snr;               /**< Signal-to-Noise Ratio in dB. */
    uint8_t multipath;         /**< Multipath distortion metric (range 0 to 100). */
    int8_t freq_offset;        /**< Frequency offset from tuned center in kHz. */
    uint8_t hd_synced;         /**< HD Radio digital carrier synchronization flag (1 if synced, 0 if unlocked). */
    uint8_t rds_synced;        /**< RDS subcarrier synchronization lock flag (1 if synced, 0 if unlocked). */
} si468x_fm_status_t;

/**
 * @brief Structure representing the dynamically retrieved UTC network date-time of a DAB ensemble.
 */
typedef struct {
    uint16_t year;             /**< Year (e.g. 2026). */
    uint8_t month;             /**< Month (1 to 12). */
    uint8_t day;               /**< Day of the month (1 to 31). */
    uint8_t hours;             /**< Hours (0 to 23). */
    uint8_t minutes;           /**< Minutes (0 to 59). */
} si468x_time_t;

/**
 * @brief Structure representing real-time digital audio decoder metrics.
 */
typedef struct {
    uint16_t bitrate;      /**< Current audio stream bitrate in kbps. */
    uint16_t sample_rate;  /**< Current audio stream sample rate in Hz (e.g. 48000). */
    uint8_t audio_mode;    /**< Audio mode (e.g. 0=Dual, 1=Mono, 2=Stereo, 3=Joint Stereo). */
} si468x_audio_info_t;

/**
 * @brief Structure representing active asynchronous event flags.
 */
typedef struct {
    uint8_t reconf;            /**< 1 if multiplex reconfiguration event occurred, 0 otherwise. */
    uint8_t annc;              /**< 1 if live service announcement event occurred, 0 otherwise. */
} si468x_event_status_t;

/**
 * @brief Structure representing DAB-specific async event status (Opcode 0xB3).
 */
typedef struct {
    uint8_t recfg_int;       /**< Reconfiguration interrupt flag. */
    uint8_t recfg_warn_int;  /**< Reconfiguration warning interrupt flag. */
    uint8_t anno_int;        /**< Announcement interrupt flag. */
    uint8_t anno_active;     /**< Active announcement status. */
    uint8_t oeserv_int;      /**< Other Ensemble service list changed interrupt. */
    uint8_t oeserv_active;   /**< Sibling/Other Ensemble available status. */
    uint8_t servlink_int;    /**< Service Linking interrupt flag. */
    uint8_t servlink_active; /**< Alternate Service handover linking active. */
    uint8_t freqinfo_int;    /**< Frequency info updated interrupt flag. */
    uint8_t freqinfo_active; /**< Handover/alternative frequencies available. */
    uint8_t svrlist_int;     /**< Service list database updated interrupt flag. */
    uint8_t svrlist_active;  /**< Active database service list parsing status. */
    uint16_t svrlist_ver;    /**< 16-bit Service list database version on-chip. */
} si468x_dab_event_status_t;

/**
 * @brief Structure representing DAB subchannel layout and capacity allocation details.
 */
typedef struct {
    uint8_t service_mode;    /**< Audio stream service mode type. */
    uint8_t protection_info; /**< Protection index (UEP/EEP level). */
    uint16_t bitrate;        /**< Stream bitrate in kbps. */
    uint16_t num_cu;         /**< Number of Capacity Units (CUs) allocated. */
    uint16_t cu_address;     /**< Multiplex CU start address. */
} si468x_subchan_info_t;

/**
 * @brief Structure representing detailed DAB multiplex frequency allocation elements.
 */
typedef struct {
    uint32_t id;             /**< Frequency ensemble ID. */
    uint32_t frequency_hz;   /**< Alternate ensemble frequency in Hz. */
    uint8_t freq_index;      /**< Alternate frequency index. */
    uint8_t rnm;             /**< Reconfiguration Network Marker. */
    uint8_t continuity;      /**< Signal transmission continuity. */
    uint8_t control;         /**< Signal control flag. */
} si468x_freq_element_t;

/**
 * @brief Structure representing RF Automatic Gain Control (AGC) diagnostic status.
 */
typedef struct {
    uint8_t rfagc_dis;       /**< RF AGC loop disabled flag (1 if disabled, 0 if enabled). */
    uint8_t ifagc_dis;       /**< IF AGC loop disabled flag (1 if disabled, 0 if enabled). */
    uint8_t lna_gain_index;  /**< LNA Gain attenuation step index (0 to 15, higher means more attenuation). */
} si468x_agc_status_t;

/**
 * @brief Initialize the driver library, perform cold reset, and boot the co-processor.
 *
 * This function exports and configures the reset GPIO, opens the SPI bus, performs a
 * hardware reset by pulling RSTB low, uploads the statically embedded boot patch,
 * uploads the selected application firmware, and issues the boot command.
 *
 * @param spi_device Path to the SPI character device (e.g. "/dev/spidev0.0").
 * @param rst_pin BCM GPIO pin number wired to RSTB reset line (e.g. 23).
 * @param boot_mode Target application core mode (SI468X_BOOT_DAB or SI468X_BOOT_FMHD).
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_init(const char* spi_device, int rst_pin, int boot_mode);

/**
 * @brief Dynamically enable or disable the library's internal diagnostic debug logging.
 * @param enable Pass 1 to enable diagnostic logging to stderr/stdout, or 0 to keep the library silent (default).
 */
void si468x_enable_debug(int enable);

/**
 * @brief Safely terminate the driver, close spi file descriptors, and drive RSTB reset low.
 *
 * Puts the co-processor into a safe, ultra-low-power reset standby mode.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_shutdown(void);

/**
 * @brief Cleanly clear the co-processor's internal digital service list database SRAM.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_clear_service_list(void);

/**
 * @brief Tune the hardware receiver synthesizer to a specified digital frequency in Hz.
 * @param frequency_hz Target frequency in Hz (e.g. 223936000 for Channel 12A).
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_set_frequency(uint32_t frequency_hz);

/**
 * @brief Retrieve the currently tuned carrier frequency in Hz.
 * @return Frequency in Hz, or 0 if uninitialized.
 */
uint32_t si468x_get_frequency(void);

/**
 * @brief Direct the co-processor to demux and decode the chosen digital service component.
 * @param service_id 32-bit Service ID (SId) to start playing.
 * @param component_id 32-bit global Component ID.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_play_service(uint32_t service_id, uint32_t component_id);

/**
 * @brief Stop decoding the active digital service component.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_stop_service(void);

/**
 * @brief Set the audio output master volume.
 * @param volume Output volume level (range 0 to 63, where 0 is muted and 63 is maximum).
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_set_volume(uint8_t volume);

/**
 * @brief Query the on-chip service list database and retrieve all compiled services.
 * @param list Pointer to pre-allocated destination service record array.
 * @param max_services Maximum number of records that can fit in the pre-allocated array.
 * @return Number of service records successfully copied, or negative error code on failure.
 */
int si468x_get_service_list(si468x_service_t* list, int max_services);

/**
 * @brief Retrieve detailed metadata (PTY, ECC, Label) for a specific digital service.
 * @param service_id Globally unique 32-bit Service ID.
 * @param info Pointer to target si468x_service_info_t struct to populate.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_get_service_info(uint32_t service_id, si468x_service_info_t* info);

/**
 * @brief Query the on-chip DSP for digital radio signal metrics.
 * @param status Pointer to target signal status struct to populate.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_get_signal_status(si468x_signal_status_t* status);

/**
 * @brief Configure the master audio hardware output path routing.
 * @param enable_i2s Pass 1 to enable I2S digital output, or 0 to route to the analog headphone jack.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_set_audio_output(int enable_i2s);

/**
 * @brief Retrieve the currently tuned DAB ensemble label name and Ensemble ID (EId).
 * @param label Pointer to pre-allocated char array (at least 17 bytes) to store the name.
 * @param ueid Pointer to 16-bit word to populate with the Ensemble ID.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_get_ensemble_info(char* label, uint16_t* ueid);

/**
 * @brief Structure representing an assembled MOT Slideshow image (JPEG/PNG).
 */
typedef struct {
    const uint8_t* image_data; /**< Pointer to the assembled image byte buffer. */
    uint32_t image_size;       /**< Total size of the assembled image in bytes. */
    uint16_t transport_id;     /**< Transport ID (TID) of the current slideshow object. */
    int is_new;                /**< 1 if this is a newly completed frame, 0 otherwise. */
} si468x_mot_slideshow_t;

/**
 * @brief Query the co-processor to retrieve the current dynamically synced UTC ensemble time.
 * @param time Pointer to target si468x_time_t struct to populate.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_get_time(si468x_time_t* time);

/**
 * @brief Query the co-processor to retrieve real-time digital audio stream metrics.
 * @param info Pointer to target si468x_audio_info_t struct to populate.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_get_audio_info(si468x_audio_info_t* info);

/**
 * @brief Query the co-processor to retrieve the current active asynchronous event status flags.
 * @param status Pointer to target si468x_event_status_t struct to populate.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_get_event_status(si468x_event_status_t* status);

/**
 * @brief Configure the FM de-emphasis regional filtering standards dynamically.
 * @param region Target region (SI468X_REGION_EUROPE for 50 us, or SI468X_REGION_US for 75 us).
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_set_rds_region(int region);

/**
 * @brief Retrieve detailed component-level specifications dynamically from the on-chip database.
 * @param service_id Globally unique 32-bit Service ID.
 * @param component_id Globally unique 32-bit Component ID.
 * @param label Pre-allocated 17-byte buffer to populate with the 16-character long label.
 * @param short_label Pre-allocated 9-byte buffer to populate with the 8-character short label.
 * @param char_mask Pointer to 16-bit word to populate with the short label character flag mask.
 * @param subchannel_id Pointer to 8-bit byte to populate with the component's subchannel ID.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_get_component_info(uint32_t service_id, uint32_t component_id, char* label, char* short_label, uint16_t* char_mask, uint8_t* subchannel_id);

/**
 * @brief Retrieve the dynamic, scrolling DAB DLS (Dynamic Label Segment) UTF-8 station text.
 * @param out_text Pre-allocated string destination buffer.
 * @param max_len Size of the destination buffer (at least 129 bytes recommended).
 * @return 1 if a newly completed DLS text frame is assembled, 0 if unchanged, or negative error.
 */
int si468x_get_dls_text(char* out_text, int max_len);

/**
 * @brief Poll and assemble MOT Slideshow image segments from the active data channel.
 *
 * This function incrementally collects segment frames broadcasted over the air. It must be
 * polled continuously. It returns 1 strictly once a complete, hole-free JPEG or PNG image
 * is fully assembled in its internal buffer.
 *
 * @param slideshow Pointer to the target si468x_mot_slideshow_t struct to populate.
 * @return 1 if a complete new image is assembled, 0 if compiling/unchanged, or negative error.
 */
int si468x_get_mot_slideshow(si468x_mot_slideshow_t* slideshow);

/**
 * @brief Query the secure boot ROM for part number, chip ID, and hardware revision info.
 * @param info Pointer to chip info structure to populate.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_get_chip_info(si468x_chip_info_t* info);

/**
 * @brief Configure a custom Band III frequency table for autonomous scanning.
 * @param freqs Pointer to array of frequencies in Hz.
 * @param count Number of elements in the frequency array.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_set_frequency_table(const uint32_t* freqs, int count);

/**
 * @brief Tune the analog/digital FM synthesizer to a specified carrier frequency in kHz.
 * @param frequency_khz Carrier frequency in kHz (e.g. 107000 for 107.0 MHz).
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_tune_fm(uint32_t frequency_khz);

/**
 * @brief Query the on-chip DSP for live FM carrier metrics and RDS sync parameters.
 * @param status Pointer to FM status structure to populate.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_get_fm_status(si468x_fm_status_t* status);

/**
 * @brief Read, assemble, and decode the live FM RDS RadioText (RT) scrolling station text.
 *
 * This function drains the internal RDS FIFO queue, maps the 16-bit blocks A, B, C, D to
 * their correct FMHD mode offsets, and runs the segment compiler. It returns 1 strictly
 * once a newly compiled, hole-free 64-character RadioText string is fully assembled.
 *
 * @param out_text Pre-allocated destination buffer (at least 65 bytes recommended).
 * @param max_len Size of the destination buffer.
 * @return 1 if a complete new RadioText string is assembled, 0 if compiling/unchanged, or negative error.
 */
int si468x_get_rds_text(char* out_text, int max_len);

/**
 * @brief Map DAB protection level index to standard string label.
 * @param level Protection level index.
 * @return String label (e.g. "EEP-A1"), or empty string if unknown.
 */
const char* si468x_get_protection_text(uint8_t level);

/**
 * @brief Map DAB audio mode index to standard string label.
 * @param mode Audio mode index.
 * @return String label (e.g. "Stereo"), or empty string if unknown.
 */
const char* si468x_get_audio_mode_text(uint8_t mode);

/**
 * @brief Map DAB service type index to standard string label.
 * @param type Service type index.
 * @return String label (e.g. "DAB+"), or empty string if unknown.
 */
const char* si468x_get_service_type_text(uint8_t type);

/**
 * @brief Trigger an autonomous FM seek operation.
 * @param seek_up Seek direction: 1 = seek up, 0 = seek down.
 * @param wrap Wrap around band boundaries: 1 = wrap around, 0 = stop at band edge.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_fm_seek_start(int seek_up, int wrap);

/**
 * @brief Query DAB announcement support flags for a specific service component.
 * @param service_id Globally unique 32-bit Service ID.
 * @param component_id Globally unique 32-bit Component ID.
 * @param asw_flags Pointer to target 16-bit variable to populate with support flags (ASW flags).
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_dab_get_announcement_support(uint32_t service_id, uint32_t component_id, uint16_t* asw_flags);

/**
 * @brief Retrieve current active announcement alert details.
 * @param buf_empty Clear/empty active queue buffer flag: 1 = empty, 0 = read only.
 * @param service_id Pointer to populate with the interrupting Service ID.
 * @param component_id Pointer to populate with the interrupting Component ID.
 * @param asw_flags Pointer to populate with the active alert flags.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_dab_get_announcement_info(int buf_empty, uint32_t* service_id, uint32_t* component_id, uint16_t* asw_flags);

/**
 * @brief Query DAB Service Linking and alternate handover frequencies.
 * @param service_id Globally unique 32-bit Service ID.
 * @param link_info Pre-allocated buffer to store raw linking information payload.
 * @param max_len Size of the destination buffer.
 * @return Number of linking bytes successfully copied, or a negative error code on failure.
 */
int si468x_dab_get_service_linking(uint32_t service_id, uint8_t* link_info, int max_len);

/**
 * @brief Retrieve detailed layout and capacity allocation details (bitrate, CUs, address) for a DAB subchannel.
 * @param service_id Globally unique 32-bit Service ID.
 * @param component_id Globally unique 32-bit Component ID.
 * @param info Pointer to target si468x_subchan_info_t struct to populate.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_dab_get_subchan_info(uint32_t service_id, uint32_t component_id, si468x_subchan_info_t* info);

/**
 * @brief Retrieve detailed DAB multiplex frequency allocation elements (alternate/handover frequencies).
 * @param elements Pre-allocated array of si468x_freq_element_t structs to populate.
 * @param max_elements Size of the pre-allocated array.
 * @return Number of frequency elements successfully copied, or a negative error code on failure.
 */
int si468x_dab_get_freq_info(si468x_freq_element_t* elements, int max_elements);

/**
 * @brief Retrieve detailed, DAB-specific active asynchronous event status flags (reconfigurations, versioning, list updates).
 * @param status Pointer to target si468x_dab_event_status_t struct to populate.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_dab_get_event_status(si468x_dab_event_status_t* status);

/**
 * @brief Perform an antenna capacitance calibration sweep for a specific DAB frequency to locate peak signal envelope.
 * @param frequency_hz Target DAB carrier frequency in Hz.
 * @param peak_antcap Pointer to target variable to populate with the optimal antenna capacitance step index (1 to 128).
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_dab_calibrate_antenna(uint32_t frequency_hz, uint16_t* peak_antcap);

/**
 * @brief Retrieve alternate DAB Ensemble IDs (EIDs) carrying the specified service ID (Other Ensembles).
 * @param service_id Globally unique 32-bit Service ID.
 * @param eids Pre-allocated array of 16-bit EIDs to populate.
 * @param max_eids Size of the pre-allocated EID array.
 * @return Number of Ensemble IDs successfully copied, or a negative error code on failure.
 */
int si468x_dab_get_other_ensemble_info(uint32_t service_id, uint16_t* eids, int max_eids);

/**
 * @brief Retrieve current DAB Bit Error Rate (BER) and FIB frame block error counts.
 * @param clear Clear current count values: 1 = clear on read, 0 = read only.
 * @param ber_count Pointer to populate with the total bit error count.
 * @param fib_error_count Pointer to populate with the Fast Information Block (FIB) error count.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_dab_get_ber_info(int clear, uint32_t* ber_count, uint32_t* fib_error_count);

/**
 * @brief Retrieve real-time Program Service Data (PSD) track metadata (Title, Artist, Album, Genre) for FMHD.
 * @param program Program selection: 0 for MPS (Main Program Service), 1-7 for SPS (Supplemental Program Services), 0xFF for current.
 * @param field Field selection: 0 = Title, 1 = Artist, 2 = Album, 3 = Genre.
 * @param out_text Destination string buffer to populate.
 * @param max_len Size of the destination buffer.
 * @return 1 if newly updated metadata is copied, 0 if unchanged/not ready, or a negative error code on failure.
 */
int si468x_fmhd_get_psd_text(int program, int field, char* out_text, int max_len);

/**
 * @brief Retrieve Station Information Service (SIS) details for the currently tuned FMHD digital station.
 * @param info_select Selector index: 1 = Station Message, 4 = Universal Short Name, 5 = Basic SIS, 6 = Slogan.
 * @param out_text Destination string buffer to populate.
 * @param max_len Size of the destination buffer.
 * @return 1 if newly updated station metadata is copied, 0 if unchanged/not ready, or a negative error code on failure.
 */
int si468x_fmhd_get_station_info(int info_select, char* out_text, int max_len);

/**
 * @brief Retrieve current active HD Radio (FMHD) emergency alert message text.
 * @param alert_text Destination string buffer to populate.
 * @param max_len Size of the destination buffer.
 * @return 1 if newly updated alert text is copied, 0 if no active alerts, or a negative error code on failure.
 */
int si468x_fmhd_get_alert_message(char* alert_text, int max_len);

/**
 * @brief Query current RF Automatic Gain Control (AGC) diagnostics and LNA gain state.
 * @param status Pointer to target si468x_agc_status_t struct to populate.
 * @return SI468X_SUCCESS on success, or a negative error code on failure.
 */
int si468x_get_agc_status(si468x_agc_status_t* status);

#ifdef __cplusplus
}
#endif

#endif /* __SI468X_H__ */
