#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int      SCCB_Init(int pin_sda, int pin_scl);
int      SCCB_Use_Port(int sccb_i2c_port);
int      SCCB_Deinit(void);
int      SCCB_Probe(uint8_t slv_addr);
uint8_t  SCCB_Read(uint8_t slv_addr, uint8_t reg);
int      SCCB_Write(uint8_t slv_addr, uint8_t reg, uint8_t data);

#ifdef __cplusplus
}
#endif
