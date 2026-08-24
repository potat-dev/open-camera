#include <LovyanGFX.hpp>

#include "Arduino.h"
#include "esp_camera.h"
#include "esp_timer.h"
#include "fb_gfx.h"
#include "img_converters.h"
#include "soc/rtc_cntl_reg.h"  //disable brownout problems
#include "soc/soc.h"           //disable brownout problems

// #include "dl_lib.h"

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

#define CLOCK_FREQUENCY 20000000

#define TFT_SCK  39  // SCL on the board's silkscreen
#define TFT_MOSI 40  // SDA on the board's silkscreen
#define TFT_DC   41
#define TFT_CS   42

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI _bus_instance;

   public:
    LGFX(void) {
        auto cfg = _bus_instance.config();
        cfg.spi_host = SPI2_HOST;
        cfg.spi_mode = 0;
        cfg.freq_write = 80000000;  // 40MHz -- safe default for jumper wires (maybe 60-80MHz)
        // cfg.freq_write = 1000000;  // 40MHz -- safe default for jumper wires (maybe 60-80MHz)
        cfg.freq_read = 16000000;
        // cfg.freq_read = 1000000;
        cfg.spi_3wire = true;  // no MISO on this board
        cfg.use_lock = true;
        cfg.dma_channel = SPI_DMA_CH_AUTO;  // <-- this is what enables DMA transfers
        cfg.pin_sclk = TFT_SCK;
        cfg.pin_mosi = TFT_MOSI;
        cfg.pin_miso = -1;  // not connected
        cfg.pin_dc = TFT_DC;
        _bus_instance.config(cfg);
        _panel_instance.setBus(&_bus_instance);

        auto pcfg = _panel_instance.config();
        pcfg.pin_cs = TFT_CS;
        pcfg.pin_rst = -1;
        pcfg.pin_busy = -1;
        pcfg.panel_width = 240;  // physical panel is 240 wide x 320 tall (portrait)
        pcfg.panel_height = 320;
        pcfg.offset_rotation = 0;
        pcfg.invert = true;
        _panel_instance.config(pcfg);

        setPanel(&_panel_instance);
    }
};

LGFX lcd;

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(false);

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

    config.pin_xclk = NOT_CONNECTED;  // sensor has its own oscillator
    config.pin_pwdn = NOT_CONNECTED;
    config.pin_reset = NOT_CONNECTED;

    config.pin_pclk = PCLK_GPIO;
    config.pin_href = HREF_GPIO;
    config.pin_vsync = VSYNC_GPIO;
    config.pin_sccb_sda = SIOD_GPIO;
    config.pin_sccb_scl = SIOC_GPIO;

    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = FRAMESIZE_QVGA;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.xclk_freq_hz = CLOCK_FREQUENCY;

    if (esp_camera_init(&config) != ESP_OK) {
        Serial.println("Camera init failed -- check wiring");
        return;
    }

    lcd.init();
    lcd.setRotation(1);  // landscape -- matches the camera's 320x240 orientation
    // lcd.setSwapBytes(false);  // esp32-camera's RGB565 byte order commonly needs this
    // lcd.setTextDatum(lgfx::middle_center);
    // lcd.setTextColor(TFT_WHITE);
    lcd.setTextSize(6);  // large, blocky digits -- fits the retro look
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextColor(TFT_WHITE);
    // lcd.fillScreen(TFT_BLACK);

    Serial.println("Streaming to lcd...");
}

// void loop() {
//     camera_fb_t* fb = esp_camera_fb_get();
//     if (!fb) return;

//     uint16_t* px = (uint16_t*)fb->buf;

//     // Retro viewfinder frame: semi-transparent border + corner "dots".
//     // blend_rect(px, fb->width, fb->height, 10, 10, fb->width - 20, fb->height - 20);
//     // blend_circle(px, fb->width, fb->height, 20, 20, 8);
//     // blend_circle(px, fb->width, fb->height, fb->width - 20, 20, 8);

//     lcd.pushImage(0, 0, fb->width, fb->height, px);
//     esp_camera_fb_return(fb);

//     // Countdown, redrawn full-opacity on top of the live frame every loop --
//     // adjust the trigger/reset logic here to whatever your shutter flow needs.
//     static uint32_t last_tick = 0;
//     static int count = 10;
//     if (millis() - last_tick > 1000) {
//         last_tick = millis();
//         count = (count <= 0) ? 10 : count - 1;
//     }
//     lcd.drawNumber(count, lcd.width() / 2, lcd.height() / 2);
// }

uint32_t count = ~0;
void loop(void) {
    lcd.startWrite();
    //   lcd.setRotation(++count & 7);
    //   lcd.setColorDepth((count & 8) ? 16 : 24);
    lcd.fillScreen(TFT_BLACK);

    lcd.drawNumber(++count, 100, 100);

    // lcd.setTextColor(0xFF0000U);
    // lcd.drawString("R", 30, 16);
    // lcd.setTextColor(0x00FF00U);
    // lcd.drawString("G", 40, 16);
    // lcd.setTextColor(0x0000FFU);
    // lcd.drawString("B", 50, 16);

    // lcd.drawRect(30, 30, lcd.width() - 60, lcd.height() - 60, count * 7);
    // lcd.drawFastHLine(0, 0, 10);

    lcd.endWrite();
    delay(100);
}

// ---

// operating modes

// struct cam_mode_t {
//     framesize_t framesize;
//     pixformat_t pixformat;
//     // pixformat can be RGB565 or YUV422, or even JPEG
//     // but only in some manual override mode when user applies no circuit bending

//     size_t fb_count;
//     uint8_t quality;
//     // for frame2jpg conversion in RGB565 or YUV422 mode
//     // or for native JPEG quality in JPEG pixformat

//     bool operator==(const cam_mode_t&) const = default;
// };

// static cam_mode_t MODE_STREAM_MEDIUM = {FRAMESIZE_QVGA, PIXFORMAT_RGB565, 2, 80};
// static cam_mode_t MODE_STREAM_HIGH = {FRAMESIZE_HVGA, PIXFORMAT_RGB565, 2, 80};
// static cam_mode_t MODE_PHOTO_MEDIUM = {FRAMESIZE_SXGA, PIXFORMAT_RGB565, 1, 85};
// static cam_mode_t MODE_PHOTO_HIGH = {FRAMESIZE_UXGA, PIXFORMAT_RGB565, 1, 85};

// static cam_mode_t MODE_NONE = {FRAMESIZE_INVALID, PIXFORMAT_RAW, 0, 0};

// static cam_mode_t current_mode = MODE_NONE;

// camera configuration

// static camera_config_t build_config(const cam_mode_t& mode) {
//     camera_config_t config = {};

//     config.ledc_channel = LEDC_CHANNEL_0;
//     config.ledc_timer = LEDC_TIMER_0;

//     config.pin_d0 = D0_GPIO;
//     config.pin_d1 = D1_GPIO;
//     config.pin_d2 = D2_GPIO;
//     config.pin_d3 = D3_GPIO;
//     config.pin_d4 = D4_GPIO;
//     config.pin_d5 = D5_GPIO;
//     config.pin_d6 = D6_GPIO;
//     config.pin_d7 = D7_GPIO;

//     config.pin_xclk = NOT_CONNECTED;  // sensor has its own oscillator
//     config.pin_pwdn = NOT_CONNECTED;
//     config.pin_reset = NOT_CONNECTED;

//     config.pin_pclk = PCLK_GPIO;
//     config.pin_href = HREF_GPIO;
//     config.pin_vsync = VSYNC_GPIO;
//     config.pin_sccb_sda = SIOD_GPIO;
//     config.pin_sccb_scl = SIOC_GPIO;

//     config.pixel_format = mode.pixformat;
//     config.frame_size = mode.framesize;
//     config.fb_count = mode.fb_count;

//     config.grab_mode = CAMERA_GRAB_LATEST;
//     config.xclk_freq_hz = CLOCK_FREQUENCY;

//     return config;
// }

// static bool ensure_camera_mode(const cam_mode_t& mode) {
//     if (mode == current_mode) return true;
//     if (current_mode != MODE_NONE) esp_camera_deinit();

//     camera_config_t config = build_config(mode);
//     esp_err_t err = esp_camera_init(&config);

//     if (err != ESP_OK) {
//         Serial.printf("Camera reinit failed switching mode: 0x%x\n", err);
//         current_mode = MODE_NONE;
//         return false;
//     }

//     current_mode = mode;
//     return true;
// }

// static void log_psram(const char* label) {
//     Serial.printf("%s -- free PSRAM: %u bytes, largest free block: %u bytes\n", label,
//                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
//                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
// }

// --- Camera Feed Stream ---

// struct frame_result_t {
//     camera_fb_t* raw_data;
//     uint8_t* data;
//     size_t len;
// };

// static esp_err_t capture(frame_result_t& result, uint8_t quality) {
//     result.raw_data = esp_camera_fb_get();
//     result.data = NULL;
//     result.len = 0;

//     if (!result.raw_data) {
//         Serial.println("Camera capture failed");
//         return ESP_FAIL;
//     }

//     // TODO: needs to be done only if raw_data pixfmt is not JPEG
//     bool converted = frame2jpg(result.raw_data, quality, &result.data, &result.len);

//     esp_camera_fb_return(result.raw_data);
//     result.raw_data = NULL;

//     if (!converted) {
//         Serial.println("JPEG compression failed");
//         return ESP_FAIL;
//     }

//     return ESP_OK;
// }

// static void release_frame(frame_result_t& result) {
//     if (result.raw_data) {
//         esp_camera_fb_return(result.raw_data);
//     } else if (result.data) {
//         free(result.data);
//     }
// }

// static esp_err_t stream_handler(httpd_req_t* req) {
//     cam_mode_t* mode = (cam_mode_t*)req->user_ctx;

//     if (!acquire_camera(3000)) {
//         send_busy(req);
//         return ESP_FAIL;
//     }

//     if (!ensure_camera_mode(*mode)) {
//         release_camera();
//         httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera init failed");
//         return ESP_FAIL;
//     }

//     esp_err_t res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
//     if (res != ESP_OK) {
//         release_camera();
//         return res;
//     }

//     char part_buf[64];

//     while (true) {
//         frame_result_t frame;
//         res = capture(frame, mode->quality);

//         if (res == ESP_OK) {
//             size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, frame.len);
//             res = httpd_resp_send_chunk(req, part_buf, hlen);
//         }
//         if (res == ESP_OK) {
//             res = httpd_resp_send_chunk(req, (const char*)frame.data, frame.len);
//         }
//         if (res == ESP_OK) {
//             res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
//         }

//         release_frame(frame);
//         if (res != ESP_OK) break;
//     }

//     release_camera();
//     return res;
// }

// --- Single Photo Capture ---

// struct photo_buffer_ctx_t {
//     uint8_t* data;
//     size_t len;
//     size_t capacity;
// };

// static size_t append_photo_chunk(void* arg, size_t index, const void* data, size_t len) {
//     photo_buffer_ctx_t* ctx = (photo_buffer_ctx_t*)arg;
//     if (index == 0) ctx->len = 0;

//     if (ctx->len + len > ctx->capacity) {
//         size_t new_capacity = ctx->capacity ? ctx->capacity * 2 : 32768;
//         while (new_capacity < ctx->len + len) new_capacity *= 2;
//         uint8_t* new_data = (uint8_t*)heap_caps_realloc(ctx->data, new_capacity,
//         MALLOC_CAP_SPIRAM); if (!new_data) return 0; ctx->data = new_data; ctx->capacity =
//         new_capacity;
//     }

//     memcpy(ctx->data + ctx->len, data, len);
//     ctx->len += len;
//     return len;
// }

// static esp_err_t photo_handler(httpd_req_t* req) {
//     cam_mode_t* mode = (cam_mode_t*)req->user_ctx;

//     if (!acquire_camera(3000)) {
//         send_busy(req);
//         return ESP_FAIL;
//     }

//     if (!ensure_camera_mode(*mode)) {
//         release_camera();
//         httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera init failed");
//         return ESP_FAIL;
//     }

//     log_psram("before capture");

//     camera_fb_t* fb = esp_camera_fb_get();

//     if (!fb) {
//         release_camera();
//         Serial.println("Camera capture failed");
//         httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Capture failed");
//         return ESP_FAIL;
//     }

//     photo_buffer_ctx_t ctx = {NULL, 0, 0};
//     bool ok = frame2jpg_cb(fb, mode->quality, append_photo_chunk, &ctx);
//     esp_camera_fb_return(fb);

//     log_psram("after capture");

//     esp_err_t res;
//     if (ok && ctx.data) {
//         Serial.printf("JPEG size: %u bytes\n", (unsigned)ctx.len);
//         httpd_resp_set_type(req, "image/jpeg");
//         res = httpd_resp_send(req, (const char*)ctx.data, ctx.len);
//     } else {
//         Serial.println("JPEG encode/send failed");
//         httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Encode failed");
//         res = ESP_FAIL;
//     }

//     if (ctx.data) free(ctx.data);
//     log_psram("after send and cleanup");

//     release_camera();
//     return res;
// }

// void setup() {}

// void loop() {}
