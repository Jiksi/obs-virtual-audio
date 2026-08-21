#include <obs-module.h>

#include <memory>

#include "audio-capture.hpp"
#include "wasapi-renderer.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-virtual-audio", "en-US")

namespace {
std::unique_ptr<AudioCapture> g_audio_capture;
std::unique_ptr<WasapiRenderer> g_wasapi_renderer;
}

MODULE_EXPORT const char *obs_module_description(void)
{
    return obs_module_text("Description");
}

bool obs_module_load(void)
{
    blog(LOG_INFO, "[obs-virtual-audio] loaded (version %s)", PLUGIN_VERSION);

    g_audio_capture = std::make_unique<AudioCapture>();
    if (!g_audio_capture->start()) {
        blog(LOG_ERROR, "[obs-virtual-audio] failed to start audio capture");
        g_audio_capture.reset();
        return false;
    }

    g_wasapi_renderer = std::make_unique<WasapiRenderer>(*g_audio_capture);
    if (!g_wasapi_renderer->start()) {
        blog(LOG_ERROR, "[obs-virtual-audio] failed to start WASAPI output");
        g_wasapi_renderer.reset();
        g_audio_capture->stop();
        g_audio_capture.reset();
        return false;
    }

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
