#pragma once

struct batteryTempDev_s;
bool virtualBatteryTempDetect(struct batteryTempDev_s *batteryTemp);
void virtualBatteryTempSet(float temperature);