#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-virtual-audio", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
    return obs_module_text("Description");
}

bool obs_module_load(void)
{
    blog(LOG_INFO, "[obs-virtual-audio] loaded (version %s)", PLUGIN_VERSION);
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "[obs-virtual-audio] unloaded");
}
