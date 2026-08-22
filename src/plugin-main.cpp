#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <QWidget>

#include <memory>
#include <string>

#include "audio-capture.hpp"
#include "settings-dialog.hpp"
#include "wasapi-renderer.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-virtual-audio", "en-US")

namespace {
constexpr char kConfigFile[] = "config.json";
constexpr char kDeviceIdKey[] = "playback_device_id";

std::unique_ptr<AudioCapture> g_audio_capture;
std::unique_ptr<WasapiRenderer> g_wasapi_renderer;
std::string g_configured_device_id;

std::string load_device_id()
{
    char *path = obs_module_config_path(kConfigFile);
    if (!path)
        return {};

    obs_data_t *config = obs_data_create_from_json_file_safe(path, "bak");
    bfree(path);
    if (!config)
        return {};

    const char *value = obs_data_get_string(config, kDeviceIdKey);
    std::string device_id = value ? value : "";
    obs_data_release(config);
    return device_id;
}

bool save_device_id(const std::string &device_id)
{
    char *directory = obs_module_config_path(nullptr);
    if (!directory)
        return false;

    const int mkdir_result = os_mkdirs(directory);
    bfree(directory);
    if (mkdir_result == MKDIR_ERROR)
        return false;

    char *path = obs_module_config_path(kConfigFile);
    if (!path)
        return false;

    obs_data_t *config = obs_data_create();
    obs_data_set_string(config, kDeviceIdKey, device_id.c_str());
    const bool saved = obs_data_save_json_pretty_safe(config, path, "tmp", "bak");
    obs_data_release(config);
    bfree(path);
    return saved;
}

bool start_output(const std::string &device_id)
{
    auto renderer = std::make_unique<WasapiRenderer>(*g_audio_capture, device_id);
    if (!renderer->start())
        return false;

    g_wasapi_renderer = std::move(renderer);
    return true;
}

bool apply_device(const std::string &device_id)
{
    if (!g_audio_capture)
        return false;

    if (g_wasapi_renderer && g_wasapi_renderer->running() &&
        g_wasapi_renderer->target_device_id() == device_id) {
        return save_device_id(device_id);
    }

    const std::string previous_device_id = g_configured_device_id;

    if (g_wasapi_renderer) {
        g_wasapi_renderer->stop();
        g_wasapi_renderer.reset();
    }
    g_audio_capture->discard_buffered_audio();

    if (start_output(device_id)) {
        g_configured_device_id = device_id;
        if (!save_device_id(device_id))
            blog(LOG_WARNING, "[obs-virtual-audio] output changed but settings could not be saved");
        blog(LOG_INFO,
             "[obs-virtual-audio] playback target changed; renderer will reconnect if needed");
        return true;
    }

    blog(LOG_ERROR,
         "[obs-virtual-audio] could not switch playback devices; restoring previous output");
    if (!previous_device_id.empty()) {
        g_audio_capture->discard_buffered_audio();
        if (!start_output(previous_device_id))
            blog(LOG_ERROR, "[obs-virtual-audio] failed to restore the previous playback device");
    }
    return false;
}

WasapiRendererState output_state()
{
    return g_wasapi_renderer ? g_wasapi_renderer->state() : WasapiRendererState::stopped;
}

void open_settings(void *)
{
    QWidget *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
    show_settings_dialog(parent, g_configured_device_id, apply_device, output_state);
}
} // namespace

MODULE_EXPORT const char *obs_module_description(void) { return obs_module_text("Description"); }

bool obs_module_load(void)
{
    blog(LOG_INFO, "[obs-virtual-audio] loaded (version %s)", PLUGIN_VERSION);

    g_audio_capture = std::make_unique<AudioCapture>();
    if (!g_audio_capture->start()) {
        blog(LOG_ERROR, "[obs-virtual-audio] failed to start audio capture");
        g_audio_capture.reset();
        return false;
    }

    g_configured_device_id = load_device_id();
    bool output_started = false;
    if (!g_configured_device_id.empty())
        output_started = start_output(g_configured_device_id);
    if (!output_started)
        output_started = start_output({});
    if (!output_started) {
        blog(LOG_WARNING, "[obs-virtual-audio] WASAPI output is inactive; choose a device from "
                          "Tools > OBS Virtual Audio");
    }

    obs_frontend_add_tools_menu_item(obs_module_text("ToolsMenu"), open_settings, nullptr);

    return true;
}

void obs_module_unload(void)
{
    if (g_wasapi_renderer) {
        g_wasapi_renderer->stop();
        g_wasapi_renderer.reset();
    }

    if (g_audio_capture) {
        g_audio_capture->stop();
        g_audio_capture.reset();
    }

    blog(LOG_INFO, "[obs-virtual-audio] unloaded");
}
