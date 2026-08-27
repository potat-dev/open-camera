#include <EncButton.h>
#include <LovyanGFX.h>
#include <Menu.h>

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
    SETTINGS,
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

const char* modeOptions[] = {"Auto", "Manual", "Expert"};

MenuItem menuItems[5] = {
    {"Demo", MENU_TOGGLE, 0},
    {"Mode", MENU_SELECT, 0, modeOptions, 3},
    {"Value", MENU_INTEGER, 50, nullptr, 0, 0, 100, 5},
    {"Bright", MENU_INTEGER, 0, nullptr, 0, -2, 2, 1},
    {"Exit", MENU_EXIT},
};

Menu menu(menuItems, 5);

static bool shot = false;
static int8_t countdown = -1;
static uint32_t countdown_tmr = 0;

static camera_config_t get_camera_config() {
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

    return config;
}

static void configure_camera() {
    // set sensor configs
    sensor_t* s = esp_camera_sensor_get();

    // switches
    // s->set_exposure_ctrl(s, 1);
    // s->set_gain_ctrl(s, 1);
    // s->set_whitebal(s, 1);
    // s->set_awb_gain(s, 1);
    // s->set_colorbar(s, 0);
    // s->set_raw_gma(s, 1);
    // s->set_aec2(s, 0);
    // s->set_lenc(s, 1);
    // s->set_bpc(s, 0);
    // s->set_wpc(s, 1);
    // s->set_dcw(s, 1);

    // // image flip
    // s->set_hmirror(s, 1);
    // s->set_vflip(s, 1);

    // // values: -2 to 2
    s->set_brightness(s, menuItems[3].value);
    // s->set_saturation(s, 0);
    // s->set_contrast(s, 0);
    // s->set_ae_level(s, 0);

    // // image correction values
    // s->set_gainceiling(s, GAINCEILING_2X);  // 2X to 128X
    // s->set_aec_value(s, 300);               // 0 to 1200
    // s->set_agc_gain(s, 0);                  // 0 to 30

    // // effects: 0 to 6
    // // None, Negative, Gray, Red Tint, Green Tint, Blue Tint, Sepia
    // s->set_special_effect(s, 0);

    // // white balance: 0 to 4
    // // Auto, Sunny, Cloudy, Office, Home
    // s->set_wb_mode(s, 0);
}

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(false);

    camera_config_t config = get_camera_config();
    if (esp_camera_init(&config) != ESP_OK) {
        Serial.println("Camera init failed -- check wiring");
        return;
    }
    configure_camera();

    lcd.init();

    lcd.setRotation(1);       // landscape
    lcd.setSwapBytes(false);  // RGB565 byte order

    canvas.setPsram(true);
    canvas.setColorDepth(16);
    canvas.createSprite(320, 240);

    canvas.setTextSize(4);
}

void capture() {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Capture failed");
        return;
    }

    canvas.pushImage(0, 0, fb->width, fb->height, (uint16_t*)fb->buf);
    esp_camera_fb_return(fb);
}

void display() { canvas.pushSprite(0, 0); }

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

            if (btn.hold()) {
                state = SETTINGS;
                break;
            }

            capture();
            display();
            break;

        case SETTINGS:
            if (btn.click()) menu.clickHandler();
            if (btn.hold()) menu.holdHandler();

            if (menu.wantsExit()) {
                state = VIEWFINDER;
                break;
            }

            if (menu.changed()) configure_camera();

            capture();
            menu.draw(canvas, 16, 3);
            display();
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

            if (countdown > 0) {
                char buf[12];
                sprintf(buf, "Shot in %d", countdown);
                canvas.setTextSize(4);
                canvas.setTextColor(TFT_WHITE, TFT_BLACK);
                canvas.setTextDatum(lgfx::baseline_center);
                canvas.drawString(buf, 320 / 2, 240 - 24);
            }

            display();
            break;

        case PICTURE:
            if (btn.click()) state = VIEWFINDER;
            break;
    }
}
