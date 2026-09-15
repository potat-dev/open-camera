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

static camera_config_t get_camera_config() {
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

    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size   = FRAMESIZE_QVGA;

    config.fb_count     = 2;
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

// Sends a raw command in SPI mode and returns the R1 response
static uint8_t sendRawCmd(uint8_t cmd, uint32_t arg, uint8_t crc) {
    SPI.transfer(0x40 | cmd);
    SPI.transfer((arg >> 24) & 0xFF);
    SPI.transfer((arg >> 16) & 0xFF);
    SPI.transfer((arg >> 8) & 0xFF);
    SPI.transfer(arg & 0xFF);
    SPI.transfer(crc);

    uint8_t res = 0xFF;
    for (int i = 0; i < 16; i++) {
        res = SPI.transfer(0xFF);
        if ((res & 0x80) == 0) break;
    }
    return res;
}

bool testSdCard() {
    // 1. Conclude display operations
    display.waitDMA();
    digitalWrite(DISPLAY_CS, HIGH);

    // 2. Bus settling delay after display DMA
    delay(5);

    // 3. Re-align SPI2_HOST to Mode 0 at 10 MHz
    SPI.beginTransaction(SPISettings(10_MHz, MSBFIRST, SPI_MODE0));
    SPI.transfer(0xFF);

    // 4. Wake the card from idle power-save into active transfer state.
    // This prevents CMD13 in ff_sd_status() from returning 0x01 (Idle),
    // eliminating the "Check status failed" error entirely.
    digitalWrite(SD_CS, LOW);
    sendRawCmd(55, 0, 0x65);                       // CMD55: APP_CMD prefix
    uint8_t r = sendRawCmd(41, 0x40000000, 0x77);  // ACMD41: HCS=1
    digitalWrite(SD_CS, HIGH);
    SPI.transfer(0xFF);  // 8 clocks to release MISO
    SPI.endTransaction();

    // 5. Perform file I/O
    File file = SD.open("/test.txt", FILE_WRITE, true);

    // Recovery path in case of an unexpected bus stall
    if (!file) {
        Serial.println("Warning: File open failed, executing clean re-mount...");
        digitalWrite(SD_CS, HIGH);

        SD.end();
        delay(20);

        SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);
        if (SD.begin(SD_CS, SPI, 10_MHz)) {
            file = SD.open("/test.txt", FILE_WRITE, true);
        }
    }

    if (!file) {
        Serial.println("FAILED to create file!");
        digitalWrite(SD_CS, HIGH);
        return false;
    }

    size_t size = file.println("Test string blah blah");
    file.flush();
    file.close();

    Serial.print("Size written: ");
    Serial.println(size);

    digitalWrite(SD_CS, HIGH);
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(1500);

    Serial.setDebugOutput(true);

    Serial.println("\n\n========================================");
    Serial.println("         STEP-BY-STEP SPI DEBUG         ");
    Serial.println("========================================");

    // 1. Immediately de-assert chip select lines
    pinMode(DISPLAY_CS, OUTPUT);
    digitalWrite(DISPLAY_CS, HIGH);

    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    gpio_set_drive_capability((gpio_num_t)SD_CS, GPIO_DRIVE_CAP_3);
    pinMode(SPI_MISO, INPUT_PULLUP);

    // 2. Initialize SPI bus with software CS (-1)
    Serial.print("[1] Initializing Arduino SPI bus... ");
    bool spiOk = SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);
    Serial.println(spiOk ? "OK" : "FAILED");

    // 3. Hardware warm-boot recovery sequence:
    // Forces the SD card controller out of any interrupted data state left by esptool or warm
    // reset.
    Serial.println("[2] Executing warm-reset bus recovery...");
    SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));

    // A. Issue CMD12 (STOP_TRANSMISSION) with CS LOW to abort any dangling transfer
    digitalWrite(SD_CS, LOW);
    sendRawCmd(12, 0, 0x61);
    for (int i = 0; i < 16; i++) SPI.transfer(0xFF);

    // B. De-assert CS and send 80 clocks (10 bytes) at 400 kHz
    digitalWrite(SD_CS, HIGH);
    for (int i = 0; i < 16; i++) SPI.transfer(0xFF);

    // C. Wait for card to release MISO (clear busy)
    digitalWrite(SD_CS, LOW);
    uint32_t t0 = millis();
    while (SPI.transfer(0xFF) != 0xFF && (millis() - t0 < 300));
    digitalWrite(SD_CS, HIGH);

    // D. Mandatory 80 dummy clock cycles with CS HIGH per SD Specification
    for (int i = 0; i < 16; i++) SPI.transfer(0xFF);

    SPI.endTransaction();

    // 4. Mount SD card at 10 MHz with retry logic
    Serial.print("[3] Mounting SD card at 10 MHz... ");
    bool sdOk = false;
    for (uint8_t attempt = 1; attempt <= 3; attempt++) {
        sdOk = SD.begin(SD_CS, SPI, 10_MHz);
        if (sdOk) break;

        Serial.printf("retry %d... ", attempt);
        delay(100);
    }

    if (sdOk) {
        Serial.println("SUCCESS!");
        Serial.printf("    Card Type: %d, Size: %llu MB\n", SD.cardType(),
                      SD.cardSize() / (1024 * 1024));
        findNextPhotoIndex();
    } else {
        Serial.println("FAILED!");
    }

    // 5. Initialize Camera
    Serial.print("[4] Initializing Camera... ");
    camera_config_t config = get_camera_config();
    esp_err_t       camErr = esp_camera_init(&config);
    if (camErr != ESP_OK) {
        Serial.printf("FAILED with error 0x%x\n", camErr);
        return;
    }
    Serial.println("OK");
    configure_camera();

    // 6. Initialize Display at 80 MHz
    Serial.print("[5] Initializing Display... ");
    display.init();
    display.setSwapBytes(false);
    Serial.println("OK");

    canvas.setPsram(true);
    canvas.setColorDepth(16);
    canvas.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT);

    Serial.println("========================================\n");
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
        case COUNTDOWN:
            countdown     = TICK_COUNT;
            countdown_tmr = millis() + TICK_TIME;
            break;

        case PICTURE:
            drawFrame();
            updateDisplay();
            testSdCard();
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
