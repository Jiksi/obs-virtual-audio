#pragma once

#include <functional>
#include <string>

class QWidget;

using ApplyDeviceCallback = std::function<bool(const std::string &device_id)>;

void show_settings_dialog(QWidget *parent, const std::string &active_device_id,
                          const ApplyDeviceCallback &apply_device);
