#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "platform.h"

#include "common/utils.h"

#include "drivers/bus.h"
#include "drivers/bus_i2c.h"
#include "drivers/bus_i2c_busdev.h"
#include "drivers/io.h"
#include "drivers/time.h"

#include "battery_temp.h"
#include "battery_temp_aht20.h"

#define ATH20_POWER_ON_DELAY_MS             40
#define ATH20_POST_INIT_DELAY_MS            80

#define AHT20_I2C_ADDR                      (0x38)

#define AHT20_INITIALIZATION_COMMAND        (0xBE)
#define AHT20_INITIALIZATION_DATA           {0x08, 0x00}

#define AHT20_MEASUREMENT_TRIGGER_COMMAND   (0xAC)
#define AHT20_MEASUREMENT_DATA              {0x33, 0x00}

#define AHT20_SOFT_RESET_COMMAND            (0xBA)
#define AHT20_READ_STATUS_COMMAND           (0x71)

static bool aht20BatteryTempStart(batteryTempDev_t *batteryTempDev);
static bool aht20BatteryTempRead(batteryTempDev_t *batteryTempDev);
static bool aht20BatteryTempGet(batteryTempDev_t *batteryTempDev, float *temperature);

static bool aht20BatteryTempStart(batteryTempDev_t *batteryTempDev)
{
    delay(ATH20_POWER_ON_DELAY_MS);
    UNUSED(batteryTempDev);

    uint8_t buffer[2] = AHT20_INITIALIZATION_DATA;

    if (i2cBusy(I2CDEV_1, NULL)) { return false; }

    if(i2cWriteBuffer(I2CDEV_1, AHT20_I2C_ADDR, AHT20_INITIALIZATION_COMMAND, sizeof(buffer), buffer)) {
        delay(ATH20_POST_INIT_DELAY_MS);
        return true;
    }

    return false;
}

static bool aht20BatteryTempRead(batteryTempDev_t *batteryTempDev)
{
    UNUSED(batteryTempDev);

    uint8_t buffer[2] = AHT20_MEASUREMENT_DATA;

    if (i2cBusy(I2CDEV_1, NULL)) { return false; }

    return i2cWriteBuffer(I2CDEV_1, AHT20_I2C_ADDR, AHT20_MEASUREMENT_TRIGGER_COMMAND, sizeof(buffer), buffer);
}

static bool aht20BatteryTempGet(batteryTempDev_t *batteryTempDev, float *temperature)
{
    UNUSED(batteryTempDev);

    uint8_t singleBuffer = 0;

    if (!i2cRead(I2CDEV_1, AHT20_I2C_ADDR, 0xFF, 1, &singleBuffer)) {
        return false;
    }

    if ((singleBuffer & 0x80) != 0) {        
        return false;
    }

    uint8_t buffer[6];

    if (!i2cRead(I2CDEV_1, AHT20_I2C_ADDR, 0xFF, 6, buffer)) {        
        return false;
    }

    uint32_t rawTemp = 0;
    rawTemp = buffer[3] & 0x0F;
    rawTemp <<= 8;
    rawTemp += buffer[4];
    rawTemp <<= 8;
    rawTemp += buffer[5];

    *temperature = (float)rawTemp/1048576.0*200.0-50.0;

    return true;
}

bool aht20BatteryTempDetect(batteryTempDev_t *batteryTempDev)
{             
    extDevice_t *dev = &batteryTempDev->dev;
    dev->busType_u.i2c.address = AHT20_I2C_ADDR;
    
    batteryTempDev->get_t = aht20BatteryTempGet;
    batteryTempDev->read_t = aht20BatteryTempRead;
    batteryTempDev->start_t = aht20BatteryTempStart;

    return true;
}