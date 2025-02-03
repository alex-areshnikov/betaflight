#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "platform.h"

#include "common/utils.h"

#include "battery_temp.h"
#include "battery_temp_virtual.h"

static bool virtualBatteryTempStart(batteryTempDev_t *batteryTempDev);
static bool virtualBatteryTempRead(batteryTempDev_t *batteryTempDev);
static bool virtualBatteryTempGet(batteryTempDev_t *batteryTempDev, float *temperature);

static float virtualTemperature;

static bool virtualBatteryTempStart(batteryTempDev_t *batteryTempDev)
{
    UNUSED(batteryTempDev);

    virtualTemperature = 0.0f;

    return true;
}

static bool virtualBatteryTempRead(batteryTempDev_t *batteryTempDev)
{
    UNUSED(batteryTempDev);

    virtualTemperature += ((float)rand() / (float)(RAND_MAX)) * 1.8f - 0.9f;

    return true;
}

static bool virtualBatteryTempGet(batteryTempDev_t *batteryTempDev, float *temperature)
{
    UNUSED(batteryTempDev);

    *temperature = virtualTemperature;

    return true;
}

bool virtualBatteryTempDetect(batteryTempDev_t *batteryTempDev)
{           
    batteryTempDev->get_t = virtualBatteryTempGet;
    batteryTempDev->read_t = virtualBatteryTempRead;
    batteryTempDev->start_t = virtualBatteryTempStart;

    return true;
}