#pragma once

#include "camera.h"
#include "freertos/FreeRTOS.h"
#include "sensor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Uninitialize the lcd_cam module
 *
 * @param handle Provide handle pointer to release resources
 *
 * @return
 *     - ESP_OK Success
 *     - ESP_FAIL Uninitialize fail
 */
esp_err_t cam_hal_deinit(void);

/**
 * @brief Initialize the lcd_cam module
 *
 * @param config Configurations - see lcd_cam_config_t struct
 *
 * @return
 *     - ESP_OK Success
 *     - ESP_ERR_INVALID_ARG Parameter error
 *     - ESP_ERR_NO_MEM No memory to initialize lcd_cam
 *     - ESP_FAIL Initialize fail
 */
esp_err_t cam_hal_init(const camera_config_t* config);

esp_err_t cam_reconfigure_raw(framesize_t new_size);

esp_err_t cam_config(const camera_config_t* config, framesize_t frame_size, uint16_t sensor_pid);

void cam_stop(void);

void cam_start(void);

camera_fb_t* cam_take(TickType_t timeout);

void cam_give(camera_fb_t* dma_buffer);

void cam_give_all(void);

bool cam_get_available_frames(void);

void cam_hal_set_psram_mode(bool enable);
bool cam_hal_get_psram_mode(void);

#ifdef __cplusplus
}
#endif
