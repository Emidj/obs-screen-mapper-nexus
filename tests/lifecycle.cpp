// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs.h>
#include <cstdio>
#include <cstdlib>
static void check(bool condition, const char *message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
    std::printf("PASS: %s\n", message);
}
static void count_child(obs_source_t *, obs_source_t *, void *data) { ++*static_cast<int *>(data); }
static int children(obs_source_t *source) { int count = 0; obs_source_enum_full_tree(source, count_child, &count); return count; }
static obs_source_t *mapper(const char *name, const char *uuid)
{
    auto *settings = obs_data_create();
    obs_data_set_string(settings, "input_uuid", uuid);
    auto *source = obs_source_create("obs_screen_mapper", name, settings, nullptr);
    obs_data_release(settings);
    return source;
}
int main(int argc, char **argv)
{
    check(argc == 3, "module and data paths supplied");
    check(obs_startup("en-US", nullptr, nullptr), "libobs startup");
    obs_module_t *module = nullptr;
    check(obs_open_module(&module, argv[1], argv[2]) == MODULE_SUCCESS, "DLL opened by libobs");
    check(obs_init_module(module), "module registration");
    for (int i = 0; i < 50; ++i) {
        auto *source = obs_source_create("obs_screen_mapper", "Mapper test", nullptr, nullptr);
        check(source != nullptr, "source creation");
        check(obs_source_get_type(source) == OBS_SOURCE_TYPE_INPUT, "registered as INPUT");
        check(obs_source_get_width(source) == 1920 && obs_source_get_height(source) == 1080, "default dimensions");
        auto *props = obs_source_properties(source);
        check(props && obs_properties_get(props, "canvas_width") && obs_properties_get(props, "author_credit"), "properties available");
        obs_properties_destroy(props);
        auto *settings = obs_data_create();
        obs_data_set_int(settings, "canvas_width", 2560);
        obs_data_set_int(settings, "canvas_height", 1440);
        obs_source_update(source, settings);
        // OBS applies video-source updates on its video thread. Without a GPU/video
        // thread, validate the updated settings through native save/load instead.
        auto *saved = obs_save_source(source);
        auto *copy = obs_load_source(saved);
        check(copy && obs_source_get_width(copy) == 2560 && obs_source_get_height(copy) == 1440, "OBS save/load round trip");
        obs_source_release(copy);
        obs_data_release(saved);
        obs_data_set_int(settings, "canvas_width", -100);
        obs_data_set_int(settings, "canvas_height", 999999);
        obs_source_update(source, settings);
        auto *bounded = obs_source_create("obs_screen_mapper", "Bounds test", settings, nullptr);
        check(bounded && obs_source_get_width(bounded) == 16 && obs_source_get_height(bounded) == 16384, "invalid dimensions bounded");
        obs_source_release(bounded);
        obs_data_release(settings);
        obs_source_release(source);
    }
    auto *scene = obs_scene_create("Existing OBS scene");
    auto *input = obs_scene_get_source(scene);
    auto *a = mapper("Mapper A", obs_source_get_uuid(input));
    check(children(a) == 1, "existing OBS scene connected without new decoder");
    obs_source_set_name(input, "Renamed input");
    check(children(a) == 1, "reference survives input rename");
    auto *b = mapper("Mapper B", obs_source_get_uuid(a));
    check(children(b) == 2, "mapper chain enumerates dependencies");
    check(!obs_source_add_active_child(a, a), "direct cycle rejected by OBS");
    check(!obs_source_add_active_child(a, b), "indirect mapper cycle rejected by OBS");
    check(obs_scene_add(scene, a) == nullptr, "scene containing its mapper rejected");
    auto *missing = mapper("Missing", "00000000-0000-0000-0000-000000000000");
    check(children(missing) == 0, "missing UUID has no input");
    obs_source_release(missing);
    obs_source_release(b);
    obs_source_release(a);
    obs_scene_release(scene);
    obs_shutdown();
    std::puts("PASS: phase 2 lifecycle complete (no GPU or OBS UI validation)");
}
