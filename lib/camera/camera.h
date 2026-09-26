#pragma once

#include "driver/ledc.h"
#include "esp_err.h"
#include "sdkconfig.h"
#include "sensor.h"
#include "sys/time.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    // fills buffers when they are empty. Less resources but first 'fb_count' frames might be old
    CAMERA_GRAB_WHEN_EMPTY,
    // except when 1 frame buffer is used, queue will always contain the last 'fb_count' frames
    CAMERA_GRAB_LATEST
} camera_grab_mode_t;

typedef enum {
    CAMERA_FB_IN_PSRAM,  // Frame buffer is placed in external PSRAM
    CAMERA_FB_IN_DRAM    // Frame buffer is placed in internal DRAM
} camera_fb_location_t;

typedef struct {
    int pin_pwdn;   // GPIO pin for power down line
    int pin_reset;  // GPIO pin for reset line
    int pin_xclk;   // GPIO pin for XCLK line
    union {
        int pin_sccb_sda;  // GPIO pin for SDA line
        int pin_sscb_sda
            __attribute__((deprecated("please use pin_sccb_sda instead")));  // GPIO pin for SDA line (legacy name)
    };
    union {
        int pin_sccb_scl;  // GPIO pin for SCL line
        int pin_sscb_scl
            __attribute__((deprecated("please use pin_sccb_scl instead")));  // GPIO pin for SCL line (legacy name)
    };
    int pin_d7;     // GPIO pin for D7
    int pin_d6;     // GPIO pin for D6
    int pin_d5;     // GPIO pin for D5
    int pin_d4;     // GPIO pin for D4
    int pin_d3;     // GPIO pin for D3
    int pin_d2;     // GPIO pin for D2
    int pin_d1;     // GPIO pin for D1
    int pin_d0;     // GPIO pin for D0
    int pin_vsync;  // GPIO pin for VSYNC
    int pin_href;   // GPIO pin for HREF
    int pin_pclk;   // GPIO pin for PCLK

    int xclk_freq_hz;  // Frequency of XCLK signal

    ledc_timer_t   ledc_timer;    // LEDC timer to be used for generating XCLK
    ledc_channel_t ledc_channel;  // LEDC channel to be used for generating XCLK

    // Format of the pixel data: PIXFORMAT_ + YUV422|GRAYSCALE|RGB565|JPEG
    pixformat_t pixel_format;

    // Size of the output image: FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
    framesize_t frame_size;

    // Maximum frame size to allocate buffer memory for. If 0 or <= frame_size, defaults to
    // frame_size
    framesize_t max_frame_size;

    // Quality of JPEG output. 0-63 lower means higher quality
    int jpeg_quality;

    // Number of frame buffers to be allocated. If more than one, then each frame
    // will be acquired (double speed)
    size_t fb_count;

    camera_fb_location_t fb_location;  // The location where the frame buffer will be allocated
    camera_grab_mode_t   grab_mode;    // When buffers should be filled

    int sccb_i2c_port;  // If pin_sccb_sda is -1, use the already configured I2C bus by number

    // Size of the JPEG frame buffer in bytes. Set to 0 to use the default size
    size_t jpeg_buffer_size;
} camera_config_t;

typedef struct {
    uint8_t*       buf;        // Pointer to the pixel data
    size_t         len;        // Length of the buffer in bytes
    size_t         width;      // Width of the buffer in pixels
    size_t         height;     // Height of the buffer in pixels
    pixformat_t    format;     // Format of the pixel data
    struct timeval timestamp;  // Timestamp since boot of the first DMA buffer of the frame
} camera_fb_t;

#define ESP_ERR_CAMERA_BASE                     0x20000
#define ESP_ERR_CAMERA_NOT_DETECTED             (ESP_ERR_CAMERA_BASE + 1)
#define ESP_ERR_CAMERA_FAILED_TO_SET_FRAME_SIZE (ESP_ERR_CAMERA_BASE + 2)
#define ESP_ERR_CAMERA_FAILED_TO_SET_OUT_FORMAT (ESP_ERR_CAMERA_BASE + 3)
#define ESP_ERR_CAMERA_NOT_SUPPORTED            (ESP_ERR_CAMERA_BASE + 4)

/**
 * @brief Initialize the camera driver
 *
 * This function detects and configures camera over I2C interface,
 * allocates framebuffer and DMA buffers,
 * initializes parallel I2S input, and sets up DMA descriptors.
 *
 * Currently this function can only be called once and there is
 * no way to de-initialize this module.
 *
 * @param config  Camera configuration parameters
 *
 * @return ESP_OK on success
 */
esp_err_t cam_init(const camera_config_t* config);

/**
 * @brief Deinitialize the camera driver
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if the driver hasn't been initialized yet
 */
esp_err_t cam_deinit(void);

/**
 * @brief Obtain pointer to a frame buffer.
 *
 * @return pointer to the frame buffer
 */
camera_fb_t* cam_fb_get(void);

/**
 * @brief Return the frame buffer to be reused again.
 *
 * @param fb    Pointer to the frame buffer
 */
void cam_fb_return(camera_fb_t* fb);

/**
 * @brief Get a pointer to the image sensor control structure
 *
 * @return pointer to the sensor
 */
sensor_t* cam_sensor_get(void);

/**
 * @brief Get current framesize
 *
 * @return current framesize
 */
framesize_t cam_get_framesize(void);

/**
 * @brief Return all frame buffers to be reused again.
 */
void cam_return_all(void);

/**
 * @brief Check if there are available frames to be immediately acquired
 */
bool cam_available_frames(void);

/**
 * @brief Reinitialize the camera with a new configuration.
 *
 * @param config  Updated camera configuration structure
 * @return
 * - ESP_OK on success
 * - ESP_ERR_INVALID_ARG if config is NULL
 * - Propagated error from deinit or init if they fail
 */
esp_err_t cam_reconfigure(const camera_config_t* config);

/**
 * @brief Dynamically switch sensor and HAL dimensions in RAW modes (RGB565/YUV422)
 *        without sensor reset or loss of AWB/exposure convergence.
 *
 * @param framesize  Target frame size (must be <= max_frame_size passed at init)
 * @return ESP_OK on success, ESP_ERR_NO_MEM if target exceeds allocated buffer
 */
esp_err_t cam_set_raw_framesize(framesize_t framesize);

/**
 * @brief Directly inject RGB channel multipliers into sensor DSP.
 *        Useful for manual WB locking, tint adjustment, and glitch effects.
 *
 * @param red   Red multiplier
 * @param green Green multiplier
 * @param blue  Blue multiplier
 */
esp_err_t cam_set_color_gains(uint8_t red, uint8_t green, uint8_t blue);

/**
 * @brief Enable or disable PSRAM DMA mode at runtime.
 *
 * @param enable  True to enable PSRAM DMA mode, false to disable it.
 * @return
 * - ESP_OK on success
 * - ESP_ERR_INVALID_STATE if the camera is not initialized
 * - Propagated error from reinitialization on failure
 */
esp_err_t cam_set_psram_mode(bool enable);

/**
 * @brief Get current PSRAM DMA mode state.
 *
 * @return True if PSRAM DMA is enabled, false otherwise.
 */
bool cam_get_psram_mode(void);

#ifdef __cplusplus
}
#endif
