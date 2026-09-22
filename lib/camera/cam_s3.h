#pragma once

#include <stdint.h>
#include "esp_idf_version.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32
    #if ESP_IDF_VERSION_MAJOR >= 4
        #include "esp32/rom/lldesc.h"
    #else
        #include "rom/lldesc.h"
    #endif
#elif CONFIG_IDF_TARGET_ESP32S2
    #include "esp32s2/rom/lldesc.h"
#elif CONFIG_IDF_TARGET_ESP32S3
    #include "esp32s3/rom/lldesc.h"
#endif

#include "camera.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sensor.h"

#if __has_include("esp_private/periph_ctrl.h")
    #include "esp_private/periph_ctrl.h"
#endif
#if __has_include("esp_private/gdma.h")
    #include "esp_private/gdma.h"
#endif

#if CONFIG_LCD_CAM_ISR_IRAM_SAFE
    #define CAMERA_ISR_IRAM_FLAG ESP_INTR_FLAG_IRAM
    #define CAMERA_ISR_IRAM_ATTR IRAM_ATTR
#else
    #define CAMERA_ISR_IRAM_FLAG 0
    #define CAMERA_ISR_IRAM_ATTR
#endif

#define CAMERA_DBG_PIN_ENABLE 0

#if CAMERA_DBG_PIN_ENABLE
    #if CONFIG_IDF_TARGET_ESP32
        #define DBG_PIN_NUM 26
    #else
        #define DBG_PIN_NUM 7
    #endif
    #include "hal/gpio_ll.h"
    #define DBG_PIN_SET(v) gpio_ll_set_level(&GPIO, DBG_PIN_NUM, v)
#else
    #define DBG_PIN_SET(v)
#endif

#define CAM_CHECK(a, str, ret)                                    \
    if (!(a)) {                                                   \
        ESP_LOGE(TAG, "%s(%d): %s", __FUNCTION__, __LINE__, str); \
        return (ret);                                             \
    }

#define CAM_CHECK_GOTO(a, str, lab)                               \
    if (!(a)) {                                                   \
        ESP_LOGE(TAG, "%s(%d): %s", __FUNCTION__, __LINE__, str); \
        goto lab;                                                 \
    }

#define LCD_CAM_DMA_NODE_BUFFER_MAX_SIZE (4092)

typedef enum { CAM_IN_SUC_EOF_EVENT = 0, CAM_VSYNC_EVENT } cam_event_t;

typedef enum {
    CAM_STATE_IDLE     = 0,
    CAM_STATE_READ_BUF = 1,
} cam_state_t;

typedef struct {
    camera_fb_t fb;
    uint8_t     en;
    // for RGB/YUV modes
    lldesc_t* dma;
    size_t    fb_offset;
    size_t    max_size;
} cam_frame_t;

typedef struct {
    uint32_t dma_bytes_per_item;
    uint32_t dma_buffer_size;
    uint32_t dma_half_buffer_size;
    uint32_t dma_half_buffer_cnt;
    uint32_t dma_node_buffer_size;
    uint32_t dma_node_cnt;
    uint32_t frame_copy_cnt;

    // for JPEG mode
    lldesc_t* dma;
    uint8_t*  dma_buffer;

    cam_frame_t* frames;

    QueueHandle_t event_queue;
    QueueHandle_t frame_buffer_queue;
    TaskHandle_t  task_handle;
    intr_handle_t cam_intr_handle;

    uint8_t       dma_num;          // ESP32-S3
    intr_handle_t dma_intr_handle;  // ESP32-S3

#if SOC_GDMA_SUPPORTED
    gdma_channel_handle_t dma_channel_handle;  // ESP32-S3
#endif

    uint8_t  jpeg_mode;
    uint8_t  vsync_pin;
    uint8_t  vsync_invert;
    uint32_t frame_cnt;
    uint32_t recv_size;
    bool     swap_data;
    bool     psram_mode;

    // for RGB/YUV modes
    uint16_t width;
    uint16_t height;
    uint8_t  in_bytes_per_pixel;
    uint8_t  fb_bytes_per_pixel;
    uint32_t fb_size;

    cam_state_t state;
} cam_obj_t;

bool      cam_s3_stop(cam_obj_t* cam);
bool      cam_s3_start(cam_obj_t* cam, int frame_pos);
esp_err_t cam_s3_config(cam_obj_t* cam, const camera_config_t* config);
esp_err_t cam_s3_deinit(cam_obj_t* cam);
void      cam_s3_vsync_intr_enable(cam_obj_t* cam, bool en);
esp_err_t cam_s3_set_pin(cam_obj_t* cam, const camera_config_t* config);
esp_err_t cam_s3_init_isr(cam_obj_t* cam);
void      cam_s3_do_vsync(cam_obj_t* cam);
uint8_t   cam_s3_get_dma_align(cam_obj_t* cam);
bool      cam_s3_dma_sizes(cam_obj_t* cam);
size_t    cam_s3_memcpy(cam_obj_t* cam, uint8_t* out, const uint8_t* in, size_t len);
esp_err_t cam_s3_set_sample_mode(cam_obj_t* cam, pixformat_t pix_format, uint32_t xclk_freq_hz,
                                 uint16_t sensor_pid);
#if CONFIG_IDF_TARGET_ESP32S3
void cam_s3_dma_print_state(cam_obj_t* cam);
void cam_s3_dma_reset(cam_obj_t* cam);
#endif

// implemented in cam_hal
void cam_s3_send_event(cam_obj_t* cam, cam_event_t cam_event, BaseType_t* HPTaskAwoken);
