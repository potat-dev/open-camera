#include "camera.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cam_hal.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ov2640.h"
#include "sccb.h"
#include "sensor.h"
#include "sys/time.h"
#include "time.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
    #include "esp32-hal-log.h"
#else
    #include "esp_log.h"
#endif

static const char* TAG = "camera";

typedef struct {
    sensor_t    sensor;
    camera_fb_t fb;
} camera_state_t;

static camera_state_t* s_state = NULL;
static camera_config_t s_saved_config;

// LCD_CAM module of ESP32-S3 will generate xclk
#define CAMERA_ENABLE_OUT_CLOCK(v)
#define CAMERA_DISABLE_OUT_CLOCK()

typedef struct {
    int (*detect)(int slv_addr, sensor_id_t* id);
    int (*init)(sensor_t* sensor);
} sensor_func_t;

static const sensor_func_t g_sensors[] = {
    {esp32_camera_ov2640_detect, esp32_camera_ov2640_init},
};

static esp_err_t camera_probe(const camera_config_t* config, camera_model_t* out_camera_model) {
    gpio_config_t conf = {};
    int           camera_model_id;
    uint8_t       slv_addr = 0x0;

    esp_err_t ret     = ESP_OK;
    *out_camera_model = CAMERA_NONE;
    if (s_state != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_state = (camera_state_t*)calloc(1, sizeof(camera_state_t));
    if (!s_state) {
        return ESP_ERR_NO_MEM;
    }

    if (config->pin_xclk >= 0) {
        ESP_LOGD(TAG, "Enabling XCLK output");
        CAMERA_ENABLE_OUT_CLOCK(config);
    }

    if (config->pin_sccb_sda != -1) {
        ESP_LOGD(TAG, "Initializing SCCB");
        ret = SCCB_Init(config->pin_sccb_sda, config->pin_sccb_scl);
    } else {
        ESP_LOGD(TAG, "Using existing I2C port");
        ret = SCCB_Use_Port(config->sccb_i2c_port);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sccb init err");
        goto err;
    }

    if (config->pin_pwdn >= 0) {
        ESP_LOGD(TAG, "Resetting camera by power down line");
        conf.pin_bit_mask = 1LL << config->pin_pwdn;
        conf.mode         = GPIO_MODE_OUTPUT;
        gpio_config(&conf);

        // careful, logic is inverted compared to reset pin
        gpio_set_level((gpio_num_t)config->pin_pwdn, 1);
        vTaskDelay(10 / portTICK_PERIOD_MS);
        gpio_set_level((gpio_num_t)config->pin_pwdn, 0);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    if (config->pin_reset >= 0) {
        ESP_LOGD(TAG, "Resetting camera");
        conf.pin_bit_mask = 1LL << config->pin_reset;
        conf.mode         = GPIO_MODE_OUTPUT;
        gpio_config(&conf);

        gpio_set_level((gpio_num_t)config->pin_reset, 0);
        vTaskDelay(10 / portTICK_PERIOD_MS);
        gpio_set_level((gpio_num_t)config->pin_reset, 1);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    ESP_LOGD(TAG, "Searching for camera address");
    vTaskDelay(10 / portTICK_PERIOD_MS);

    // probe each known sensor until a supported camera is detected
    for (camera_model_id = 0; *out_camera_model == CAMERA_NONE && camera_model_id < CAMERA_MODEL_MAX;
        camera_model_id++) {
        slv_addr = camera_sensor[camera_model_id].sccb_addr;

        if (ESP_OK != SCCB_Probe(slv_addr)) {
            continue;
        }

        s_state->sensor.slv_addr     = slv_addr;
        s_state->sensor.xclk_freq_hz = config->xclk_freq_hz;

        // Read sensor ID and then initialize sensor
        // Attention: Some sensors have the same SCCB address
        // Therefore, several attempts may be made in the detection process
        sensor_id_t* id = &s_state->sensor.id;
        for (size_t i = 0; i < sizeof(g_sensors) / sizeof(sensor_func_t); i++) {
            if (g_sensors[i].detect(slv_addr, id)) {
                ESP_LOGI(
                    TAG, "Camera PID=0x%02x VER=0x%02x MIDL=0x%02x MIDH=0x%02x", id->PID, id->VER, id->MIDH, id->MIDL);
                camera_sensor_info_t* info = cam_sensor_get_info(id);
                if (NULL != info) {
                    *out_camera_model = info->model;
                    ESP_LOGI(TAG, "Detected %s camera", info->name);
                    g_sensors[i].init(&s_state->sensor);
                    break;
                }
            }
        }
    }

    if (CAMERA_NONE == *out_camera_model) {
        // no supported sensors are detected
        ESP_LOGE(TAG, "Detected camera not supported.");
        ret = ESP_ERR_NOT_SUPPORTED;
        goto err;
    }

    ESP_LOGI(TAG, "Detected camera at address=0x%02x", slv_addr);

    ESP_LOGD(TAG, "Doing SW reset of sensor");
    vTaskDelay(10 / portTICK_PERIOD_MS);

    return s_state->sensor.reset(&s_state->sensor);
err:
    CAMERA_DISABLE_OUT_CLOCK();
    return ret;
}

esp_err_t cam_init(const camera_config_t* config) {
    esp_err_t err;

    framesize_t frame_size;
    pixformat_t pix_format;

    s_saved_config = *config;
    err            = cam_hal_init(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return err;
    }

    camera_model_t camera_model = CAMERA_NONE;
    err                         = camera_probe(config, &camera_model);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera probe failed with error 0x%x(%s)", err, esp_err_to_name(err));
        goto fail;
    }

    frame_size = (framesize_t)config->frame_size;
    pix_format = (pixformat_t)config->pixel_format;

    if (PIXFORMAT_JPEG == pix_format && (!camera_sensor[camera_model].support_jpeg)) {
        ESP_LOGE(TAG, "JPEG format is not supported on this sensor");
        err = ESP_ERR_NOT_SUPPORTED;
        goto fail;
    }

    if (frame_size > camera_sensor[camera_model].max_size) {
        ESP_LOGW(TAG,
            "The frame size exceeds the maximum for this sensor, it will be forced to the "
            "maximum possible value");
        frame_size = camera_sensor[camera_model].max_size;
    }

    err = cam_config(config, frame_size, s_state->sensor.id.PID);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera config failed with error 0x%x", err);
        goto fail;
    }

    s_state->sensor.status.framesize = frame_size;
    s_state->sensor.pixformat        = pix_format;

    ESP_LOGD(TAG, "Setting frame size to %dx%d", resolution[frame_size].width, resolution[frame_size].height);
    if (s_state->sensor.set_framesize(&s_state->sensor, frame_size) != 0) {
        ESP_LOGE(TAG, "Failed to set frame size");
        err = ESP_ERR_CAMERA_FAILED_TO_SET_FRAME_SIZE;
        goto fail;
    }
    s_state->sensor.set_pixformat(&s_state->sensor, pix_format);

    if (s_state->sensor.id.PID == OV2640_PID) {
        s_state->sensor.set_gainceiling(&s_state->sensor, GAINCEILING_2X);
        s_state->sensor.set_bpc(&s_state->sensor, false);
        s_state->sensor.set_wpc(&s_state->sensor, true);
        s_state->sensor.set_lenc(&s_state->sensor, true);
    }

    if (pix_format == PIXFORMAT_JPEG) {
        s_state->sensor.set_quality(&s_state->sensor, config->jpeg_quality);
    }
    s_state->sensor.init_status(&s_state->sensor);

    cam_start();

    return ESP_OK;

fail:
    cam_deinit();
    return err;
}

esp_err_t cam_deinit() {
    esp_err_t ret = cam_hal_deinit();
    CAMERA_DISABLE_OUT_CLOCK();
    if (s_state) {
        SCCB_Deinit();

        free(s_state);
        s_state = NULL;
    }

    return ret;
}

#define FB_GET_TIMEOUT (4000 / portTICK_PERIOD_MS)

camera_fb_t* cam_fb_get() {
    if (s_state == NULL) {
        return NULL;
    }
    camera_fb_t* fb = cam_take(FB_GET_TIMEOUT);
    // set the frame properties
    if (fb) {
        fb->width  = resolution[s_state->sensor.status.framesize].width;
        fb->height = resolution[s_state->sensor.status.framesize].height;
        fb->format = s_state->sensor.pixformat;
    }
    return fb;
}

void cam_fb_return(camera_fb_t* fb) {
    if (s_state == NULL) {
        return;
    }
    cam_give(fb);
}

sensor_t* cam_sensor_get() {
    if (s_state == NULL) {
        return NULL;
    }
    return &s_state->sensor;
}

// because now I can modify the driver and nobody can stop me
framesize_t cam_get_framesize() {
    if (s_state == NULL) {
        return FRAMESIZE_INVALID;
    }
    return s_state->sensor.status.framesize;
}

void cam_return_all(void) {
    if (s_state == NULL) {
        return;
    }
    cam_give_all();
}

bool cam_available_frames(void) {
    if (s_state == NULL) {
        return false;
    }
    return cam_get_available_frames();
}

esp_err_t cam_reconfigure(const camera_config_t* config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_state) {
        esp_err_t err = cam_deinit();
        if (err != ESP_OK) {
            return err;
        }
    }
    s_saved_config = *config;
    return cam_init(&s_saved_config);
}

esp_err_t cam_set_raw_framesize(framesize_t framesize) {
    if (s_state == NULL) return ESP_ERR_INVALID_STATE;

    // pause DMA before touching sensor registers
    cam_stop();

    // reprogram sensor over I2C first (while DMA is paused)
    if (s_state->sensor.set_framesize(&s_state->sensor, framesize) != 0) {
        ESP_LOGE(TAG, "Failed to set sensor frame size");
        cam_start();
        return ESP_ERR_CAMERA_FAILED_TO_SET_FRAME_SIZE;
    }

    // reconfigure DMA descriptors to match the new geometry
    return cam_reconfigure_raw(framesize);
}

esp_err_t cam_set_color_gains(uint8_t red, uint8_t green, uint8_t blue) {
    if (s_state == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    sensor_t* s = &s_state->sensor;

    if (s->id.PID == OV2640_PID) {
        s->set_reg(s, 0x00FF, 0xFF, 0x00);
        s->set_reg(s, 0x00C7, 0x40, 0x40);  // lock manual gains
        s->set_reg(s, 0x00CC, 0xFF, red);
        s->set_reg(s, 0x00CD, 0xFF, green);
        s->set_reg(s, 0x00CE, 0xFF, blue);
        return ESP_OK;
    }

    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t cam_set_psram_mode(bool enable) {
    cam_hal_set_psram_mode(enable);
    if (!s_state) {
        return ESP_ERR_INVALID_STATE;
    }
    return cam_reconfigure(&s_saved_config);
}

bool cam_get_psram_mode(void) { return cam_hal_get_psram_mode(); }
