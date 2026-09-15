#include <Arduino.h>
#include <EncButton.h>
#include <LittleFS.h>
#include <LovyanGFX.h>
#include <SD.h>
#include <SPI.h>
#include <esp_camera.h>
#include <esp_timer.h>
#include <fb_gfx.h>
#include <img_converters.h>

#include "config.h"
#include "menu_config.h"

// state

enum State {
    VIEWFINDER,
    SETTINGS,
    COUNTDOWN,
    PICTURE,
};

static State state = VIEWFINDER;

// TODO: extract into class
static uint8_t  countdown     = 0;
static uint32_t countdown_tmr = 0;

// TODO: extract into photo manager class
static uint16_t nextPhotoIndex = 1;

// buttons

Button btnA(BTN_A);
Button btnB(BTN_B);
Button btnX(BTN_X);

const std::vector<Button*> buttons = {&btnA, &btnB, &btnX};

// display

class LGFX_Display : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 panel_instance;
    lgfx::Bus_SPI      bus_instance;

   public:
    LGFX_Display(void) {
        auto bus = bus_instance.config();

        bus.pin_dc   = DISPLAY_DC;
        bus.pin_sclk = SPI_SCK;
        bus.pin_mosi = SPI_MOSI;

        // should be NC and true because no MISO on this board
        // but we need to init MISO here for shared SPI bus
        bus.pin_miso  = SPI_MISO;
        bus.spi_3wire = false;

        bus.spi_mode    = 0;
        bus.use_lock    = true;
        bus.spi_host    = SPI_HOST;
        bus.dma_channel = SPI_DMA_CH_AUTO;  // enable DMA transfers
        bus.freq_write  = DISPLAY_FREQ_WRITE;
        bus.freq_read   = DISPLAY_FREQ_READ;

        bus_instance.config(bus);
        panel_instance.setBus(&bus_instance);

        auto panel = panel_instance.config();

        panel.pin_cs   = DISPLAY_CS;
        panel.pin_rst  = NOT_CONNECTED;
        panel.pin_busy = NOT_CONNECTED;

        panel.bus_shared = true;

        // physical panel is portrait but we use it as landscape
        panel.panel_width     = DISPLAY_HEIGHT;
        panel.panel_height    = DISPLAY_WIDTH;
        panel.offset_rotation = 1;     // landscape
        panel.invert          = true;  // invert colors

        panel_instance.config(panel);
        setPanel(&panel_instance);
    }
};

LGFX_Display display;
LGFX_Sprite  canvas(&display);

// camera

struct cam_mode_t {
    framesize_t framesize;
    pixformat_t pixformat;  // RGB565 or YUV422, never JPEG
    size_t      fb_count;

    bool operator==(const cam_mode_t&) const = default;
};

static cam_mode_t MODE_VIEWFINDER = {FRAMESIZE_QVGA, PIXFORMAT_RGB565, 2};
static cam_mode_t MODE_CAPTURE    = {FRAMESIZE_UXGA, PIXFORMAT_RGB565, 1};

static camera_config_t build_camera_config(const cam_mode_t& mode) {
    camera_config_t config = {};

    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;

    config.pin_d0 = CAMERA_D0;
    config.pin_d1 = CAMERA_D1;
    config.pin_d2 = CAMERA_D2;
    config.pin_d3 = CAMERA_D3;
    config.pin_d4 = CAMERA_D4;
    config.pin_d5 = CAMERA_D5;
    config.pin_d6 = CAMERA_D6;
    config.pin_d7 = CAMERA_D7;

    config.pin_xclk  = NOT_CONNECTED;  // sensor has its own oscillator
    config.pin_pwdn  = NOT_CONNECTED;
    config.pin_reset = NOT_CONNECTED;

    config.pin_pclk     = CAMERA_PCLK;
    config.pin_href     = CAMERA_HREF;
    config.pin_vsync    = CAMERA_VSYNC;
    config.pin_sccb_sda = CAMERA_SDA;
    config.pin_sccb_scl = CAMERA_SCL;

    config.pixel_format = mode.pixformat;
    config.frame_size   = mode.framesize;
    config.fb_count     = mode.fb_count;

    config.grab_mode    = CAMERA_GRAB_LATEST;
    config.xclk_freq_hz = CAMERA_PCLK_FREQ;

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

bool camera_init(const cam_mode_t& mode = MODE_VIEWFINDER) {
    esp_camera_deinit();

    camera_config_t config = build_camera_config(mode);
    if (esp_camera_init(&config) != ESP_OK) {
        Serial.println("Error: Camera initialization failed");
        return false;
    }

    configure_camera();
    return true;
}

// SD card

void findNextPhotoIndex() {
    char path[32];
    while (nextPhotoIndex < 10000) {
        snprintf(path, sizeof(path), "/pic_%04d.jpg", nextPhotoIndex);
        if (!SD.exists(path)) {
            break;
        }
        nextPhotoIndex++;
    }
    Serial.printf("Next photo will be: /pic_%04d.jpg\n", nextPhotoIndex);
}

bool mountSD(uint8_t max_attempts = 3, uint32_t retry_delay = 150) {
    display.waitDMA();
    digitalWrite(DISPLAY_CS, HIGH);
    delay(5);

    for (uint8_t attempt = 1; attempt <= max_attempts; attempt++) {
        SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, NOT_CONNECTED);
        if (SD.begin(SD_CS, SPI, SD_CARD_SPI_FREQ)) return true;
        SD.end();
        digitalWrite(SD_CS, HIGH);
        if (attempt < max_attempts) delay(retry_delay);
    }

    return false;
}

void unmountSD() {
    SD.end();
    digitalWrite(SD_CS, HIGH);
}

bool saveImage(const char* filename, const uint8_t* data, size_t size) {
    if (!mountSD()) {
        Serial.println("Error: Failed to mount SD card");
        return false;
    }

    File file = SD.open(filename, FILE_WRITE, true);
    if (!file) {
        Serial.println("Error: Failed to open file");
        unmountSD();
        return false;
    }

    size_t size_written = file.write(data, size);
    file.flush();
    file.close();

    unmountSD();
    return size_written == size;
}

struct photo_buffer {
    uint8_t* data;
    size_t   size;
    size_t   capacity;
};

static size_t append_photo_chunk(void* arg, size_t index, const void* data, size_t size) {
    photo_buffer* ctx = (photo_buffer*)arg;
    if (index == 0) ctx->size = 0;

    if (ctx->size + size > ctx->capacity) {
        // TODO: improve realloc sizing (capacity * 2 can be dangerous)
        size_t new_capacity = ctx->capacity ? ctx->capacity * 2 : 32768;
        while (new_capacity < ctx->size + size) new_capacity *= 2;
        uint8_t* new_data = (uint8_t*)heap_caps_realloc(ctx->data, new_capacity, MALLOC_CAP_SPIRAM);
        if (!new_data) return 0;
        ctx->data     = new_data;
        ctx->capacity = new_capacity;
    }

    memcpy(ctx->data + ctx->size, data, size);
    ctx->size += size;
    return size;
}

void capture() {
    bool         ok;
    char         path[32];
    camera_fb_t* raw_data;
    photo_buffer ctx = {NULL, 0, 0};

    ESP_LOGI("capture", "Capture start");

    // TODO: also capture and save viewfinder frame with some .raw ext
    ok = camera_init(MODE_CAPTURE);
    if (!ok) {
        Serial.println("Camera init failed");
        return;
    }

    // drop 2 dummy frames
    // TODO: probably need more (needs testing)
    // seems like it does not help
    delay(5);
    raw_data = esp_camera_fb_get();
    if (raw_data) esp_camera_fb_return(raw_data);
    delay(5);
    raw_data = esp_camera_fb_get();
    if (raw_data) esp_camera_fb_return(raw_data);
    delay(5);

    raw_data = esp_camera_fb_get();
    if (!raw_data) {
        Serial.println("Capture failed");
        return;
    }

    ESP_LOGI("capture", "RAW capture done");

    ok = frame2jpg_cb(raw_data, CAMERA_JPEG_QUALITY, append_photo_chunk, &ctx);
    esp_camera_fb_return(raw_data);

    if (!ok || !ctx.data) {
        Serial.println("JPEG compression failed");
        if (ctx.data) free(ctx.data);
        return;
    }

    ESP_LOGI("capture", "JPEG compression done");

    snprintf(path, sizeof(path), "/pic_%04d.jpg", nextPhotoIndex++);
    ok = saveImage(path, ctx.data, ctx.size);
    free(ctx.data);

    if (!ok) {
        Serial.println("Save failed");
        return;
    }

    ESP_LOGI("capture", "Save done");
}

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);

    delay(1500);  // stabilize SD card

    // setup SPI pins
    pinMode(DISPLAY_CS, OUTPUT);
    digitalWrite(DISPLAY_CS, HIGH);

    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    gpio_set_drive_capability((gpio_num_t)SD_CS, GPIO_DRIVE_CAP_3);
    pinMode(SPI_MISO, INPUT_PULLUP);

    if (mountSD()) {
        Serial.printf("SD Card detected. Size: %llu MB\n", SD.cardSize() / (1024 * 1024));
        findNextPhotoIndex();
        unmountSD();
    } else {
        Serial.println("Error: Failed to mount SD card");
        // TODO: reboot
    }

    camera_init(MODE_VIEWFINDER);

    display.init();
    display.setSwapBytes(false);

    canvas.setPsram(true);
    canvas.setColorDepth(16);
    canvas.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

void drawFrame() {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Capture failed");
        return;
    }

    canvas.pushImage(0, 0, fb->width, fb->height, (uint16_t*)fb->buf);
    esp_camera_fb_return(fb);
}

void updateDisplay() { canvas.pushSprite(0, 0); }

void transitionTo(State next) {
    // exit action
    switch (state) {
        case SETTINGS:
            if (menu.changed()) configure_camera();
            break;

        case COUNTDOWN:
            countdown = 0;
            break;

        default:
            break;
    }

    state = next;

    // entry action
    switch (state) {
        case VIEWFINDER:
            camera_init(MODE_VIEWFINDER);
            break;

        case COUNTDOWN:
            countdown     = TICK_COUNT;
            countdown_tmr = millis() + TICK_TIME;
            break;

        case PICTURE:
            drawFrame();
            updateDisplay();
            capture();
            break;

        default:
            break;
    }
}

void handleViewfinder() {
    if (btnX.click()) {
        transitionTo(COUNTDOWN);
        return;
    }

    if (btnX.hold()) {
        transitionTo(SETTINGS);
        return;
    }

    // if (btnA.click()) rotateCCW();    // TODO: implement
    // if (btnB.click()) rotateCW();     // TODO: implement
    // if (btnA.hold()) flipScreen();    // TODO: implement (hFlip)
    // if (btnB.hold()) viewPictures();  // TODO: implement

    // or

    // if (btnA.click()) rotateScreen();  // TODO: implement
    // if (btnB.click()) viewPictures();  // TODO: implement
    // if (btnA.hold()) flipScreen();     // TODO: implement (hFlip)

    drawFrame();
    updateDisplay();
}

void drawMenu() { menu.draw(canvas, 16, menuScale.value); }

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

    drawFrame();
    drawMenu();
    updateDisplay();
}

void drawCountdown() {
    char buf[12];
    sprintf(buf, "Shot in %d", countdown);
    canvas.setTextSize(4);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.setTextDatum(lgfx::baseline_center);
    canvas.drawString(buf, 320 / 2, 240 - 24);
}

int8_t getCountdownInc(int8_t amount) {
    if (!amount) return 0;
    uint8_t inc = abs(amount);

    if (amount > 0) {
        if (countdown % inc) inc = inc * 2 - countdown % inc;
        return inc;
    } else {
        inc = countdown < inc ? countdown : inc + countdown % inc;
        return -inc;
    }
}

void updateCountdown(int8_t amount) {
    int16_t updated = countdown + getCountdownInc(amount);
    countdown       = constrain(updated, 0, TICK_COUNT_MAX);
    countdown_tmr   = millis() + TICK_TIME;
}

void handleCountdown() {
    if (btnX.click()) {
        transitionTo(VIEWFINDER);
        return;
    }

    if (btnA.step() || btnA.click()) updateCountdown(+5);
    if (btnB.step()) updateCountdown(-5);

    if (btnB.click()) {
        transitionTo(PICTURE);
        return;
    }

    if (countdown_tmr < millis()) {
        if (countdown && --countdown) {
            countdown_tmr = millis() + TICK_TIME;
        } else {
            transitionTo(PICTURE);
            return;
        }
    }

    drawFrame();
    drawCountdown();
    updateDisplay();
}

void handlePicture() {
    if (btnX.click()) transitionTo(VIEWFINDER);

    // if (btnX.hold()) deletePicture();  // TODO: implement
    // if (btnA.click()) nextPicture();   // TODO: implement
    // if (btnB.click()) prevPicture();   // TODO: implement

    // note: pressing prev:
    // pic_3 (current) -> pic_2 (prev) -> pic_1 -> pic_0
    // pressing next goes other direction and loops to pic_0

    // TODO: read from SD card and shrink to display size (generate thumbnail)
    // TODO: pre-load prev and next thumbnail and store in RAM
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
