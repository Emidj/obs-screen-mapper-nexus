// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs-module.h>
#include <graphics/vec4.h>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include "../src/zones.hpp"
#include "mask-fixtures.hpp"
static std::atomic<bool> patterned{false};
static std::atomic<unsigned> render_calls{0};
static void check(bool ok, const char *label) { std::printf("%s: %s\n", ok ? "PASS" : "FAIL", label); if (!ok) std::exit(1); }
static const char *fixture_name(void *) { return "Test existing source"; }
static void *fixture_create(obs_data_t *, obs_source_t *) { return reinterpret_cast<void *>(1); }
static void fixture_destroy(void *) {}
static uint32_t fixture_width(void *) { return 32; }
static uint32_t fixture_height(void *) { return 16; }
static void fixture_render(void *, gs_effect_t *)
{
    ++render_calls;
    auto *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
    for (int i = 0; i < (patterned ? 4 : 1); ++i) {
        vec4 color; vec4_set(&color, i == 0 || i == 3 ? 0.25f : 0, i == 1 || i == 3 ? 0.25f : 0, i == 2 ? 0.25f : 0, 0.5f);
        gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "color"), &color);
        gs_matrix_push(); gs_matrix_translate3f(float(i % 2) * 16, float(i / 2) * 8, 0);
        while (gs_effect_loop(effect, "Solid")) gs_draw_sprite(nullptr, 0, patterned ? 16 : 32, patterned ? 8 : 16);
        gs_matrix_pop();
    }
}
static int children(obs_source_t *source)
{
    int count = 0;
    obs_source_enum_full_tree(source, [](obs_source_t *, obs_source_t *, void *p) { ++*static_cast<int *>(p); }, &count);
    return count;
}
static void select_input(obs_source_t *mapper, const char *uuid)
{
    auto *settings = obs_data_create(); obs_data_set_string(settings, "input_uuid", uuid);
    obs_source_update(mapper, settings); obs_data_release(settings);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
}
static std::vector<uint8_t> capture(obs_source_t *source)
{
    obs_enter_graphics();
    auto *target = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    auto *stage = gs_stagesurface_create(64, 64, GS_RGBA);
    check(target && stage && gs_texrender_begin(target, 64, 64), "GPU target ready");
    vec4 clear; vec4_zero(&clear); gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
    gs_ortho(0, 64, 0, 64, -100, 100);
    gs_blend_state_push(); gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
    obs_source_video_render(source);
    gs_blend_state_pop(); gs_texrender_end(target);
    gs_stage_texture(stage, gs_texrender_get_texture(target));
    uint8_t *pixels = nullptr; uint32_t stride = 0;
    check(gs_stagesurface_map(stage, &pixels, &stride), "GPU readback");
    std::vector<uint8_t> result(64 * 64 * 4);
    for (unsigned y = 0; y < 64; ++y) std::copy(pixels + y * stride, pixels + y * stride + 64 * 4, result.begin() + y * 64 * 4);
    gs_stagesurface_unmap(stage); gs_stagesurface_destroy(stage); gs_texrender_destroy(target);
    obs_leave_graphics();
    return result;
}
static void test_cached_geometry(obs_source_t *source)
{
    auto *saved=obs_source_get_settings(source);
    auto *settings=obs_data_create_from_json(obs_data_get_json(saved));
    obs_data_set_string(settings,"distribution","custom");
    mapping::Layout layout;layout.automatic=false;layout.mode=mapping::Mode::Custom;
    mapping::Zone zone;zone.id="cache-test";zone.name="Cache";zone.mesh.enabled=true;zone.mesh.horizontal=.04f;
    layout.zones.push_back(zone);
    auto publish=[&] { mapping::write(settings,layout);obs_source_update(source,settings);std::this_thread::sleep_for(std::chrono::milliseconds(100)); };
    publish();const auto original=capture(source);
    check(capture(source)==original,"cached mesh render exactly matches first upload");
    layout.zones[0].name="Metadata only";publish();
    check(capture(source)==original,"metadata update preserves cached mesh pixels");
    layout.zones[0].uv={.5f,.5f,.5f,.5f};publish();
    check(capture(source)!=original,"Custom UV invalidates cached mesh texture coordinates");
    layout.zones[0].uv={};layout.zones[0].mesh.horizontal=-.12f;publish();
    check(capture(source)!=original,"curvature change invalidates cached geometry");
    layout.zones[0].mesh.horizontal=.04f;publish();
    check(capture(source)==original,"restored curvature reproduces exact original pixels");
    layout.zones.clear();publish();const auto empty=capture(source);
    check(std::all_of(empty.begin(),empty.end(),[](uint8_t value){return value==0;}),"deleting final cached zone produces transparent output");
    layout.zones.push_back(zone);publish();
    check(capture(source)==original,"reusing deleted zone ID rebuilds correct geometry");
    obs_source_update(source,saved);std::this_thread::sleep_for(std::chrono::milliseconds(100));
    obs_data_release(settings);obs_data_release(saved);
}
static void pixel_check(obs_source_t *source, bool transparent)
{
    auto pixels = capture(source);
    bool valid = true;
    for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 64; ++x) {
        auto *p = pixels.data() + (y * 64 + x) * 4;
        if (transparent) valid = valid && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0;
        else valid = valid && p[0] >= 62 && p[0] <= 66 && p[1] == 0 && p[2] == 0 && p[3] >= 126 && p[3] <= 130;
    }
    check(valid, transparent ? "all pixels transparent" : "existing source scaled to canvas, premultiplied alpha preserved");
}
static bool color_at(const std::vector<uint8_t> &image, int x, int y, int r, int g, int b, int a = 128)
{
    const auto *p = image.data() + (y * 64 + x) * 4;
    return std::abs(int(p[0]) - r) <= 2 && std::abs(int(p[1]) - g) <= 2 && std::abs(int(p[2]) - b) <= 2 && std::abs(int(p[3]) - a) <= 2;
}
static void test_zones(obs_source_t *source)
{
    auto *settings = obs_source_get_settings(source);
    auto *props = obs_source_properties(source);
    obs_properties_apply_settings(props, settings);
    check(mapping::read(settings).zones.size() == 1 && mapping::read(settings).automatic, "starts with one zone, opening properties preserves automatic layout");
    auto change_string = [&](const char *key, const char *value) {
        obs_data_set_string(settings, key, value); obs_property_modified(obs_properties_get(props, key), settings);
    };
    auto publish = [&] { obs_source_update(source, settings); std::this_thread::sleep_for(std::chrono::milliseconds(100)); };
    change_string("zone_action", "add");
    check(mapping::read(settings).zones.size() == 2, "native properties add zone");
    change_string("distribution", "repeat"); publish(); patterned = true;
    auto before = render_calls.load(); auto image = capture(source);
    check(render_calls.load() == before + 1, "two zones capture input exactly once per mapper render");
    check(color_at(image, 8, 16, 64, 0, 0) && color_at(image, 24, 16, 0, 64, 0) &&
          color_at(image, 40, 16, 64, 0, 0) && color_at(image, 56, 48, 64, 64, 0), "Repeat maps full input to each zone");
    change_string("distribution", "split"); publish(); image = capture(source);
    check(color_at(image, 16, 16, 64, 0, 0) && color_at(image, 48, 16, 0, 64, 0) &&
          color_at(image, 16, 48, 0, 0, 64) && color_at(image, 48, 48, 64, 64, 0), "Split reconstructs input across ordered zones");
    change_string("selected_zone", "1");
    obs_data_set_bool(settings, "zone_visible", false); obs_property_modified(obs_properties_get(props, "zone_visible"), settings);
    publish(); image = capture(source);
    check(color_at(image, 16, 16, 0, 0, 0, 0) && color_at(image, 48, 16, 0, 64, 0), "hidden zone leaves transparency and preserves Split allocation");
    obs_data_set_bool(settings, "zone_visible", true); obs_property_modified(obs_properties_get(props, "zone_visible"), settings);
    change_string("distribution", "custom");
    for (const char *key : {"zone_uv_x", "zone_uv_y", "zone_uv_w", "zone_uv_h"}) obs_data_set_double(settings, key, 0.5);
    obs_property_modified(obs_properties_get(props, "zone_uv_x"), settings);
    change_string("zone_name", "Zona personalizada ñ"); publish(); image = capture(source);
    check(color_at(image, 16, 16, 64, 64, 0) && color_at(image, 16, 48, 64, 64, 0), "Custom UV samples the selected source region");
    obs_data_set_double(settings, "zone_dst_x", 0.125); obs_data_set_double(settings, "zone_dst_w", 0.25);
    obs_property_modified(obs_properties_get(props, "zone_dst_x"), settings); publish(); image = capture(source);
    check(!mapping::read(settings).automatic && color_at(image, 2, 16, 0, 0, 0, 0) && color_at(image, 16, 16, 64, 64, 0) && color_at(image, 28, 16, 0, 0, 0, 0), "manual destination creates transparent margins");
    change_string("zone_action", "duplicate");
    auto layout = mapping::read(settings);
    check(layout.zones.size() == 3 && layout.zones[2].id != layout.zones[0].id && layout.zones[2].uv.x == 0.5f && layout.zones[2].destination.x == 0.125f, "duplicate preserves geometry and UV with a new ID");
    change_string("zone_action", "up");
    check(mapping::selection(settings, mapping::read(settings)) == 1, "zone ordering moves selected zone");
    auto *roundtrip = obs_data_create_from_json(obs_data_get_json(settings));
    auto loaded = mapping::read(roundtrip);
    check(loaded.zones.size() == 3 && loaded.zones[0].name == "Zona personalizada ñ" && loaded.zones[0].uv.y == 0.5f && !loaded.automatic, "JSON round trip preserves names, order, geometry and UV");
    obs_data_release(roundtrip);
    publish();
    auto *saved_source = obs_save_source(source);
    auto *loaded_source = obs_load_source(saved_source);
    check(loaded_source != nullptr, "OBS source with zones loads");
    auto *loaded_settings = obs_source_get_settings(loaded_source);
    const auto native_loaded = mapping::read(loaded_settings);
    check(native_loaded.zones.size() == 3 && native_loaded.zones[0].name == "Zona personalizada ñ" &&
          native_loaded.zones[0].uv.y == 0.5f && native_loaded.zones[1].id == loaded.zones[1].id && children(loaded_source) == 1,
          "OBS save/load preserves zones and existing input reference");
    obs_data_release(loaded_settings); obs_source_release(loaded_source); obs_data_release(saved_source);
    for (int i = 0; i < 9; ++i) change_string("zone_action", "add");
    check(mapping::read(settings).zones.size() == 12, "zone count exceeds six");
    change_string("zone_action", "arrange"); change_string("distribution", "repeat"); publish();
    before = render_calls.load(); image = capture(source);
    check(render_calls.load() == before + 1, "twelve zones reuse a single input capture");
    bool covered = true;
    for (size_t i = 3; i < image.size(); i += 4) covered = covered && image[i] >= 126 && image[i] <= 130;
    check(covered, "twelve automatically arranged zones cover the canvas without gaps or double alpha");
    for (int i = 0; i < 12; ++i) change_string("zone_action", "delete");
    publish(); pixel_check(source, true);
    check(mapping::read(settings).zones.empty(), "last zone can be removed and empty layout persists");
    change_string("zone_action", "add"); change_string("zone_action", "arrange");
    change_string("distribution", "split"); publish(); patterned = false;
    obs_properties_destroy(props); obs_data_release(settings);
}
static void test_polygon(obs_source_t *source)
{
    patterned=true;
    auto *settings=obs_source_get_settings(source);
    auto *props=obs_source_properties(source);
    auto layout=mapping::read(settings);
    auto publish=[&] {
        for(auto &zone:layout.zones) mapping::compile_polygon(zone.polygon);
        mapping::write(settings,layout); obs_source_update(source,settings);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    };
    auto baseline=capture(source);
    obs_data_set_bool(settings,"zone_polygon",true);
    obs_property_modified(obs_properties_get(props,"zone_polygon"),settings);
    layout=mapping::read(settings); publish();
    check(capture(source)==baseline,"rectangle Polygon matches legacy rectangular output including alpha");
    obs_data_set_string(settings,"vertex_action","insert"); obs_property_modified(obs_properties_get(props,"vertex_action"),settings);
    layout=mapping::read(settings);
    check(layout.zones[0].polygon.vertices.size()==5 && layout.zones[0].polygon.error.empty(),"native properties insert vertex with interpolated UV");
    obs_data_set_double(settings,"vertex_y",0.125); obs_property_modified(obs_properties_get(props,"vertex_y"),settings);
    layout=mapping::read(settings);
    check(layout.zones[0].polygon.vertices[1].y==0.125f,"native properties edit selected vertex");
    obs_data_set_string(settings,"vertex_action","remove"); obs_property_modified(obs_properties_get(props,"vertex_action"),settings);
    check(mapping::read(settings).zones[0].polygon.vertices.size()==4,"native properties remove vertex");
    layout.zones[0].polygon.vertices={{0,0,0,0},{1,0,1,0},{0,1,0,1}}; publish();
    auto image=capture(source);
    check(color_at(image,8,8,64,0,0) && color_at(image,48,8,0,64,0) && color_at(image,8,48,0,0,64) && color_at(image,48,48,0,0,0,0),
        "triangle samples input and leaves pixels outside transparent");
    layout.zones[0].polygon.vertices={{0,0,0,0},{1,0,1,0},{1,.375f,1,.375f},{.375f,.375f,.375f,.375f},{.375f,1,.375f,1},{0,1,0,1}};
    publish(); image=capture(source);
    bool alpha_valid=true;
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
        const int alpha=image[(y*64+x)*4+3], expected=x<24 || y<24 ? 128 : 0;
        alpha_valid=alpha_valid && std::abs(alpha-expected)<=2;
    }
    check(alpha_valid,"concave Polygon covers all expected pixels without alpha seams or filling its notch");
    std::reverse(layout.zones[0].polygon.vertices.begin(),layout.zones[0].polygon.vertices.end()); publish();
    check(capture(source)==image,"both Polygon winding directions render identically");
    layout.mode=mapping::Mode::Custom; obs_data_set_string(settings,"distribution","custom");
    layout.zones[0].uv={.5f,.5f,.5f,.5f}; publish(); image=capture(source);
    check(color_at(image,8,8,64,64,0) && color_at(image,8,48,64,64,0) && color_at(image,48,48,0,0,0,0),"Polygon composes with Custom UV");
    obs_data_set_string(settings,"zone_action","duplicate"); obs_property_modified(obs_properties_get(props,"zone_action"),settings);
    auto duplicate=mapping::read(settings);
    check(duplicate.zones.size()==2 && duplicate.zones[1].polygon.vertices.size()==6 && duplicate.zones[1].polygon.enabled,"zone duplicate preserves Polygon");
    auto *saved=obs_save_source(source); auto *restored=obs_load_source(saved);
    auto *restored_settings=obs_source_get_settings(restored); auto persisted=mapping::read(restored_settings);
    check(persisted.zones[0].polygon.enabled && persisted.zones[0].polygon.vertices.size()==6 && persisted.zones[0].polygon.error.empty() &&
        persisted.zones[0].polygon.vertices[1].y==layout.zones[0].polygon.vertices[1].y,"OBS save/load preserves polygon vertices and UV");
    obs_data_release(restored_settings); obs_source_release(restored); obs_data_release(saved);
    layout.zones[0].polygon.vertices={{0,0,1,1},{1,0,1,1},{0,1,1,1}};
    layout.zones[0].uv={}; layout.mode=mapping::Mode::Repeat; obs_data_set_string(settings,"distribution","repeat"); publish(); image=capture(source);
    check(color_at(image,8,8,64,64,0),"per-vertex UV controls texture sampling independently of destination position");
    layout.zones[0].polygon.vertices={{0,0,0,0},{1,0,1,0},{0,1,0,1}};
    layout.zones.push_back(layout.zones[0]); layout.zones[1].id="polygon-copy"; mapping::arrange(layout);
    layout.mode=mapping::Mode::Split; obs_data_set_string(settings,"distribution","split"); publish(); image=capture(source);
    check(color_at(image,4,8,64,0,0) && color_at(image,36,8,0,64,0) && color_at(image,28,56,0,0,0,0) && color_at(image,60,56,0,0,0,0),"Polygon composes with Split across two zones");
    layout.zones.resize(1); mapping::arrange(layout);
    layout.zones[0].polygon.vertices={{0,0,0,0},{1,1,1,1},{1,0,1,0},{0,1,0,1}}; publish(); pixel_check(source,true);
    check(!layout.zones[0].polygon.error.empty(),"invalid crossing Polygon reports an error and renders transparent");
    layout.zones[0].polygon.enabled=false; publish();
    check(capture(source)==baseline,"disabling invalid Polygon restores rectangular output");
    layout.zones[0].polygon=mapping::Polygon{}; publish(); patterned=false;
    obs_properties_destroy(props); obs_data_release(settings);
}
static void test_mesh(obs_source_t *source)
{
    patterned=true;
    auto *settings=obs_source_get_settings(source);
    auto *props=obs_source_properties(source);
    auto layout=mapping::read(settings);
    auto publish=[&] {
        mapping::write(settings,layout); obs_source_update(source,settings);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    };
    auto change_bool=[&](const char *key,bool value) { obs_data_set_bool(settings,key,value); obs_property_modified(obs_properties_get(props,key),settings); };
    auto change_double=[&](const char *key,double value) { obs_data_set_double(settings,key,value); obs_property_modified(obs_properties_get(props,key),settings); };
    auto action=[&](const char *value) { obs_data_set_string(settings,"mesh_action",value); obs_property_modified(obs_properties_get(props,"mesh_action"),settings); };
    auto baseline=capture(source);
    change_bool("zone_mesh",true); layout=mapping::read(settings); publish();
    auto image=capture(source);
    bool neutral=true; for(size_t i=0;i<image.size();++i) neutral=neutral && std::abs(int(image[i])-baseline[i])<=1;
    check(neutral,"neutral Smooth Mesh preserves rectangular texture and alpha without seams");
    change_double("mesh_horizontal",.25); layout=mapping::read(settings); publish(); image=capture(source);
    check(color_at(image,16,4,0,0,0,0)&&color_at(image,16,30,64,0,0),"horizontal quick curvature creates expected curved transparent border");
    change_double("mesh_horizontal",-.25); layout=mapping::read(settings); publish(); image=capture(source);
    check(color_at(image,16,60,0,0,0,0),"negative curvature bends in opposite direction");
    action("reset_curves"); layout=mapping::read(settings);
    for(auto &point:layout.zones[0].mesh.points) point.x=.125f;
    publish(); image=capture(source);
    check(color_at(image,4,16,0,0,0,0)&&color_at(image,12,16,64,0,0)&&color_at(image,44,16,0,64,0),"control field translates textured surface with transparent margins");
    obs_data_set_int(settings,"mesh_columns",7); obs_property_modified(obs_properties_get(props,"mesh_columns"),settings);
    layout=mapping::read(settings); publish();
    check(layout.zones[0].mesh.points.size()==28 && capture(source)==image,"grid resizing preserves affine field and GPU output");
    action("reset_all");
    obs_data_set_int(settings,"mesh_selected",5); obs_property_modified(obs_properties_get(props,"mesh_selected"),settings);
    change_bool("mesh_soft",true); change_double("mesh_radius",1); change_double("mesh_strength",.5);
    change_double("mesh_dx",.12);
    layout=mapping::read(settings);
    check(std::abs(layout.zones[0].mesh.points[5].x-.06f)<1e-5 && layout.zones[0].mesh.points[6].x>0 &&
        layout.zones[0].mesh.points[6].x<.06f,"native Soft Selection moves neighbors with radius and strength");
    const auto preserved=layout.zones[0].mesh.points;
    change_double("mesh_horizontal",.1); action("reset_curves"); layout=mapping::read(settings);
    check(layout.zones[0].mesh.horizontal==0 && layout.zones[0].mesh.points[5].x==preserved[5].x,"resetting curvature preserves manual control edits");
    action("reset_all"); layout=mapping::read(settings);
    layout.zones[0].polygon.enabled=true;
    layout.zones[0].polygon.vertices={{0,0,0,0},{1,0,1,0},{1,.375f,1,.375f},{.375f,.375f,.375f,.375f},{.375f,1,.375f,1},{0,1,0,1}};
    layout.zones[0].mesh.horizontal=.125f; publish();
    const auto before=render_calls.load(); image=capture(source);
    check(render_calls.load()==before+1,"Polygon plus Smooth Mesh still captures input once");
    check(color_at(image,32,1,0,0,0,0)&&color_at(image,8,40,0,0,64)&&color_at(image,40,48,0,0,0,0),"concave Polygon and Smooth Mesh deform together while preserving transparent notch");
    layout.zones[0].mesh.vertical=.1f; publish();
    check(capture(source)!=image,"vertical quick curvature combines with horizontal curvature");
    layout.zones[0].mesh.vertical=0; layout.zones[0].mesh.wave=true; publish();
    check(capture(source)!=image,"wave profile changes curved GPU output");
    layout.zones[0].mesh.wave=false;
    layout.mode=mapping::Mode::Custom; obs_data_set_string(settings,"distribution","custom");
    layout.zones[0].uv={.5f,.5f,.5f,.5f}; publish(); image=capture(source);
    check(color_at(image,8,40,64,64,0)&&color_at(image,40,48,0,0,0,0),"Polygon plus Smooth Mesh preserves Custom UV sampling");
    layout.zones[0].mesh.points[5]={.02f,.03f}; layout.zones[0].mesh.soft=true; layout.zones[0].mesh.radius=.7f; layout.zones[0].mesh.strength=.6f; publish();
    auto *saved=obs_save_source(source); auto *loaded_source=obs_load_source(saved);
    auto *loaded_settings=obs_source_get_settings(loaded_source); const auto loaded=mapping::read(loaded_settings);
    check(loaded.zones[0].mesh.enabled && loaded.zones[0].polygon.enabled && loaded.zones[0].mesh.points[5].x==.02f &&
        loaded.zones[0].mesh.horizontal==.125f && loaded.zones[0].mesh.soft && loaded.zones[0].mesh.radius==.7f && loaded.zones[0].mesh.strength==.6f,
        "OBS save/load preserves combined geometry, controls, curvature and Soft Selection");
    obs_data_release(loaded_settings); obs_source_release(loaded_source); obs_data_release(saved);
    obs_data_set_string(settings,"zone_action","duplicate"); obs_property_modified(obs_properties_get(props,"zone_action"),settings);
    const auto duplicated=mapping::read(settings);
    check(duplicated.zones.size()==2 && duplicated.zones[1].polygon.enabled && duplicated.zones[1].mesh.enabled && duplicated.zones[1].mesh.points[5].x==.02f,
        "duplicating zone preserves Polygon and mesh together");
    layout=duplicated; layout.mode=mapping::Mode::Split; obs_data_set_string(settings,"distribution","split"); publish(); image=capture(source);
    check(color_at(image,4,40,0,0,64)&&color_at(image,36,40,64,64,0)&&color_at(image,20,48,0,0,0,0)&&color_at(image,52,48,0,0,0,0),
        "Polygon plus Smooth Mesh preserves Split regions across multiple zones");
    layout.zones.resize(1); mapping::arrange(layout);
    layout.zones[0].polygon=mapping::Polygon{}; layout.zones[0].mesh=mapping::Mesh{}; layout.zones[0].mesh.enabled=true;
    mapping::resize_mesh(layout.zones[0].mesh,2,2);
    layout.zones[0].mesh.points[1].x=layout.zones[0].mesh.points[3].x=-1;
    layout.zones[0].uv={}; layout.mode=mapping::Mode::Repeat; obs_data_set_string(settings,"distribution","repeat"); publish(); pixel_check(source,true);
    check(!mapping::compile_surface(layout.zones[0].mesh,layout.zones[0].polygon).error.empty(),"collapsed mesh renders transparent with validation error");
    layout.zones[0].mesh.enabled=false; publish(); check(capture(source)==baseline,"disabling invalid mesh restores previous rectangular output");
    layout.zones[0].mesh=mapping::Mesh{}; publish(); patterned=false;
    obs_properties_destroy(props); obs_data_release(settings);
}
static void test_masks(obs_source_t *source,const std::string &directory)
{
    patterned=false;
    auto *settings=obs_source_get_settings(source); auto *props=obs_source_properties(source);
    auto layout=mapping::read(settings);
    auto publish=[&] { mapping::write(settings,layout); obs_source_update(source,settings); std::this_thread::sleep_for(std::chrono::milliseconds(150)); };
    auto baseline=capture(source);
    obs_data_set_int(settings,"mask_kind",1); obs_property_modified(obs_properties_get(props,"mask_kind"),settings);
    layout=mapping::read(settings); publish(); auto image=capture(source);
    check(color_at(image,32,32,64,0,0)&&color_at(image,1,1,0,0,0,0),"ellipse mask keeps center and makes corners transparent");
    layout.zones[0].mask.invert=true; publish(); image=capture(source);
    check(color_at(image,32,32,0,0,0,0)&&color_at(image,1,1,64,0,0),"mask inversion preserves premultiplied color and alpha");
    layout.zones[0].mask.strength=.5f; publish(); image=capture(source);
    check(color_at(image,32,32,32,0,0,64),"partial mask strength scales color and alpha together");
    layout.zones[0].mask.invert=false; layout.zones[0].mask.strength=1; layout.zones[0].mask.feather=.2f; publish(); image=capture(source);
    const auto *edge=image.data()+(32*64+4)*4;
    check(edge[3]>0&&edge[3]<128&&std::abs(2*int(edge[0])-int(edge[3]))<=2,"ellipse feather has partial alpha without dark fringes");
    layout.zones[0].mask.kind=2; publish(); image=capture(source);
    check(color_at(image,32,32,64,0,0)&&image[(32*64+4)*4+3]<128,"rectangle mask supports feathered borders");
    auto &crop=layout.zones[0].mask;crop.kind=1;crop.feather=0;crop.radius=.25f;publish();image=capture(source);
    check(color_at(image,32,32,64,0,0)&&color_at(image,8,32,0,0,0,0),"smaller circle radius crops video without moving its content");
    crop.center_x=.25f;crop.center_y=.75f;crop.radius=.125f;publish();image=capture(source);
    check(color_at(image,16,48,64,0,0)&&color_at(image,32,32,0,0,0,0),"circle center positions crop independently in X and Y");
    crop.width=.5f;publish();image=capture(source);
    check(color_at(image,22,48,0,0,0,0)&&color_at(image,16,54,64,0,0),"ellipse width and height scale independently of radius");
    crop.kind=2;crop.center_x=.25f;crop.center_y=.5f;crop.width=.5f;crop.height=.25f;publish();image=capture(source);
    check(color_at(image,16,32,64,0,0)&&color_at(image,40,32,0,0,0,0)&&color_at(image,16,8,0,0,0,0),"rectangle size and position select a smaller video region");
    crop.invert=true;publish();image=capture(source);
    check(color_at(image,16,32,0,0,0,0)&&color_at(image,40,32,64,0,0),"inverted resized rectangle cuts a transparent hole");
    crop.strength=0;publish();check(capture(source)==baseline,"zero intensity bypasses resized crop exactly");
    crop.strength=1;crop.invert=false;crop.width=0;publish();pixel_check(source,true);
    crop.kind=1;crop.width=1;crop.radius=0;publish();pixel_check(source,true);
    crop.kind=2;mapping::reset_mask_shape(crop);crop.width=1.1f;publish();pixel_check(source,true);
    mapping::reset_mask_shape(crop);crop.kind=0;publish();check(capture(source)==baseline,"reset shape restores legacy full-zone output");
    crop.kind=1;publish();obs_data_set_double(settings,"mask_radius",.2);obs_property_modified(obs_properties_get(props,"mask_radius"),settings);
    check(std::abs(mapping::read(settings).zones[0].mask.radius-.2f)<.00001f,"native properties persist circle radius");
    obs_data_set_string(settings,"mask_action","reset_shape");obs_property_modified(obs_properties_get(props,"mask_action"),settings);layout=mapping::read(settings);
    check(layout.zones[0].mask.radius==.5f&&layout.zones[0].mask.width==1,"native reset shape restores defaults");
    auto &mask=layout.zones[0].mask;
    mask.kind=3; mask.path=directory+"/máscara-ñ.png"; publish(); image=capture(source);
    check(color_at(image,8,32,64,0,0)&&color_at(image,56,32,0,0,0,0),"PNG alpha mask loads through Unicode path and clips expected half");
    mask.path=directory+"/half.svg"; publish(); auto svg_image=capture(source);
    check(color_at(svg_image,8,32,64,0,0)&&color_at(svg_image,56,32,0,0,0,0),"SVG path mask clips expected half on GPU");
    mask.path=directory+"/gray.png"; publish(); image=capture(source);
    check(color_at(image,32,32,32,0,0,64),"PNG half alpha multiplies source alpha exactly once");
    mask.luminance=true; publish(); image=capture(source);
    check(color_at(image,32,32,16,0,0,32),"luminance times alpha masks premultiplied source correctly");
    mask.path=directory+"/partial.svg"; publish(); image=capture(source);
    check(color_at(image,32,32,32,0,0,64),"SVG partial alpha avoids double premultiplication in luminance mode");
    mask.path=directory+"/missing.png"; publish(); pixel_check(source,true);
    mask.strength=0; publish(); check(capture(source)==baseline,"zero mask strength bypasses missing image and restores original pixels");
    mask.strength=1; mask.luminance=false; mask.path=directory+"/reload.png";
    check(fixture_png(mask.path,8,8,std::vector<uint8_t>(8*8*4,255)),"reload PNG fixture created");
    publish(); check(capture(source)==baseline,"fully opaque PNG mask preserves original pixels");
    check(fixture_png(mask.path,8,8,std::vector<uint8_t>(8*8*4,0)),"reload PNG fixture changed on disk");
    publish(); check(capture(source)==baseline,"unchanged path uses cached mask without reading file on each update");
    obs_data_set_string(settings,"mask_action","reload"); obs_property_modified(obs_properties_get(props,"mask_action"),settings);
    layout=mapping::read(settings); publish(); pixel_check(source,true);
    layout.zones[0].mask.path=directory+"/máscara-ñ.png";
    layout.zones[0].mesh.enabled=true; layout.zones[0].mesh.horizontal=.125f;
    layout.zones[0].polygon.enabled=true;
    layout.zones[0].polygon.vertices={{0,0,0,0},{1,0,1,0},{1,.375f,1,.375f},{.375f,.375f,.375f,.375f},{.375f,1,.375f,1},{0,1,0,1}};
    publish(); const auto before=render_calls.load(); image=capture(source);
    check(render_calls.load()==before+1,"mask plus Polygon and mesh still captures input once");
    check(color_at(image,8,40,64,0,0)&&color_at(image,48,8,0,0,0,0)&&color_at(image,32,1,0,0,0,0),"image mask follows deformed Polygon in local coordinates");
    layout.mode=mapping::Mode::Custom; obs_data_set_string(settings,"distribution","custom"); layout.zones[0].uv={.5f,.5f,.5f,.5f}; patterned=true; publish(); image=capture(source);
    check(color_at(image,8,40,64,64,0)&&color_at(image,48,8,0,0,0,0),"mask coordinates remain independent of Custom UV");
    layout.zones.push_back(layout.zones[0]); layout.zones[1].id="second-mask"; layout.zones[1].mask.invert=true; mapping::arrange(layout);
    layout.mode=mapping::Mode::Split; obs_data_set_string(settings,"distribution","split"); publish(); image=capture(source);
    check(color_at(image,4,40,0,0,64)&&color_at(image,28,8,0,0,0,0)&&color_at(image,36,8,0,0,0,0)&&color_at(image,60,8,0,64,0),"Split zones have independent masks and inversion");
    layout.zones[0].mask.strength=.7f; layout.zones[0].mask.feather=.1f; publish();
    auto *saved=obs_save_source(source); auto *restored=obs_load_source(saved); auto *loaded_settings=obs_source_get_settings(restored);
    const auto loaded=mapping::read(loaded_settings);
    check(loaded.zones.size()==2&&loaded.zones[0].mask.path==layout.zones[0].mask.path&&loaded.zones[0].mask.strength==.7f&&
        loaded.zones[1].mask.invert&&loaded.zones[0].polygon.enabled&&loaded.zones[0].mesh.enabled,"OBS save/load preserves mask settings with combined geometry");
    check(capture(restored)==capture(source),"restored source recreates mask textures and matches saved output pixels");
    obs_data_release(loaded_settings); obs_source_release(restored); obs_data_release(saved);
    obs_data_set_string(settings,"zone_action","duplicate"); obs_property_modified(obs_properties_get(props,"zone_action"),settings);
    const auto duplicated=mapping::read(settings);
    check(duplicated.zones.size()==3&&duplicated.zones[2].mask.strength==layout.zones[0].mask.strength&&
        duplicated.zones[2].mask.invert==layout.zones[0].mask.invert&&duplicated.zones[2].mask.path==layout.zones[0].mask.path,"zone duplication preserves selected zone mask configuration");
    layout.zones.resize(1); layout.zones[0].mask=mapping::Mask{}; layout.zones[0].polygon=mapping::Polygon{}; layout.zones[0].mesh=mapping::Mesh{};
    layout.zones[0].uv={}; mapping::arrange(layout); layout.mode=mapping::Mode::Repeat; obs_data_set_string(settings,"distribution","repeat"); publish(); patterned=false;
    obs_properties_destroy(props); obs_data_release(settings);
}
static void test_transforms(obs_source_t *source)
{
    auto *saved=obs_source_get_settings(source);auto *settings=obs_data_create_from_json(obs_data_get_json(saved));obs_data_set_int(settings,"canvas_width",64);obs_data_set_int(settings,"canvas_height",64);obs_data_set_string(settings,"distribution","repeat");
    mapping::Layout layout;layout.automatic=false;layout.mode=mapping::Mode::Repeat;mapping::Zone zone;zone.id="transform-test";zone.name="Transform";zone.destination={.125f,.125f,.75f,.75f};layout.zones.push_back(zone);
    auto publish=[&]{mapping::write(settings,layout);obs_source_update(source,settings);std::this_thread::sleep_for(std::chrono::milliseconds(100));};
    patterned=false;publish();const auto baseline=capture(source);auto &t=layout.zones[0].transform;
    t.x=-.5f;publish();auto image=capture(source);check(color_at(image,4,32,64,0,0)&&color_at(image,48,32,0,0,0,0),"negative transform position leaves canvas and clips output");
    t.x=2;publish();pixel_check(source,true);check(obs_source_get_width(source)==64&&obs_source_get_height(source)==64,"outside zone never changes output canvas dimensions");
    t=mapping::Transform{};t.z=2;publish();image=capture(source);check(color_at(image,32,32,64,0,0)&&color_at(image,16,32,0,0,0,0),"positive depth moves zone away from common camera");
    t=mapping::Transform{};t.scale=.5f;t.pivot_x=t.pivot_y=0;publish();image=capture(source);check(color_at(image,12,12,64,0,0)&&color_at(image,48,32,0,0,0,0),"scale uses independent zone pivot");
    patterned=true;t=mapping::Transform{};t.rz=90;publish();image=capture(source);check(color_at(image,48,16,64,0,0)&&color_at(image,16,16,0,0,64)&&color_at(image,16,48,64,64,0)&&color_at(image,48,48,0,64,0),"Z rotation rotates all four video quadrants correctly");
    t=mapping::Transform{};t.rx=25;t.ry=45;publish();image=capture(source);
    auto mappedColor=[&](float u,float v){auto q=mapping::project(t,.125f,.125f,.75f,.75f,u,v,64,64);if(!q.visible)return false;const int x=int(q.x*64),y=int(q.y*64);if(x<0||x>=64||y<0||y>=64)return false;return color_at(image,x,y,u<.5f?(v<.5f?64:0):(v<.5f?0:64),u>=.5f?64:0,u<.5f&&v>=.5f?64:0);};
    check(mappedColor(.25f,.25f)&&mappedColor(.75f,.25f)&&mappedColor(.25f,.75f)&&mappedColor(.75f,.75f),"CPU controls and GPU camera projection agree under X and Y rotation");
    t=mapping::Transform{};t.pinned=true;t.corners={{{.2f,.1f},{.8f,.1f},{1,1},{0,1}}};publish();image=capture(source);
    bool perspective=true;for(float u:{.15f,.3f,.4f,.6f,.7f,.85f})for(float v:{.15f,.35f,.65f,.85f})perspective&=mappedColor(u,v);
    check(perspective,"four-corner homography preserves perspective-correct video sampling across both triangles");
    t.ry=25;publish();image=capture(source);check(mappedColor(.25f,.25f)&&mappedColor(.75f,.25f)&&mappedColor(.25f,.75f)&&mappedColor(.75f,.75f),"corner pin and 3D rotation share the projected zone pivot");t.ry=0;
    layout.zones[0].mask.kind=1;layout.zones[0].mask.radius=.1f;publish();image=capture(source);auto outside=mapping::project(t,.125f,.125f,.75f,.75f,.2f,.2f,64,64);check(color_at(image,int(outside.x*64),int(outside.y*64),0,0,0,0),"resized mask follows projective corner pinning");
    layout.zones[0].mask.kind=0;t.corners[1]=t.corners[3];publish();pixel_check(source,true);
    t=mapping::Transform{};patterned=false;publish();check(capture(source)==baseline,"reset transformation exactly restores original untransformed pixels");
    obs_source_update(source,saved);std::this_thread::sleep_for(std::chrono::milliseconds(100));obs_data_release(settings);obs_data_release(saved);
}
static void test_canvas_bounds(obs_source_t *source)
{
    auto *settings=obs_source_get_settings(source);auto layout=mapping::read(settings);
    layout.zones[0].mesh.enabled=true;for(auto &point:layout.zones[0].mesh.points)point.x=.5f;
    mapping::write(settings,layout);obs_data_set_int(settings,"canvas_width",32);obs_data_set_int(settings,"canvas_height",32);
    obs_source_update(source,settings);std::this_thread::sleep_for(std::chrono::milliseconds(100));auto image=capture(source);
    check(color_at(image,24,16,64,0,0)&&color_at(image,40,16,0,0,0,0)&&color_at(image,24,40,0,0,0,0),"deformed source clips to configured canvas when rendered into a larger scene");
    obs_data_release(settings);
}
int main(int argc, char **argv)
{
    check(argc == 5 && obs_startup("en-US", nullptr, nullptr), "libobs startup");
    // Test-host compatibility helper; plugin itself uses no deprecated API.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    std::string data_path = std::string(argv[3]) + "/";
    obs_add_data_path(data_path.c_str());
#pragma clang diagnostic pop
    obs_video_info video{};
    std::string graphics = std::string(argv[3]) + "/../../bin/64bit/libobs-d3d11.dll";
    std::string dll_directory = std::string(argv[3]) + "/../../bin/64bit";
    SetDllDirectoryA(dll_directory.c_str());
    video.graphics_module = graphics.c_str();
    video.fps_num = 60; video.fps_den = 1;
    video.base_width = video.base_height = video.output_width = video.output_height = 64;
    video.output_format = VIDEO_FORMAT_RGBA;
    video.colorspace = VIDEO_CS_709; video.range = VIDEO_RANGE_FULL;
    video.scale_type = OBS_SCALE_BILINEAR;
    check(obs_reset_video(&video) == OBS_VIDEO_SUCCESS, "D3D11 video initialized");
    obs_module_t *module = nullptr;
    check(obs_open_module(&module, argv[1], argv[2]) == MODULE_SUCCESS && obs_init_module(module), "mapper loaded");
    obs_source_info fixture{};
    fixture.id = "mapper_test_fixture"; fixture.type = OBS_SOURCE_TYPE_INPUT;
    fixture.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    fixture.get_name = fixture_name; fixture.create = fixture_create; fixture.destroy = fixture_destroy;
    fixture.get_width = fixture_width; fixture.get_height = fixture_height; fixture.video_render = fixture_render;
    obs_register_source(&fixture);
    auto *input = obs_source_create(fixture.id, "Existing input", nullptr, nullptr);
    auto *settings = obs_data_create();
    obs_data_set_int(settings, "canvas_width", 64); obs_data_set_int(settings, "canvas_height", 64);
    auto *a = obs_source_create("obs_screen_mapper", "A", settings, nullptr);
    auto *b = obs_source_create("obs_screen_mapper", "B", settings, nullptr);
    obs_data_release(settings);
    pixel_check(a, true);
    select_input(a, obs_source_get_uuid(input)); check(children(a) == 1, "live input connected");
    pixel_check(a, false);
    test_zones(a);
    test_polygon(a);
    test_mesh(a);
    test_masks(a,argv[4]);
    test_transforms(a);
    test_canvas_bounds(a);
    patterned=true;test_cached_geometry(a);patterned=false;
    select_input(a, obs_source_get_uuid(a)); check(children(a) == 0, "plugin rejects self selection");
    pixel_check(a, true);
    select_input(a, obs_source_get_uuid(input));
    select_input(b, obs_source_get_uuid(a)); check(children(b) == 2, "live mapper chain");
    select_input(a, obs_source_get_uuid(b)); check(children(a) == 0, "plugin rejects indirect cycle on update");
    select_input(b, ""); select_input(a, obs_source_get_uuid(input));
    obs_source_remove(input);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    check(children(a) == 0, "deleted source detached"); pixel_check(a, true);
    obs_source_release(b); obs_source_release(a); obs_source_release(input);
    obs_shutdown(); std::puts("PASS: D3D11 phase 7 validation complete");
}
