#include "sccb.h"

#include <Arduino.h>
#include <Wire.h>

static TwoWire* s_wire = &Wire;

extern "C" int SCCB_Init(int pin_sda, int pin_scl) {
    s_wire->begin(pin_sda, pin_scl, 100000);
    return 0;
}

extern "C" int SCCB_Use_Port(int sccb_i2c_port) {
    if (sccb_i2c_port == 1) {
        s_wire = &Wire1;
    } else {
        s_wire = &Wire;
    }
    return 0;
}

extern "C" int SCCB_Deinit(void) {
    s_wire->end();
    return 0;
}

extern "C" int SCCB_Probe(uint8_t slv_addr) {
    s_wire->beginTransmission(slv_addr);
    return (s_wire->endTransmission() == 0) ? 0 : -1;
}

extern "C" uint8_t SCCB_Read(uint8_t slv_addr, uint8_t reg) {
    s_wire->beginTransmission(slv_addr);
    s_wire->write(reg);
    if (s_wire->endTransmission() != 0) {
        return 0;
    }
    if (s_wire->requestFrom(slv_addr, (uint8_t)1) != 1) {
        return 0;
    }
    return s_wire->read();
}

extern "C" int SCCB_Write(uint8_t slv_addr, uint8_t reg, uint8_t data) {
    s_wire->beginTransmission(slv_addr);
    s_wire->write(reg);
    s_wire->write(data);
    return (s_wire->endTransmission() == 0) ? 0 : -1;
}

// Stubs for 16-bit operations (not used by OV2640, satisfies sccb.h)

extern "C" uint8_t SCCB_Read16(uint8_t slv_addr, uint16_t reg) {
    (void)slv_addr;
    (void)reg;
    return 0;
}
extern "C" int SCCB_Write16(uint8_t slv_addr, uint16_t reg, uint8_t data) {
    (void)slv_addr;
    (void)reg;
    (void)data;
    return -1;
}
extern "C" uint16_t SCCB_Read_Addr16_Val16(uint8_t slv_addr, uint16_t reg) {
    (void)slv_addr;
    (void)reg;
    return 0;
}
extern "C" int SCCB_Write_Addr16_Val16(uint8_t slv_addr, uint16_t reg, uint16_t data) {
    (void)slv_addr;
    (void)reg;
    (void)data;
    return -1;
}
