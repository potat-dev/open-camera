#include "Arduino.h"
#include "WiFi.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "fb_gfx.h"
#include "img_converters.h"
#include "soc/rtc_cntl_reg.h"  //disable brownout problems
#include "soc/soc.h"           //disable brownout problems

// #include "dl_lib.h"

#include "secrets.h"

#define NOT_CONNECTED -1

#define SIOD_GPIO  4
#define SIOC_GPIO  5
#define VSYNC_GPIO 6
#define HREF_GPIO  7
#define PCLK_GPIO  15

#define D0_GPIO 1
#define D1_GPIO 2
#define D2_GPIO 9
#define D3_GPIO 10
#define D4_GPIO 11
#define D5_GPIO 12
#define D6_GPIO 13
#define D7_GPIO 14

#define PIXEL_FORMAT PIXFORMAT_RGB565

#define FRAME_BUFFER_COUNT 2

#define CLOCK_FREQUENCY 20000000

#define PART_BOUNDARY "123456789000000000000987654321"

static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t camera_server = NULL;

static SemaphoreHandle_t camera_mutex;

struct cam_mode_t {
    framesize_t framesize;
    pixformat_t pixformat;  // can be RGB565 or YUV422, never JPEG

    bool operator==(const cam_mode_t&) const = default;
};

static cam_mode_t MODE_STREAM_QVGA = {FRAMESIZE_QVGA, PIXFORMAT_RGB565};
static cam_mode_t MODE_STREAM_HVGA = {FRAMESIZE_HVGA, PIXFORMAT_RGB565};
static cam_mode_t MODE_PHOTO_SXGA = {FRAMESIZE_SXGA, PIXFORMAT_RGB565};
static cam_mode_t MODE_PHOTO_UXGA = {FRAMESIZE_UXGA, PIXFORMAT_RGB565};
static cam_mode_t MODE_NONE = {FRAMESIZE_INVALID, PIXFORMAT_RAW};

static cam_mode_t current_mode = MODE_NONE;

static camera_config_t build_config(const cam_mode_t& mode) {
    camera_config_t config = {};

    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;

    config.pin_d0 = D0_GPIO;
    config.pin_d1 = D1_GPIO;
    config.pin_d2 = D2_GPIO;
    config.pin_d3 = D3_GPIO;
    config.pin_d4 = D4_GPIO;
    config.pin_d5 = D5_GPIO;
    config.pin_d6 = D6_GPIO;
    config.pin_d7 = D7_GPIO;

    config.pin_xclk = NOT_CONNECTED;
    config.pin_pwdn = NOT_CONNECTED;
    config.pin_reset = NOT_CONNECTED;

    config.pin_pclk = PCLK_GPIO;
    config.pin_href = HREF_GPIO;
    config.pin_vsync = VSYNC_GPIO;
    config.pin_sccb_sda = SIOD_GPIO;
    config.pin_sccb_scl = SIOC_GPIO;

    config.pixel_format = mode.pixformat;
    config.frame_size = mode.framesize;

    config.fb_count = FRAME_BUFFER_COUNT;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.xclk_freq_hz = CLOCK_FREQUENCY;

    return config;
}

static bool ensure_camera_mode(const cam_mode_t& mode) {
    if (mode == current_mode) return true;
    esp_camera_deinit();

    camera_config_t config = build_config(mode);
    esp_err_t err = esp_camera_init(&config);

    if (err != ESP_OK) {
        Serial.printf("Camera reinit failed switching mode: 0x%x\n", err);
        current_mode = MODE_NONE;
        return false;
    }

    current_mode = mode;
    return true;
}

static bool acquire_camera(TickType_t timeout_ms) {
    return xSemaphoreTake(camera_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static void release_camera() { xSemaphoreGive(camera_mutex); }

static void send_busy(httpd_req_t* req) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_send(req, "Camera busy, try again", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t stream_handler(httpd_req_t* req) {
    cam_mode_t* mode = (cam_mode_t*)req->user_ctx;

    if (!acquire_camera(3000)) {
        send_busy(req);
        return ESP_FAIL;
    }
    if (!ensure_camera_mode(*mode)) {
        release_camera();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera init failed");
        return ESP_FAIL;
    }

    esp_err_t res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        release_camera();
        return res;
    }

    char part_buf[64];

    while (true) {
        camera_fb_t* fb = esp_camera_fb_get();
        uint8_t* jpg_buf = NULL;
        size_t jpg_buf_len = 0;

        if (!fb) {
            Serial.println("Camera capture failed");
            res = ESP_FAIL;
        } else if (fb->format != PIXFORMAT_JPEG) {
            bool converted = frame2jpg(fb, 80, &jpg_buf, &jpg_buf_len);
            esp_camera_fb_return(fb);
            fb = NULL;
            if (!converted) {
                Serial.println("JPEG compression failed");
                res = ESP_FAIL;
            }
        } else {
            jpg_buf_len = fb->len;
            jpg_buf = fb->buf;
        }

        if (res == ESP_OK) {
            size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, jpg_buf_len);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char*)jpg_buf, jpg_buf_len);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }

        if (fb) {
            esp_camera_fb_return(fb);
        } else if (jpg_buf) {
            free(jpg_buf);
        }

        if (res != ESP_OK) {
            break;
        }
    }

    release_camera();
    return res;
}

static esp_err_t photo_handler(httpd_req_t* req) {
    cam_mode_t* mode = (cam_mode_t*)req->user_ctx;

    if (!acquire_camera(3000)) {
        send_busy(req);
        return ESP_FAIL;
    }
    if (!ensure_camera_mode(*mode)) {
        release_camera();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera init failed");
        return ESP_FAIL;
    }

    esp_err_t res = httpd_resp_set_type(req, "image/jpeg");
    if (res != ESP_OK) {
        release_camera();
        return res;
    }

    camera_fb_t* fb = esp_camera_fb_get();
    uint8_t* jpg_buf = NULL;
    size_t jpg_buf_len = 0;

    if (!fb) {
        release_camera();
        Serial.println("Camera capture failed");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Capture failed");
        res = ESP_FAIL;
    } else if (fb->format != PIXFORMAT_JPEG) {
        bool converted = frame2jpg(fb, 80, &jpg_buf, &jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if (!converted) {
            Serial.println("JPEG compression failed");
            res = ESP_FAIL;
        }
    } else {
        jpg_buf_len = fb->len;
        jpg_buf = fb->buf;
    }

    if (res == ESP_OK) {
        res = httpd_resp_send(req, (const char*)jpg_buf, jpg_buf_len);
    }

    if (fb) {
        esp_camera_fb_return(fb);
    } else if (jpg_buf) {
        free(jpg_buf);
    }

    release_camera();
    return res;
}

void startNetwork() {
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("WiFi connected");

    Serial.print("Camera ready at: http://");
    Serial.println(WiFi.localIP());
}

void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.server_port = 80;
    config.max_uri_handlers = 8;

    if (httpd_start(&camera_server, &config) != ESP_OK) {
        Serial.println("Failed to start HTTP server");
        return;
    }

    httpd_uri_t stream_uri = {"/stream", HTTP_GET, stream_handler, &MODE_STREAM_QVGA};
    httpd_uri_t stream_hd_uri = {"/stream_hd", HTTP_GET, stream_handler, &MODE_STREAM_HVGA};
    httpd_uri_t photo_uri = {"/photo", HTTP_GET, photo_handler, &MODE_PHOTO_SXGA};
    httpd_uri_t photo_hd_uri = {"/photo_hd", HTTP_GET, photo_handler, &MODE_PHOTO_UXGA};

    httpd_register_uri_handler(camera_server, &stream_uri);
    httpd_register_uri_handler(camera_server, &stream_hd_uri);
    httpd_register_uri_handler(camera_server, &photo_uri);
    httpd_register_uri_handler(camera_server, &photo_hd_uri);

    Serial.println("Camera server started");
}

void setup() {
    // WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector

    Serial.begin(115200);
    Serial.setDebugOutput(false);

    camera_mutex = xSemaphoreCreateMutex();

    if (!ensure_camera_mode(MODE_STREAM_QVGA)) {
        Serial.println("Initial camera init failed -- check wiring");
        return;
    }

    startNetwork();
    startCameraServer();
}

void loop() { delay(1); }
