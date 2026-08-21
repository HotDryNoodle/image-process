#include "smi.h"
#include <lyn_smi.h>
#include <sdk/error.hpp>

using namespace lynsdk;

SMI_INFO_T SMI::get() {
  lynDeviceProperties_t deviceProp;
  CHECK_ERR(lynGetDeviceProperties(id, &deviceProp));

  SMI_INFO_T info = {0};
  info.apuUsage = deviceProp.deviceApuUsageRate;
  info.cpuUsage = deviceProp.deviceArmUsageRate;
  info.vicUsage = deviceProp.deviceVicUsageRate;
  info.memoryUsage = (float)deviceProp.deviceMemoryUsed /
                     (float)deviceProp.deviceMemoryTotal * 100;
  info.ipeFPS = deviceProp.deviceIpeUsageRate;
  info.temperature = deviceProp.deviceTemperatureCurrent;
  info.power = deviceProp.boardPowerDraw / 1000;

  return info;
}