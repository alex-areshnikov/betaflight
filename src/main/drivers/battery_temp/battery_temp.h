#pragma once

#include "drivers/bus.h" // XXX
#include "drivers/exti.h"

struct batteryTempDev_s;

typedef bool (*batteryTempOpFuncPtr)(struct batteryTempDev_s *battery_temp); 
typedef bool (*batteryTempGetFuncPtr)(struct batteryTempDev_s *battery_temp);
typedef void (*batteryTempCalculateFuncPtr)(float *temperature);

typedef struct batteryTempDev_s {
    extDevice_t dev;
    extiCallbackRec_t exti;
    batteryTempOpFuncPtr start_t;
    batteryTempGetFuncPtr read_t;
    batteryTempGetFuncPtr get_t;
    batteryTempCalculateFuncPtr calculate;
} batteryTempDev_t;