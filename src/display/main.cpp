#include <EncButton.h>

#include <LovyanGFX.hpp>

#include "Arduino.h"
#include "esp_camera.h"
#include "esp_timer.h"
#include "fb_gfx.h"
#include "img_converters.h"

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
#define COUNTDOWN_TICK_TIME  750

enum State {
    VIEWFINDER,
    COUNTDOWN,
    PICTURE,
};

static State state = VIEWFINDER;

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI _bus_instance;

   public:
    LGFX(void) {
        auto bus = _bus_instance.config();

        bus.pin_dc = TFT_DC;
        bus.pin_sclk = TFT_SCK;
        bus.pin_mosi = TFT_MOSI;

        bus.pin_miso = NOT_CONNECTED;
        bus.spi_3wire = true;  // no MISO on this board

        bus.freq_write = 80000000;
        bus.freq_read = 16000000;

        bus.spi_mode = 0;
        bus.use_lock = true;
        bus.spi_host = SPI2_HOST;
        bus.dma_channel = SPI_DMA_CH_AUTO;  // enable DMA transfers

        _bus_instance.config(bus);
        _panel_instance.setBus(&_bus_instance);

        auto panel = _panel_instance.config();

        panel.pin_cs = TFT_CS;
        panel.pin_rst = NOT_CONNECTED;
        panel.pin_busy = NOT_CONNECTED;

        panel.invert = true;      // invert colors
        panel.panel_width = 240;  // physical panel is 240 wide x 320 tall (portrait)
        panel.panel_height = 320;
        panel.offset_rotation = 0;

        _panel_instance.config(panel);
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

    canvas.setTextSize(4);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.setTextDatum(lgfx::baseline_center);
}

void capture() {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Capture failed");
        return;
    }

    canvas.pushImage(0, 0, fb->width, fb->height, (uint16_t*)fb->buf);
    esp_camera_fb_return(fb);

    if (countdown > 0) {
        char buf[12];
        sprintf(buf, "Shot in %d", countdown);
        canvas.drawString(buf, 320 / 2, 240 - 24);
    }

    canvas.pushSprite(0, 0);
}

void loop() {
    btn.tick();

    switch (state) {
        case VIEWFINDER:
            if (btn.click()) {
                state = COUNTDOWN;
                countdown = COUNTDOWN_TICK_COUNT;
                countdown_tmr = millis() + COUNTDOWN_TICK_TIME;
                break;
            }

            capture();
            break;

        case COUNTDOWN:
            if (btn.click()) {
                state = VIEWFINDER;
                countdown = 0;
                break;
            }

            if (countdown && countdown_tmr < millis()) {
                if (--countdown) {
                    countdown_tmr += COUNTDOWN_TICK_TIME;
                } else {
                    state = PICTURE;
                }
            }

            capture();
            break;

        case PICTURE:
            if (btn.click()) state = VIEWFINDER;
            break;
    }
}
