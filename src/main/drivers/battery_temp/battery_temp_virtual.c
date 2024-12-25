#include <stdbool.h>
#include <stdint.h>

#include "platform.h"

#include "common/utils.h"

#include "battery_temp.h"
#include "battery_temp_virtual.h"

static float virtualTemperature;

static bool virtualBatteryTempStart(batteryTempDev_t *batteryTemp)
{
    UNUSED(batteryTemp);

    return true;
}

static bool virtualBatteryTempReadGet(batteryTempDev_t *batteryTemp)
{
    UNUSED(batteryTemp);

    return true;
}

static void virtualBatteryTempCalculate(float *temperature)
{
    if (temperature)
        *temperature = virtualTemperature;
}

void virtualBatteryTempSet(float temperature)
{
    virtualTemperature = temperature;
}

bool virtualBatteryTempDetect(batteryTempDev_t *batteryTemp)
{
    virtualTemperature = 25.69;
            
    batteryTemp->get_t = virtualBatteryTempReadGet;
    batteryTemp->read_t = virtualBatteryTempReadGet;
    batteryTemp->start_t = virtualBatteryTempStart;
    batteryTemp->calculate = virtualBatteryTempCalculate;

    return true;
}