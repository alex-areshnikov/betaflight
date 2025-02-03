#pragma once

#include "drivers/bus.h" // XXX
#include "drivers/exti.h"

struct batteryTempDev_s;

typedef bool (*batteryTempOpFuncPtr)(struct batteryTempDev_s *battery_temp); 
typedef bool (*batteryTempReadFuncPtr)(struct batteryTempDev_s *battery_temp);
typedef bool (*batteryTempGetFuncPtr)(struct batteryTempDev_s *battery_temp, float *temperature);

typedef struct batteryTempDev_s {
    extDevice_t dev;
    extiCallbackRec_t exti;
    batteryTempOpFuncPtr start_t;
    batteryTempReadFuncPtr read_t;
    batteryTempGetFuncPtr get_t;
} batteryTempDev_t;