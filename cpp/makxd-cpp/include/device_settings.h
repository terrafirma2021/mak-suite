#pragma once
#include "makxd_settings_types.h"
#include <array>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>
namespace makxd {
using DeviceSettings=makxd_device_settings_t;
using SettingsInfo=makxd_settings_info_t;
using SettingsSnapshot=makxd_settings_snapshot_t;
class SettingsError: public std::runtime_error {
public:
    uint8_t status;
    explicit SettingsError(uint8_t value):std::runtime_error("Device settings request failed (status "+std::to_string(value)+")"),status(value) {}
};
namespace detail {
using SettingsQuery=std::function<std::vector<uint8_t>(std::span<const uint8_t>)>;
std::array<uint8_t,400> settingsEncode(const DeviceSettings& value);
DeviceSettings settingsDecode(std::span<const uint8_t> image);
SettingsInfo settingsInfo(const SettingsQuery& query);
SettingsSnapshot settingsRead(const SettingsQuery& query);
SettingsSnapshot settingsApply(const SettingsQuery& query,const SettingsSnapshot& snapshot,uint8_t sections);
void settingsSave(const SettingsQuery& query,const SettingsSnapshot& snapshot,uint8_t sections);
std::vector<uint8_t> settingsExport(const SettingsQuery& query,const SettingsSnapshot& snapshot,uint8_t sections);
SettingsSnapshot settingsImport(const SettingsQuery& query,std::span<const uint8_t> file);
}
}
