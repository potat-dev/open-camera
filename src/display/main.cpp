#include <EncButton.h>

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

#define TFT_SCK  39  // SCL
#define TFT_MOSI 40  // SDA
#define TFT_DC   41
#define TFT_CS   42

#define BTN_GPIO 16

#define COUNTDOWN_TICK_COUNT 3
#define COUNTDOWN_TICK_TIME  500

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
LGFX_Sprite canvas(&lcd);

Button btn(BTN_GPIO);

static bool shot = false;
static int8_t countdown = -1;
static uint32_t countdown_tmr = 0;

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

    lcd.setRotation(1);       // landscape
    lcd.setSwapBytes(false);  // RGB565 byte order

    canvas.setPsram(true);
    canvas.setColorDepth(16);
    canvas.createSprite(320, 240);

    canvas.setTextSize(12);  // large blocky digits
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextDatum(lgfx::middle_center);
}

void loop() {
    btn.tick();

    if (btn.click()) {
        if (shot) {
            shot = false;
        } else {
            countdown = COUNTDOWN_TICK_COUNT;
            countdown_tmr = millis() + COUNTDOWN_TICK_TIME;
        }
    }

    if (countdown > 0 && countdown_tmr < millis()) {
        countdown_tmr += COUNTDOWN_TICK_TIME;
        countdown -= 1;
    }

    if (shot) return;

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Capture failed");
        return;
    }

    canvas.pushImage(0, 0, fb->width, fb->height, (uint16_t*)fb->buf);

    esp_camera_fb_return(fb);

    if (countdown > 0) canvas.drawNumber(countdown, 320 / 2, 240 / 2);

    canvas.pushSprite(0, 0);

    if (countdown == 0) {
        countdown = -1;
        shot = true;
    };
}
