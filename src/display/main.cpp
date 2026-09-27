#include <Arduino.h>
#include <EncButton.h>
#include <LittleFS.h>
#include <LovyanGFX.h>
#include <SD.h>
#include <SPI.h>
#include <camera.h>
#include <esp_timer.h>
#include <fb_gfx.h>

#include "config.h"
#include "img_converters.h"
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
static int16_t photoIndex = NOT_CONNECTED;  // by default: no SD card

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

static camera_config_t build_camera_config() {
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

    config.pixel_format   = CAMERA_PIXFORMAT;
    config.frame_size     = SIZE_VIEWFINDER;
    config.max_frame_size = FRAMESIZE_UXGA;

    config.fb_count     = 2;  // enable asymmetric double-buffering
    config.grab_mode    = CAMERA_GRAB_LATEST;
    config.xclk_freq_hz = CAMERA_PCLK_FREQ;

    return config;
}

// TODO: validate if still needed
static void enable_sde_bits(sensor_t* s, uint8_t bits) {
    s->set_reg(s, 0xFF, 0xFF, 0x00);
    s->set_reg(s, 0x7C, 0xFF, 0x00);
    int current = s->get_reg(s, 0x7D, 0xFF);
    s->set_reg(s, 0x7D, 0xFF, current | bits);
}

static void configure_camera() {
    // set sensor configs
    sensor_t* s = cam_sensor_get();

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
    // s->set_ae_level(s, 0);

    // // image correction values
    // s->set_gainceiling(s, GAINCEILING_2X);  // 2X to 128X
    // s->set_aec_value(s, 300);               // 0 to 1200
    // s->set_agc_gain(s, 0);                  // 0 to 30

    s->set_special_effect(s, effect.value);
    s->set_wb_mode(s, whiteBalance.value);

    enable_sde_bits(s, 0x07);
}

bool camera_init() {
    cam_deinit();

    camera_config_t config = build_camera_config();
    esp_err_t       err    = cam_init(&config);
    if (err != ESP_OK) return false;

    configure_camera();
    return true;
}

// SD card

char* getFilename(const uint16_t index, uint8_t mode, const char* res = NULL) {
    // examples:
    // - images/0042_720p.jpg
    // - raw/0067_240p.raw
    // - thumb/0123.thumb
    static char name[32];

    if (mode & CAPTURE_THUMB) {
        snprintf(name, sizeof(name), "/thumb/%04u.thumb", index);
        return name;
    }

    if (mode & CAPTURE_RAW) {
        snprintf(name, sizeof(name), "/raw/%04u_%s.raw", index, res);
        return name;
    }

    if (mode & CAPTURE_JPEG) {
        snprintf(name, sizeof(name), "/images/%04u_%s.jpg", index, res);
        return name;
    }

    return NULL;
}

void findPhotoIndex() {
    char* name = NULL;
    photoIndex = 0;

    while (photoIndex < 10000) {
        name = getFilename(photoIndex, CAPTURE_THUMB);  // check for always-created thumbs
        if (!SD.exists(name)) {
            ESP_LOGI("photo", "Next photo index: %u", photoIndex);
            return;
        }
        photoIndex++;
    }

    photoIndex = NOT_CONNECTED;
    ESP_LOGE("photo", "Photo index overflow");
}

bool mountSD(uint8_t max_attempts = 3, uint32_t retry_delay = 150) {
    ESP_LOGD("sd", "Waiting for DMA");
    display.waitDMA();

    digitalWrite(DISPLAY_CS, HIGH);
    delay(5);

    for (uint8_t attempt = 1; attempt <= max_attempts; attempt++) {
        SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, NOT_CONNECTED);
        if (SD.begin(SD_CS, SPI, SD_CARD_SPI_FREQ)) return true;
        ESP_LOGW("sd", "Mount attempt %d failed", attempt);

        SD.end();
        digitalWrite(SD_CS, HIGH);
        if (attempt < max_attempts) delay(retry_delay);
    }

    ESP_LOGE("sd", "All mount attempts failed");
    return false;
}

void unmountSD() {
    SD.end();
    digitalWrite(SD_CS, HIGH);
}

// capture

bool saveRawFrame(const camera_fb_t* frame, const char* filename) {
    File file = SD.open(filename, FILE_WRITE, true);
    if (!file) {
        ESP_LOGE("save_raw", "Failed to open file");
        return false;
    }

    size_t written = file.write(frame->buf, frame->len);
    file.flush();
    file.close();

    if (written != frame->len) {
        ESP_LOGE("save_raw", "Failed to write file");
        return false;
    }

    return true;
}

static size_t save_photo_chunk(void* arg, size_t index, const void* data, size_t size) {
    File* file = static_cast<File*>(arg);
    return file->write(static_cast<const uint8_t*>(data), size);
}

bool saveImage(const camera_fb_t* frame, const char* filename) {
    File file = SD.open(filename, FILE_WRITE, true);
    if (!file) {
        ESP_LOGE("save_image", "Failed to open file");
        return false;
    }

    bool ok = frame2jpg_cb(frame, IMAGE_QUALITY, save_photo_chunk, &file);
    file.flush();
    file.close();

    if (!ok) {
        ESP_LOGE("save_image", "Failed to convert and write image");
        return false;
    }

    return true;
}

bool captureImage(framesize_t size, uint8_t mode, const char* resolution) {
    bool         ok;
    esp_err_t    err;
    camera_fb_t* frame;

    if (mode == CAPTURE_NO) {
        ESP_LOGI("capture", "No need to capture framesize %d", size);
        return true;
    }

    if (size != cam_get_framesize()) {
        ESP_LOGD("capture", "Changing framesize");
        err = cam_set_raw_framesize(size);
        if (err != ESP_OK) {
            ESP_LOGE("capture", "Failed to set framesize");
            return false;
        }

        ESP_LOGD("capture", "Capturing dummy frame");
        frame = cam_fb_get();
        if (frame == NULL) {
            ESP_LOGE("capture", "Capture failed");
            return false;
        }

        cam_fb_return(frame);
    }

    ESP_LOGI("capture", "Capturing actual frame");
    frame = cam_fb_get();
    if (frame == NULL) {
        ESP_LOGE("capture", "Capture failed");
        return false;
    }

    if (mode & CAPTURE_THUMB) {
        char* file = getFilename(photoIndex, CAPTURE_THUMB, resolution);

        ESP_LOGI("capture", "Saving thumb");
        ok = saveRawFrame(frame, file);
        if (!ok) {
            ESP_LOGE("capture", "Failed to save thumb");
            cam_fb_return(frame);
            return false;
        }

        ESP_LOGI("capture", "Save success");
    }

    if (mode & CAPTURE_RAW) {
        char* file = getFilename(photoIndex, CAPTURE_RAW, resolution);

        ESP_LOGI("capture", "Saving RAW frame");
        ok = saveRawFrame(frame, file);
        if (!ok) {
            ESP_LOGE("capture", "Failed to save RAW frame");
            cam_fb_return(frame);
            return false;
        }

        ESP_LOGI("capture", "Save success");
    }

    if (mode & CAPTURE_JPEG) {
        char* file = getFilename(photoIndex, CAPTURE_JPEG, resolution);

        ESP_LOGI("capture", "Saving JPEG image");
        ok = saveImage(frame, file);
        if (!ok) {
            ESP_LOGE("capture", "Failed to save JPEG image");
            cam_fb_return(frame);
            return false;
        }

        ESP_LOGI("capture", "Save success");
    }

    cam_fb_return(frame);
    return true;
}

bool capture() {
    bool ok;

    ok = mountSD();
    if (!ok) {
        ESP_LOGE("capture", "Failed to mount SD card");
        return false;
    }

    for (size_t i = 0; i < captureSizesCount; i++) {
        uint8_t     mode       = captureSettings[i]->value;
        const char* resolution = captureSettings[i]->name;

        // always capture RAW for gallery previews (thumbs)
        if (captureSizes[i] == SIZE_VIEWFINDER) mode |= CAPTURE_THUMB;

        if (mode == CAPTURE_NO) {
            ESP_LOGD("capture", "No need to capture %s", resolution);
            continue;
        }

        ok = captureImage(captureSizes[i], mode, resolution);
        if (!ok) {
            ESP_LOGE("capture", "Failed to capture %s", resolution);
            continue;
        }

        ESP_LOGI("capture", "Capture %s success", resolution);
        // TODO: draw message on display (note: impossible when SD is mounted)
        // TODO: flash an indicator led (maybe ring)
    }

    unmountSD();
    photoIndex++;

    return true;
}

void initSPI() {
    delay(1500);  // stabilize SD card

    pinMode(DISPLAY_CS, OUTPUT);
    digitalWrite(DISPLAY_CS, HIGH);

    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    gpio_set_drive_capability((gpio_num_t)SD_CS, GPIO_DRIVE_CAP_3);
    pinMode(SPI_MISO, INPUT_PULLUP);
}

bool initDisplay() {
    bool ok = display.init();
    if (!ok) return false;

    display.setSwapBytes(false);

    canvas.setPsram(true);
    canvas.setColorDepth(16);
    canvas.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT);

    return true;
}

void setup() {
    bool ok;

    Serial.begin(115200);
    Serial.setDebugOutput(true);

    initSPI();

    ok = mountSD();
    if (ok) {
        ESP_LOGI("init", "SD Card detected with size: %llu MB", SD.cardSize() / (1024 * 1024));
        findPhotoIndex();
        unmountSD();
    } else {
        ESP_LOGE("init", "Failed to mount SD card");
    }

    ok = camera_init();
    if (!ok) {
        ESP_LOGE("init", "Camera init failed");
        return;
    }

    ok = initDisplay();
    if (!ok) {
        ESP_LOGE("init", "Display init failed");
        return;
    }

    ESP_LOGI("init", "Init done");
}

void drawFrame() {
    camera_fb_t* fb = cam_fb_get();
    if (fb == NULL) {
        ESP_LOGE("frame", "Capture failed");
        return;
    }

    canvas.pushImage(0, 0, fb->width, fb->height, (uint16_t*)fb->buf);
    cam_fb_return(fb);
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
            cam_set_raw_framesize(SIZE_VIEWFINDER);
            break;

        case COUNTDOWN:
            countdown     = TICK_COUNT;
            countdown_tmr = millis() + TICK_TIME;
            break;

        case PICTURE:
            drawFrame();  // draw frame without countdown
            updateDisplay();
            capture();
            // TODO: conditional auto exit to viewfinder
            break;

        default:
            break;
    }
}

void handleViewfinder() {
    if (btnX.click() && photoIndex > NOT_CONNECTED) {
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

// TODO: refactor using:
// saveImage() - converts FB to JPEG and saves
// saveRawFrame() - just saves FB as plain pixformat bytes
// both accepts FB and filename, open and close files automatically underneah
