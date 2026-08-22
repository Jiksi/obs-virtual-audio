#pragma once

#include "wasapi-renderer.hpp"

#include <functional>
#include <string>

class QWidget;

using ApplyDeviceCallback = std::function<bool(const std::string &device_id)>;
using RendererStateCallback = std::function<WasapiRendererState()>;

void show_settings_dialog(QWidget *parent, const std::string &active_device_id,
                          const ApplyDeviceCallback &apply_device,
                          const RendererStateCallback &renderer_state);
