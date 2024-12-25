#include <stdlib.h>

#include "common/utils.h"
#include "battery_temp.h"

batteryTemp_t batteryTemp;

void batteryTempInit(void)
{
    batteryTemp.temperature = 10.0f;
}

void batteryUpdateTemperature(timeUs_t currentTimeUs)
{
    UNUSED(currentTimeUs);
    batteryTemp.temperature += ((float)rand() / (float)(RAND_MAX)) * 1.8f - 0.9f;
}

float getBatteryTemp(void)
{
    return batteryTemp.temperature;
}