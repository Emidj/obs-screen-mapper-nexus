// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "i18n.hpp"
#include <obs-module.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "polygon.hpp"
#include "mesh.hpp"
#include "mask.hpp"
#include "transform.hpp"

namespace mapping {
struct Rect { float x = 0, y = 0, w = 1, h = 1; };
struct Zone { std::string id, name; bool visible = true; Rect destination, uv; Polygon polygon; Mesh mesh; std::shared_ptr<const Surface> surface; Mask mask; Transform transform; };
enum class Mode { Split, Repeat, Custom };
struct Layout { std::vector<Zone> zones; Mode mode = Mode::Split; bool automatic = true; };
inline float bounded(double value, float fallback = 0)
{
    return std::isfinite(value) ? float(std::clamp(value, 0.0, 1.0)) : fallback;
}
inline Rect sanitize(Rect r)
{
    r.x = bounded(r.x); r.y = bounded(r.y);
    r.w = std::min(bounded(r.w), 1 - r.x); r.h = std::min(bounded(r.h), 1 - r.y);
    return r;
}
inline Rect sanitize_destination(Rect r)
{
    // Placement can extend beyond the canvas. UV cropping must remain inside its image.
    return {bounded(r.x),bounded(r.y),bounded(r.w),bounded(r.h)};
}
inline void arrange(Layout &layout)
{
    if (!layout.automatic || layout.zones.empty()) return;
    const float count = float(layout.zones.size());
    for (size_t i = 0; i < layout.zones.size(); ++i)
        layout.zones[i].destination = {float(i) / count, 0, 1 / count, 1};
}
inline Rect source_uv(const Layout &layout, size_t index)
{
    if (layout.mode == Mode::Custom) return layout.zones[index].uv;
    if (layout.mode == Mode::Repeat) return {};
    const float count = float(layout.zones.size());
    return {float(index) / count, 0, 1 / count, 1};
}
inline Rect read_rect(obs_data_t *data, const char *prefix,bool clip_to_image=true)
{
    Rect r;
    float *values[] = {&r.x, &r.y, &r.w, &r.h};
    const char *suffix[] = {"x", "y", "w", "h"};
    for (int i = 0; i < 4; ++i) {
        const std::string key = std::string(prefix) + suffix[i];
        if (obs_data_has_user_value(data, key.c_str())) *values[i] = bounded(obs_data_get_double(data, key.c_str()));
    }
    return clip_to_image?sanitize(r):sanitize_destination(r);
}
inline void write_rect(obs_data_t *data, const char *prefix, Rect r)
{
    const float values[] = {r.x, r.y, r.w, r.h};
    const char *suffix[] = {"x", "y", "w", "h"};
    for (int i = 0; i < 4; ++i) obs_data_set_double(data, (std::string(prefix) + suffix[i]).c_str(), values[i]);
}
inline Mesh read_mesh(obs_data_t *data)
{
    Mesh mesh;
    mesh.enabled=obs_data_get_bool(data,"mesh_enabled");
    mesh.smooth=!obs_data_has_user_value(data,"mesh_smooth") || obs_data_get_bool(data,"mesh_smooth");
    mesh.soft=obs_data_get_bool(data,"mesh_soft"); mesh.wave=obs_data_get_bool(data,"mesh_wave");
    mesh.domain=int(std::clamp<long long>(obs_data_get_int(data,"mesh_domain"),-1,3));
    if(obs_data_has_user_value(data,"mesh_domain_cx"))mesh.center_x=float(obs_data_get_double(data,"mesh_domain_cx"));
    if(obs_data_has_user_value(data,"mesh_domain_cy"))mesh.center_y=float(obs_data_get_double(data,"mesh_domain_cy"));
    if(obs_data_has_user_value(data,"mesh_domain_rx"))mesh.axis_x=float(obs_data_get_double(data,"mesh_domain_rx"));
    if(obs_data_has_user_value(data,"mesh_domain_ry"))mesh.axis_y=float(obs_data_get_double(data,"mesh_domain_ry"));
    const int columns=obs_data_has_user_value(data,"mesh_columns")?int(std::clamp<long long>(obs_data_get_int(data,"mesh_columns"),2,16)):4;
    const int rows=obs_data_has_user_value(data,"mesh_rows")?int(std::clamp<long long>(obs_data_get_int(data,"mesh_rows"),2,16)):4;
    resize_mesh(mesh,columns,rows);
    mesh.horizontal=float(obs_data_get_double(data,"mesh_horizontal")); mesh.vertical=float(obs_data_get_double(data,"mesh_vertical"));
    if(obs_data_has_user_value(data,"mesh_radius")) mesh.radius=float(obs_data_get_double(data,"mesh_radius"));
    if(obs_data_has_user_value(data,"mesh_strength")) mesh.strength=float(obs_data_get_double(data,"mesh_strength"));
    if(auto *points=obs_data_get_array(data,"mesh_points")) {
        mesh.points.clear();
        if(obs_data_array_count(points)==size_t(columns*rows)) for(size_t i=0;i<obs_data_array_count(points);++i) {
            auto *point=obs_data_array_item(points,i);
            mesh.points.push_back({float(obs_data_get_double(point,"x")),float(obs_data_get_double(point,"y"))}); obs_data_release(point);
        }
        obs_data_array_release(points);
    }
    validate_mesh(mesh); return mesh;
}
inline void write_mesh(obs_data_t *data,const Mesh &mesh)
{
    obs_data_set_bool(data,"mesh_enabled",mesh.enabled); obs_data_set_bool(data,"mesh_smooth",mesh.smooth);
    obs_data_set_bool(data,"mesh_soft",mesh.soft); obs_data_set_bool(data,"mesh_wave",mesh.wave);
    obs_data_set_int(data,"mesh_domain",mesh.domain);obs_data_set_double(data,"mesh_domain_cx",mesh.center_x);obs_data_set_double(data,"mesh_domain_cy",mesh.center_y);obs_data_set_double(data,"mesh_domain_rx",mesh.axis_x);obs_data_set_double(data,"mesh_domain_ry",mesh.axis_y);
    obs_data_set_int(data,"mesh_columns",mesh.columns); obs_data_set_int(data,"mesh_rows",mesh.rows);
    obs_data_set_double(data,"mesh_horizontal",mesh.horizontal); obs_data_set_double(data,"mesh_vertical",mesh.vertical);
    obs_data_set_double(data,"mesh_radius",mesh.radius); obs_data_set_double(data,"mesh_strength",mesh.strength);
    auto *points=obs_data_array_create();
    for(const auto &point:mesh.points) {
        auto *item=obs_data_create(); obs_data_set_double(item,"x",point.x); obs_data_set_double(item,"y",point.y);
        obs_data_array_push_back(points,item); obs_data_release(item);
    }
    obs_data_set_array(data,"mesh_points",points); obs_data_array_release(points);
}
inline Layout read(obs_data_t *settings)
{
    Layout layout;
    const std::string mode = obs_data_get_string(settings, "distribution");
    layout.mode = mode == "repeat" ? Mode::Repeat : mode == "custom" ? Mode::Custom : Mode::Split;
    layout.automatic = !obs_data_has_user_value(settings, "automatic_layout") || obs_data_get_bool(settings, "automatic_layout");
    auto *array = obs_data_get_array(settings, "zones");
    if (!array) { Zone zone; zone.id = "1"; zone.name = mapper_i18n::text("Zona 1"); layout.zones.push_back(std::move(zone)); }
    else {
        for (size_t i = 0; i < obs_data_array_count(array); ++i) {
            auto *item = obs_data_array_item(array, i);
            Zone zone;
            zone.id = obs_data_get_string(item, "id");
            // Repair missing/duplicate IDs deterministically, preserving valid IDs.
            if (zone.id.empty() || std::any_of(layout.zones.begin(), layout.zones.end(), [&](const Zone &z) { return z.id == zone.id; })) {
                zone.id = "recovered-" + std::to_string(i);
                while (std::any_of(layout.zones.begin(), layout.zones.end(), [&](const Zone &z) { return z.id == zone.id; })) zone.id += "-";
            }
            zone.name = obs_data_get_string(item, "name");
            if (zone.name.empty()) zone.name = mapper_i18n::text("Zona ") + std::to_string(i + 1);
            zone.visible = !obs_data_has_user_value(item, "visible") || obs_data_get_bool(item, "visible");
            zone.destination = read_rect(item, "dst_",false); zone.uv = read_rect(item, "uv_");
            zone.polygon.enabled = obs_data_get_bool(item, "polygon_enabled");
            if (auto *points = obs_data_get_array(item, "polygon_vertices")) {
                zone.polygon.vertices.clear();
                for (size_t j = 0; j < obs_data_array_count(points); ++j) {
                    auto *point = obs_data_array_item(points, j);
                    zone.polygon.vertices.push_back({float(obs_data_get_double(point,"x")), float(obs_data_get_double(point,"y")),
                        float(obs_data_get_double(point,"u")), float(obs_data_get_double(point,"v"))});
                    obs_data_release(point);
                }
                obs_data_array_release(points);
            }
            compile_polygon(zone.polygon);
            zone.mesh=read_mesh(item);
            const auto mask_kind=obs_data_get_int(item,"mask_kind"); zone.mask.kind=mask_kind>=0 && mask_kind<=3?int(mask_kind):-1;
            zone.mask.path=obs_data_get_string(item,"mask_path");
            zone.mask.luminance=obs_data_get_bool(item,"mask_luminance"); zone.mask.invert=obs_data_get_bool(item,"mask_invert");
            if(obs_data_has_user_value(item,"mask_strength")) zone.mask.strength=float(obs_data_get_double(item,"mask_strength"));
            zone.mask.feather=float(obs_data_get_double(item,"mask_feather")); zone.mask.revision=obs_data_get_int(item,"mask_revision");
            for(const auto &field:mask_dimensions)if(obs_data_has_user_value(item,field.key))zone.mask.*(field.member)=float(obs_data_get_double(item,field.key));
            for(const auto &field:transform_fields)if(obs_data_has_user_value(item,field.key))zone.transform.*(field.member)=float(obs_data_get_double(item,field.key));
            zone.transform.pinned=obs_data_get_bool(item,"corner_pin");
            for(size_t k=0;k<4;++k)for(int axis=0;axis<2;++axis){const auto key="corner_"+std::to_string(k)+(axis?"_y":"_x");if(obs_data_has_user_value(item,key.c_str()))(axis?zone.transform.corners[k].y:zone.transform.corners[k].x)=float(obs_data_get_double(item,key.c_str()));}
            layout.zones.push_back(std::move(zone)); obs_data_release(item);
        }
        obs_data_array_release(array);
    }
    arrange(layout);
    return layout;
}
inline void write(obs_data_t *settings, const Layout &layout)
{
    auto *array = obs_data_array_create();
    for (const auto &zone : layout.zones) {
        auto *item = obs_data_create();
        obs_data_set_string(item, "id", zone.id.c_str()); obs_data_set_string(item, "name", zone.name.c_str());
        obs_data_set_bool(item, "visible", zone.visible);
        write_rect(item, "dst_", zone.destination); write_rect(item, "uv_", zone.uv);
        obs_data_set_bool(item, "polygon_enabled", zone.polygon.enabled);
        auto *points = obs_data_array_create();
        for (const auto &point : zone.polygon.vertices) {
            auto *vertex = obs_data_create();
            obs_data_set_double(vertex,"x",point.x); obs_data_set_double(vertex,"y",point.y);
            obs_data_set_double(vertex,"u",point.u); obs_data_set_double(vertex,"v",point.v);
            obs_data_array_push_back(points,vertex); obs_data_release(vertex);
        }
        obs_data_set_array(item,"polygon_vertices",points); obs_data_array_release(points);
        write_mesh(item,zone.mesh);
        obs_data_set_int(item,"mask_kind",zone.mask.kind); obs_data_set_string(item,"mask_path",zone.mask.path.c_str());
        obs_data_set_bool(item,"mask_luminance",zone.mask.luminance); obs_data_set_bool(item,"mask_invert",zone.mask.invert);
        obs_data_set_double(item,"mask_strength",zone.mask.strength); obs_data_set_double(item,"mask_feather",zone.mask.feather);
        obs_data_set_int(item,"mask_revision",zone.mask.revision);
        for(const auto &field:mask_dimensions)obs_data_set_double(item,field.key,zone.mask.*(field.member));
        for(const auto &field:transform_fields)obs_data_set_double(item,field.key,zone.transform.*(field.member));
        obs_data_set_bool(item,"corner_pin",zone.transform.pinned);
        for(size_t k=0;k<4;++k){obs_data_set_double(item,("corner_"+std::to_string(k)+"_x").c_str(),zone.transform.corners[k].x);obs_data_set_double(item,("corner_"+std::to_string(k)+"_y").c_str(),zone.transform.corners[k].y);}
        obs_data_array_push_back(array, item); obs_data_release(item);
    }
    obs_data_set_array(settings, "zones", array); obs_data_array_release(array);
    obs_data_set_bool(settings, "automatic_layout", layout.automatic);
    obs_data_set_int(settings, "mapping_schema", 7);
}
inline size_t selection(obs_data_t *settings, const Layout &layout)
{
    const std::string id = obs_data_get_string(settings, "selected_zone");
    for (size_t i = 0; i < layout.zones.size(); ++i) if (layout.zones[i].id == id) return i;
    return 0;
}
inline void load_editor(obs_data_t *settings, const Layout &layout, size_t index)
{
    if (layout.zones.empty()) { obs_data_set_string(settings, "selected_zone", ""); return; }
    const auto &zone = layout.zones[index];
    obs_data_set_string(settings, "selected_zone", zone.id.c_str());
    obs_data_set_string(settings, "zone_name", zone.name.c_str()); obs_data_set_bool(settings, "zone_visible", zone.visible);
    write_rect(settings, "zone_dst_", zone.destination); write_rect(settings, "zone_uv_", zone.uv);
    obs_data_set_bool(settings,"zone_polygon",zone.polygon.enabled);
    const auto vertex = std::clamp<long long>(obs_data_get_int(settings,"selected_vertex"),0, zone.polygon.vertices.empty() ? 0 : static_cast<long long>(zone.polygon.vertices.size()-1));
    obs_data_set_int(settings,"selected_vertex",vertex);
    if (!zone.polygon.vertices.empty()) {
        const auto &point = zone.polygon.vertices[size_t(vertex)];
        obs_data_set_double(settings,"vertex_x",point.x); obs_data_set_double(settings,"vertex_y",point.y);
        obs_data_set_double(settings,"vertex_u",point.u); obs_data_set_double(settings,"vertex_v",point.v);
    }
    const auto &mesh=zone.mesh;
    obs_data_set_bool(settings,"zone_mesh",mesh.enabled); obs_data_set_bool(settings,"mesh_smooth",mesh.smooth);
    obs_data_set_bool(settings,"mesh_soft",mesh.soft); obs_data_set_bool(settings,"mesh_wave",mesh.wave);
    obs_data_set_int(settings,"mesh_columns",mesh.columns); obs_data_set_int(settings,"mesh_rows",mesh.rows);
    obs_data_set_double(settings,"mesh_horizontal",mesh.horizontal); obs_data_set_double(settings,"mesh_vertical",mesh.vertical);
    obs_data_set_double(settings,"mesh_radius",mesh.radius); obs_data_set_double(settings,"mesh_strength",mesh.strength);
    const auto control=std::clamp<long long>(obs_data_get_int(settings,"mesh_selected"),0,mesh.points.empty()?0:static_cast<long long>(mesh.points.size()-1));
    obs_data_set_int(settings,"mesh_selected",control);
    if(!mesh.points.empty()) {
        obs_data_set_double(settings,"mesh_dx",mesh.points[size_t(control)].x); obs_data_set_double(settings,"mesh_dy",mesh.points[size_t(control)].y);
    }
    obs_data_set_int(settings,"mask_kind",zone.mask.kind); obs_data_set_string(settings,"mask_path",zone.mask.path.c_str());
    obs_data_set_bool(settings,"mask_luminance",zone.mask.luminance); obs_data_set_bool(settings,"mask_invert",zone.mask.invert);
    obs_data_set_double(settings,"mask_strength",zone.mask.strength); obs_data_set_double(settings,"mask_feather",zone.mask.feather);
    for(const auto &field:mask_dimensions)obs_data_set_double(settings,field.key,zone.mask.*(field.member));
}
inline std::string new_id(const Layout &layout)
{
    size_t number = 1;
    for (;;) {
        auto id = std::to_string(number++);
        if (std::none_of(layout.zones.begin(), layout.zones.end(), [&](const Zone &z) { return z.id == id; })) return id;
    }
}
inline void action(Layout &layout, size_t &index, const std::string &operation)
{
    if (operation == "add" || operation == "duplicate") {
        Zone zone;
        if (operation == "duplicate" && !layout.zones.empty()) zone = layout.zones[index];
        zone.id = new_id(layout);
        zone.name = operation == "duplicate" ? zone.name + mapper_i18n::text(" (copia)") : mapper_i18n::text("Zona ") + zone.id;
        layout.zones.push_back(std::move(zone)); index = layout.zones.size() - 1;
    } else if (!layout.zones.empty() && operation == "delete") {
        layout.zones.erase(layout.zones.begin() + index);
        index = layout.zones.empty() ? 0 : std::min(index, layout.zones.size() - 1);
    } else if (!layout.zones.empty() && operation == "up" && index > 0) {
        std::swap(layout.zones[index], layout.zones[index - 1]); --index;
    } else if (!layout.zones.empty() && operation == "down" && index + 1 < layout.zones.size()) {
        std::swap(layout.zones[index], layout.zones[index + 1]); ++index;
    } else if (operation == "arrange") layout.automatic = true;
    arrange(layout);
}
}

