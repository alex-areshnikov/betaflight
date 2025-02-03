#pragma once

#include "pg/pg.h"
#include "common/time.h"
#include "drivers/battery_temp/battery_temp.h"

typedef enum {
    BATTERY_TEMP_DEFAULT = 0,
    BATTERY_TEMP_NONE = 1,
    BATTERY_TEMP_AHT20 = 2,
    BATTERY_TEMP_VIRTUAL = 3
} batteryTempSensor_e;

typedef struct batteryTempConfig_s {
    uint8_t battery_temp_i2c_device;
    uint8_t battery_temp_i2c_address;
    uint8_t battery_temp_hardware;
} batteryTempConfig_t;

PG_DECLARE(batteryTempConfig_t, batteryTempConfig);

typedef struct batteryTemp_s {
    batteryTempDev_t dev;
    float temperature;
} batteryTemp_t;

extern batteryTemp_t batterTemp;

void batteryTempInit(void);
void batteryUpdateTemperature(timeUs_t currentTimeUs);
float getBatteryTemp(void);