#include <stdlib.h>

#include "pg/pg.h"
#include "pg/pg_ids.h"

#include "common/utils.h"
#include "battery_temp.h"
#include "drivers/battery_temp/battery_temp_virtual.h"
#include "drivers/battery_temp/battery_temp_aht20.h"

batteryTemp_t batteryTemp;
static bool batteryTempReady = false;

void batteryTempAutoDetect(void);

PG_REGISTER(batteryTempConfig_t, batteryTempConfig, PG_BATTERY_TEMP_CONFIG, 1);

typedef enum {
    BATTERY_TEMP_STATE_TEMPERATURE_START,
    BATTERY_TEMP_STATE_TEMPERATURE_READ
} batteryTempState_e;

void batteryTempInit(void)
{
    batteryTemp.temperature = 0.0f;

    if(batteryTempConfig()->battery_temp_hardware == BATTERY_TEMP_DEFAULT) {
        batteryTempAutoDetect();
        return;
    }

    if(batteryTempConfig()->battery_temp_hardware == BATTERY_TEMP_VIRTUAL) {
        virtualBatteryTempDetect(&batteryTemp.dev);
    }

    if(batteryTempConfig()->battery_temp_hardware == BATTERY_TEMP_AHT20) {
        aht20BatteryTempDetect(&batteryTemp.dev);                
    }

    batteryTempReady = batteryTemp.dev.start_t(&batteryTemp.dev);
}

void batteryUpdateTemperature(timeUs_t currentTimeUs)
{
    UNUSED(currentTimeUs);
    
    if(!batteryTempReady) { return; }

    static batteryTempState_e state = BATTERY_TEMP_STATE_TEMPERATURE_START;    

    switch (state) {
        default:
        case BATTERY_TEMP_STATE_TEMPERATURE_START:
            if(batteryTemp.dev.read_t(&batteryTemp.dev)) {
                state = BATTERY_TEMP_STATE_TEMPERATURE_READ;
            }
            
            break;

        case BATTERY_TEMP_STATE_TEMPERATURE_READ:
            if(batteryTemp.dev.get_t(&batteryTemp.dev, &batteryTemp.temperature)) {
                state = BATTERY_TEMP_STATE_TEMPERATURE_START;
            }

            break;
    }
}

float getBatteryTemp(void)
{
    return batteryTemp.temperature;
}

// private

void batteryTempAutoDetect(void) {
    aht20BatteryTempDetect(&batteryTemp.dev);
    batteryTempReady = batteryTemp.dev.start_t(&batteryTemp.dev);
}