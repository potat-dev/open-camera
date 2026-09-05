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

#define TFT_SCK  39  // SCL
#define TFT_MOSI 40  // SDA
#define TFT_DC   41
#define TFT_CS   42

#define S_BTN_GPIO 16  // shutter
#define A_BTN_GPIO 21  // up
#define B_BTN_GPIO 20  // down
#define X_BTN_GPIO 19  // select

#define COUNTDOWN_TICK_COUNT 3
#define COUNTDOWN_TICK_TIME  750

#define CLOCK_FREQUENCY 20000000

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

        // 80MHz is crazy
        // 40MHz is crazy
        // 20MHz is barely fine
        // 16MHz is great but very slow
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

Button btnS(S_BTN_GPIO);
Button btnA(A_BTN_GPIO);
Button btnB(B_BTN_GPIO);
Button btnX(X_BTN_GPIO);

const std::vector<Button*> buttons = {&btnS, &btnA, &btnB, &btnX};

const char* modeOptions[] = {"Auto", "Manual", "Expert"};

MenuItem contrast = {"Contr", MENU_INTEGER, 0, nullptr, 0, -2, 2, 1};
MenuItem brightness = {"Bright", MENU_INTEGER, 0, nullptr, 0, -2, 2, 1};
MenuItem saturation = {"Satur", MENU_INTEGER, 0, nullptr, 0, -2, 2, 1};
MenuItem sharpness = {"Sharp", MENU_INTEGER, 0, nullptr, 0, -2, 2, 1};

const char* wbOptions[] = {"Auto", "Sunny", "Cloudy", "Office", "Home"};
MenuItem whiteBalance = {"White", MENU_SELECT, 0, wbOptions, 5};

const char* effectOptions[] = {"None", "Invert", "Gray", "Red", "Green", "Blue", "Sepia"};
MenuItem effect = {"Effect", MENU_SELECT, 0, effectOptions, 7};

MenuItem hFlip = {"FlipH", MENU_TOGGLE, 0};
MenuItem vFlip = {"FlipV", MENU_TOGGLE, 0};

MenuItem expCtrl = {"ExpCtrl", MENU_TOGGLE, 1};
MenuItem gainCtrl = {"GainCtrl", MENU_TOGGLE, 1};
MenuItem colorBar = {"ColorBar", MENU_TOGGLE, 0};
MenuItem whiteBal = {"WhiteBal", MENU_TOGGLE, 1};
MenuItem gainAWB = {"GainAWB", MENU_TOGGLE, 1};
MenuItem rawGMA = {"RawGMA", MENU_TOGGLE, 1};
MenuItem aec2 = {"AEC2", MENU_TOGGLE, 0};
MenuItem lenc = {"LenC", MENU_TOGGLE, 1};
MenuItem bpc = {"BPC", MENU_TOGGLE, 0};
MenuItem wpc = {"WPC", MENU_TOGGLE, 1};
MenuItem dcw = {"DCW", MENU_TOGGLE, 1};

MenuItem menuScale = {"TextSize", MENU_INTEGER, 2, nullptr, 0, 2, 3, 1};

MenuItem* menuItems[] = {
    &contrast, &brightness, &saturation, &sharpness, &whiteBalance, &effect,    &hFlip,
    &vFlip,    &expCtrl,    &gainCtrl,   &colorBar,  &whiteBal,     &gainAWB,   &rawGMA,
    &aec2,     &lenc,       &bpc,        &wpc,       &dcw,          &menuScale,
};

Menu menu(menuItems, 20);

static uint8_t countdown = 0;
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

static void enable_sde_bits(sensor_t* s, uint8_t bits) {
    s->set_reg(s, 0xFF, 0xFF, 0x00);
    s->set_reg(s, 0x7C, 0xFF, 0x00);
    int current = s->get_reg(s, 0x7D, 0xFF);
    s->set_reg(s, 0x7D, 0xFF, current | bits);
}

static void configure_camera() {
    // set sensor configs
    sensor_t* s = esp_camera_sensor_get();

    // switches
    s->set_exposure_ctrl(s, expCtrl.value);
    s->set_gain_ctrl(s, gainCtrl.value);
    s->set_colorbar(s, colorBar.value);
    s->set_whitebal(s, whiteBal.value);
    s->set_awb_gain(s, gainAWB.value);
    s->set_raw_gma(s, rawGMA.value);
    s->set_aec2(s, aec2.value);
    s->set_lenc(s, lenc.value);
    s->set_bpc(s, bpc.value);
    s->set_wpc(s, wpc.value);
    s->set_dcw(s, dcw.value);

    // image flip
    s->set_hmirror(s, hFlip.value);
    s->set_vflip(s, vFlip.value);

    // values: -2 to 2
    s->set_contrast(s, contrast.value);
    s->set_brightness(s, brightness.value);
    s->set_saturation(s, saturation.value);
    s->set_sharpness(s, sharpness.value);
    // s->set_ae_level(s, 0);

    // // image correction values
    // s->set_gainceiling(s, GAINCEILING_2X);  // 2X to 128X
    // s->set_aec_value(s, 300);               // 0 to 1200
    // s->set_agc_gain(s, 0);                  // 0 to 30

    s->set_special_effect(s, effect.value);
    s->set_wb_mode(s, whiteBalance.value);

    enable_sde_bits(s, 0x07);
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

    // gpio_set_drive_capability((gpio_num_t)TFT_SCK, GPIO_DRIVE_CAP_0);
    // gpio_set_drive_capability((gpio_num_t)TFT_MOSI, GPIO_DRIVE_CAP_0);

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

void transitionTo(State next) {
    // exit action
    switch (state) {
        case SETTINGS:
            if (menu.changed()) configure_camera();
            break;

        case COUNTDOWN:
            countdown = 0;

        default:
            break;
    }

    state = next;

    // entry action
    switch (state) {
        case COUNTDOWN:
            countdown = COUNTDOWN_TICK_COUNT;
            countdown_tmr = millis() + COUNTDOWN_TICK_TIME;
            break;

        case PICTURE:
            capture();
            display();
            break;

        default:
            break;
    }
}

void handleViewfinder() {
    if (btnS.click()) {
        transitionTo(COUNTDOWN);
        return;
    }

    if (btnS.hold() || btnX.click()) {
        transitionTo(SETTINGS);
        return;
    }

    capture();
    display();
}

void handleSettings() {
    if (btnA.click() || btnA.step()) menu.upHandler();
    if (btnB.click() || btnB.step()) menu.downHandler();
    if (btnX.click()) menu.selectHandler();
    if (btnX.hold()) menu.backHandler();

    if (menu.wantsExit()) {
        transitionTo(VIEWFINDER);
        return;
    }

    if (menu.changed()) {
        configure_camera();
        return;
    }

    capture();
    menu.draw(canvas, 16, menuScale.value);
    display();
}

void drawCountdown() {
    char buf[12];
    sprintf(buf, "Shot in %d", countdown);
    canvas.setTextSize(4);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.setTextDatum(lgfx::baseline_center);
    canvas.drawString(buf, 320 / 2, 240 - 24);
}

void handleCountdown() {
    if (btnS.click()) {
        transitionTo(VIEWFINDER);
        return;
    }

    if (countdown && countdown_tmr < millis()) {
        if (--countdown) {
            countdown_tmr += COUNTDOWN_TICK_TIME;
        } else {
            transitionTo(PICTURE);
            return;
        }
    }

    capture();
    drawCountdown();
    display();
}

void handlePicture() {
    if (btnS.click()) transitionTo(VIEWFINDER);
}

void loop() {
    for (Button* btn : buttons) btn->tick();

    switch (state) {
        case VIEWFINDER:
            handleViewfinder();
            break;

        case SETTINGS:
            handleSettings();
            break;

        case COUNTDOWN:
            handleCountdown();
            break;

        case PICTURE:
            handlePicture();
            break;
    }
}
