// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs-module.h>
#include <atomic>
#include <new>
#include <mutex>
#include <string>
#include <memory>
#include <graphics/vec4.h>
#include "zones.hpp"
#include "i18n.hpp"
#include "source-list.hpp"
#include <unordered_map>
#include <util/bmem.h>
#include <cstring>

OBS_DECLARE_MODULE()
MODULE_EXPORT void obs_module_set_locale(const char *locale) { mapper_i18n::set_locale(locale); }
MODULE_EXPORT const char *obs_module_name(void) { return "OBS Screen Mapper"; }
MODULE_EXPORT const char *obs_module_description(void) { return mapper_i18n::text("Independent mapping source - shared OBS input"); }
MODULE_EXPORT const char *obs_module_author(void) { return "DjEmiMas"; }

namespace {
constexpr int min_size = 16;
constexpr int max_size = 16384;
bool same_polygon(const mapping::Polygon &a,const mapping::Polygon &b)
{
    if(a.enabled!=b.enabled || a.error!=b.error || a.triangles!=b.triangles || a.vertices.size()!=b.vertices.size()) return false;
    for(size_t i=0;i<a.vertices.size();++i) {
        const auto &x=a.vertices[i],&y=b.vertices[i];
        if(x.x!=y.x || x.y!=y.y || x.u!=y.u || x.v!=y.v) return false;
    }
    return true;
}
bool same_mesh(const mapping::Mesh &a,const mapping::Mesh &b)
{
    if(a.domain!=b.domain||a.center_x!=b.center_x||a.center_y!=b.center_y||a.axis_x!=b.axis_x||a.axis_y!=b.axis_y||a.enabled!=b.enabled || a.smooth!=b.smooth || a.soft!=b.soft || a.wave!=b.wave || a.columns!=b.columns || a.rows!=b.rows ||
       a.horizontal!=b.horizontal || a.vertical!=b.vertical || a.radius!=b.radius || a.strength!=b.strength || a.error!=b.error || a.points.size()!=b.points.size()) return false;
    for(size_t i=0;i<a.points.size();++i) if(a.points[i].x!=b.points[i].x || a.points[i].y!=b.points[i].y) return false;
    return true;
}
struct GeometryBuffer {
    gs_vertbuffer_t *buffer=nullptr;
    mapping::Rect destination,uv;
    float width=0,height=0;
    std::shared_ptr<const mapping::Surface> surface;
    mapping::Polygon polygon;
    bool mesh=false;
    uint64_t generation=0;
    ~GeometryBuffer() { if(buffer) { obs_enter_graphics();gs_vertexbuffer_destroy(buffer);obs_leave_graphics(); } }
};
struct Mapper {
    obs_source_t *self = nullptr;
    obs_source_t *input = nullptr;
    std::mutex mutex;
    std::string wanted;
    float retry_seconds = 1.0f;
    std::atomic<uint32_t> width{1920};
    std::atomic<uint32_t> height{1080};
    std::shared_ptr<const mapping::Layout> layout;
    gs_texrender_t *capture = nullptr;
    gs_texrender_t *output = nullptr;
    gs_effect_t *mask_effect=nullptr;
    bool mask_shader_failed=false;
    std::atomic<bool> editor_unclipped{false};
    std::unordered_map<std::string,std::weak_ptr<mapping::MaskResource>> masks;
    std::unordered_map<std::string,std::unique_ptr<GeometryBuffer>> geometry;
    std::shared_ptr<const mapping::Layout> rendered_layout;
    uint64_t geometry_generation=0;
};
obs_source_t *input_ref(Mapper *mapper)
{
    std::lock_guard<std::mutex> lock(mapper->mutex);
    return obs_source_get_ref(mapper->input);
}
void disconnect(Mapper *mapper)
{
    obs_source_t *old;
    { std::lock_guard<std::mutex> lock(mapper->mutex); old = mapper->input; mapper->input = nullptr; }
    if (old) {
        obs_source_remove_active_child(mapper->self, old);
        obs_source_release(old);
    }
}
void connect(Mapper *mapper)
{
    auto *candidate = obs_get_source_by_uuid(mapper->wanted.c_str());
    auto *old = input_ref(mapper);
    if (candidate == old) { obs_source_release(candidate); obs_source_release(old); return; }
    obs_source_release(old);
    disconnect(mapper);
    if (!candidate) return;
    if (candidate == mapper->self || obs_source_removed(candidate) || !(obs_source_get_output_flags(candidate) & OBS_SOURCE_VIDEO) ||
        !obs_source_add_active_child(mapper->self, candidate)) {
        obs_source_release(candidate);
        return;
    }
    std::lock_guard<std::mutex> lock(mapper->mutex);
    mapper->input = candidate;
}
void enumerate(void *data, obs_source_enum_proc_t callback, void *param)
{
    auto *mapper = static_cast<Mapper *>(data);
    auto *input = input_ref(mapper);
    if (input) { callback(mapper->self, input, param); obs_source_release(input); }
}
void tick(void *data, float seconds)
{
    auto *mapper = static_cast<Mapper *>(data);
    auto *input = input_ref(mapper);
    const bool removed = input && obs_source_removed(input);
    obs_source_release(input);
    if (removed) disconnect(mapper);
    // Retry unresolved UUIDs after scene collection load; never bind by name.
    if (!input && !mapper->wanted.empty()) {
        mapper->retry_seconds -= seconds;
        if (mapper->retry_seconds <= 0.0f) { connect(mapper); mapper->retry_seconds = 1.0f; }
    }
}
uint32_t dimension(obs_data_t *settings, const char *key)
{
    auto value = obs_data_get_int(settings, key);
    return static_cast<uint32_t>(value < min_size ? min_size : value > max_size ? max_size : value);
}
const char *name(void *) { return "OBS Screen Mapper"; }
void defaults(obs_data_t *settings)
{
    obs_data_set_default_int(settings, "canvas_width", 1920);
    obs_data_set_default_int(settings, "canvas_height", 1080);
    obs_data_set_default_string(settings, "input_uuid", "");
    obs_data_set_default_string(settings, "distribution", "split");
    obs_data_set_default_string(settings, "selected_zone", "1");
    obs_data_set_default_string(settings, "zone_name", mapper_i18n::text("Zona 1"));
    obs_data_set_default_bool(settings, "zone_visible", true);
    obs_data_set_default_double(settings, "zone_dst_w", 1);
    obs_data_set_default_double(settings, "zone_dst_h", 1);
    obs_data_set_default_double(settings, "zone_uv_w", 1);
    obs_data_set_default_double(settings, "zone_uv_h", 1);
}
void update(void *data, obs_data_t *settings)
{
    auto *mapper = static_cast<Mapper *>(data);
    mapper->width.store(dimension(settings, "canvas_width"), std::memory_order_relaxed);
    mapper->editor_unclipped.store(obs_obj_is_private(mapper->self)&&obs_data_get_bool(settings,"_mapper_preview_unclipped"),std::memory_order_relaxed);
    mapper->height.store(dimension(settings, "canvas_height"), std::memory_order_relaxed);
    auto layout = mapping::read(settings);
    const auto previous=std::atomic_load(&mapper->layout);
    std::unordered_map<std::string,const mapping::Zone *> old_zones;
    if(previous) for(const auto &zone:previous->zones) old_zones.emplace(zone.id,&zone);
    for(auto &zone:layout.zones) {
        const auto old=old_zones.find(zone.id);
        if(zone.mesh.enabled) {
            if(old!=old_zones.end() && same_mesh(zone.mesh,old->second->mesh) && same_polygon(zone.polygon,old->second->polygon)) zone.surface=old->second->surface;
            else zone.surface=std::make_shared<const mapping::Surface>(mapping::compile_surface(zone.mesh,zone.polygon));
        }
        if(zone.mask.kind==3 && zone.mask.strength>0 && mapping::valid_mask(zone.mask)) {
            const std::string key=zone.mask.path+"|"+std::to_string(zone.mask.revision);
            zone.mask.resource=mapper->masks[key].lock();
            if(!zone.mask.resource) {
                zone.mask.resource=std::make_shared<mapping::MaskResource>(zone.mask.path);
                mapper->masks[key]=zone.mask.resource;
                if(!zone.mask.resource->image.error.empty()) blog(LOG_WARNING,"[OBS Screen Mapper] %s",zone.mask.resource->image.error.c_str());
            }
        }
    }
    for(auto entry=mapper->masks.begin();entry!=mapper->masks.end();) {
        if(entry->second.expired()) entry=mapper->masks.erase(entry); else ++entry;
    }
    mapping::write(settings, layout);
    std::atomic_store(&mapper->layout, std::make_shared<const mapping::Layout>(std::move(layout)));
    mapper->wanted = obs_data_get_string(settings, "input_uuid");
    mapper->retry_seconds = 1.0f;
    connect(mapper);
}
void *create(obs_data_t *settings, obs_source_t *source)
{
    auto *mapper = new (std::nothrow) Mapper;
    if (mapper) { mapper->self = source; update(mapper, settings); }
    return mapper;
}
void destroy(void *data)
{
    auto *mapper = static_cast<Mapper *>(data);
    disconnect(mapper);
    if (mapper->capture || mapper->output || mapper->mask_effect) {
        obs_enter_graphics(); gs_texrender_destroy(mapper->capture); gs_texrender_destroy(mapper->output);gs_effect_destroy(mapper->mask_effect); obs_leave_graphics();
    }
    delete mapper;
}
uint32_t width(void *data) { return static_cast<Mapper *>(data)->width.load(std::memory_order_relaxed); }
uint32_t height(void *data) { return static_cast<Mapper *>(data)->height.load(std::memory_order_relaxed); }
void draw_zone(Mapper *mapper,const std::shared_ptr<const mapping::Layout> &layout,size_t index,mapping::Rect uv,float cw,float ch)
{
    const auto &zone=layout->zones[index];
    const auto d=zone.destination;
    auto &cached=mapper->geometry[zone.id];
    auto same_rect=[](mapping::Rect a,mapping::Rect b) { return a.x==b.x && a.y==b.y && a.w==b.w && a.h==b.h; };
    bool valid=cached && cached->buffer && same_rect(cached->destination,d) && same_rect(cached->uv,uv) && cached->width==cw && cached->height==ch;
    if(valid && cached->generation!=mapper->geometry_generation) {
        valid=zone.mesh.enabled==cached->mesh && (zone.mesh.enabled?zone.surface==cached->surface:same_polygon(zone.polygon,cached->polygon));
    }
    const auto mode=zone.mesh.enabled || zone.polygon.enabled?GS_TRIS:GS_TRISTRIP;
    if(valid) {
        cached->generation=mapper->geometry_generation;
        if(cached->buffer) { gs_load_vertexbuffer(cached->buffer);gs_load_indexbuffer(nullptr);gs_draw(mode,0,0); }
        return;
    }
    cached=std::make_unique<GeometryBuffer>();
    cached->destination=d;cached->uv=uv;cached->width=cw;cached->height=ch;
    cached->mesh=zone.mesh.enabled;cached->surface=zone.surface;
    if(!zone.mesh.enabled) cached->polygon=zone.polygon;
    cached->generation=mapper->geometry_generation;
    auto emit=[&](const mapping::Vertex &point,float mx,float my) {
        gs_texcoord(uv.x+point.u*uv.w,uv.y+point.v*uv.h,0); gs_texcoord(mx,my,1);
        gs_vertex2f((d.x+point.x*d.w)*cw,(d.y+point.y*d.h)*ch);
    };
    if(zone.mesh.enabled) {
        if(!zone.surface || zone.surface->triangles.empty()) return;
        gs_render_start(true);
        for(const auto &point:zone.surface->triangles) emit(point,point.mask_x,point.mask_y);
    } else if(zone.polygon.enabled) {
        if(zone.polygon.triangles.empty()) return;
        gs_render_start(true);
        for(size_t index:zone.polygon.triangles) { const auto &point=zone.polygon.vertices[index]; emit(point,point.x,point.y); }
    } else {
        gs_render_start(true);
        emit({0,0,0,0},0,0); emit({1,0,1,0},1,0); emit({0,1,0,1},0,1); emit({1,1,1,1},1,1);
    }
    cached->buffer=gs_render_save();
    if(cached->buffer) { gs_load_vertexbuffer(cached->buffer);gs_load_indexbuffer(nullptr);gs_draw(mode,0,0); }
}
void render(void *data, gs_effect_t *)
{
    static thread_local unsigned render_depth = 0;
    if (render_depth >= 32) return; // Last-resort guard, including foreign plugins.
    auto *mapper = static_cast<Mapper *>(data);
    auto layout = std::atomic_load(&mapper->layout);
    if(layout && mapper->rendered_layout!=layout) {
        ++mapper->geometry_generation;
        std::unordered_map<std::string,bool> retained;
        for(const auto &zone:layout->zones) retained.emplace(zone.id,true);
        for(auto it=mapper->geometry.begin();it!=mapper->geometry.end();) {
            if(!retained.count(it->first)) it=mapper->geometry.erase(it);else ++it;
        }
        mapper->rendered_layout=layout;
    }
    if (!layout || std::none_of(layout->zones.begin(), layout->zones.end(), [](const mapping::Zone &z) { return z.visible; })) return;
    auto *input = input_ref(mapper);
    if (!input) return;
    const auto w = obs_source_get_width(input), h = obs_source_get_height(input);
    if (w && h && !obs_source_removed(input)) {
        ++render_depth;
        if (!mapper->capture) mapper->capture = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
        if (mapper->capture) {
            gs_texrender_reset(mapper->capture);
            gs_blend_state_push();
            gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
            if (gs_texrender_begin(mapper->capture, w, h)) {
                vec4 clear; vec4_zero(&clear); gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
                gs_ortho(0, float(w), 0, float(h), -100, 100);
                obs_source_video_render(input);
                gs_texrender_end(mapper->capture);
                auto *texture = gs_texrender_get_texture(mapper->capture);
                const float cw = float(width(mapper)), ch = float(height(mapper));
                const bool exterior=mapper->editor_unclipped.load(std::memory_order_relaxed);
                if(!exterior){
                if(!mapper->output) mapper->output=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
                if(!mapper->output) { gs_blend_state_pop(); --render_depth; obs_source_release(input); return; }
                gs_texrender_reset(mapper->output);
                if(!gs_texrender_begin(mapper->output,uint32_t(cw),uint32_t(ch))) { gs_blend_state_pop(); --render_depth; obs_source_release(input); return; }
                vec4 empty;vec4_zero(&empty);gs_clear(GS_CLEAR_COLOR,&empty,0,0);gs_ortho(0,cw,0,ch,-100,100);
                }
                for(size_t i=0;i<layout->zones.size();++i) {
                    const auto &zone=layout->zones[i]; const auto d=zone.destination,uv=mapping::source_uv(*layout,i);
                    if(!zone.visible || d.w<=0 || d.h<=0 || uv.w<=0 || uv.h<=0 || !mapping::valid_mask(zone.mask)||!mapping::valid_transform(zone.transform)) continue;
                    auto *effect=obs_get_base_effect(OBS_EFFECT_DEFAULT);
                    if((zone.mask.kind!=0 && zone.mask.strength>0)||mapping::transformed(zone.transform)) {
                        if(!mapper->mask_effect && !mapper->mask_shader_failed) {
                            char *error=nullptr;
                            mapper->mask_effect=gs_effect_create(mapping::mask_shader,"obs-screen-mapper-mask.effect",&error);
                            if(!mapper->mask_effect) { mapper->mask_shader_failed=true; blog(LOG_WARNING,"[OBS Screen Mapper] Mask shader: %s",error?error:"failed"); }
                            bfree(error);
                        }
                        if(!mapper->mask_effect) continue;
                        effect=mapper->mask_effect;
                        auto *mask_texture=texture;
                        if(zone.mask.kind==3&&zone.mask.strength>0) {
                            if(!zone.mask.resource || !(mask_texture=zone.mask.resource->get_texture())) continue;
                        }
                        gs_effect_set_texture(gs_effect_get_param_by_name(effect,"mask_image"),mask_texture);
                        gs_effect_set_float(gs_effect_get_param_by_name(effect,"mask_kind"),float(zone.mask.kind));
                        gs_effect_set_float(gs_effect_get_param_by_name(effect,"mask_luminance"),zone.mask.luminance?1:0);
                        gs_effect_set_float(gs_effect_get_param_by_name(effect,"mask_invert"),zone.mask.invert?1:0);
                        gs_effect_set_float(gs_effect_get_param_by_name(effect,"mask_strength"),zone.mask.kind?zone.mask.strength:0);
                        gs_effect_set_float(gs_effect_get_param_by_name(effect,"mask_feather"),zone.mask.feather);
                        for(const auto &field:mapping::mask_dimensions)gs_effect_set_float(gs_effect_get_param_by_name(effect,field.key),zone.mask.*(field.member));
                        gs_effect_set_float(gs_effect_get_param_by_name(effect,"transform_enabled"),mapping::transformed(zone.transform)?1:0);
                        for(const auto &field:mapping::transform_fields)gs_effect_set_float(gs_effect_get_param_by_name(effect,field.key),zone.transform.*(field.member));
                        auto uniform=[&](const char *key,float value){gs_effect_set_float(gs_effect_get_param_by_name(effect,key),value);};
                        uniform("zone_x",d.x*cw);uniform("zone_y",d.y*ch);uniform("zone_w",d.w*cw);uniform("zone_h",d.h*ch);uniform("canvas_w",cw);uniform("canvas_h",ch);
                        const auto h=mapping::homography(zone.transform);uniform("corner_a",h.a);uniform("corner_b",h.b);uniform("corner_c",h.c);uniform("corner_d",h.d);uniform("corner_e",h.e);uniform("corner_f",h.f);uniform("corner_g",h.g);uniform("corner_h",h.h);
                    }
                    gs_effect_set_texture(gs_effect_get_param_by_name(effect,"image"),texture);
                    while(gs_effect_loop(effect,"Draw")) draw_zone(mapper,layout,i,uv,cw,ch);
                }
                if(!exterior){gs_texrender_end(mapper->output);
                auto *result=gs_texrender_get_texture(mapper->output);auto *effect=obs_get_base_effect(OBS_EFFECT_DEFAULT);
                gs_effect_set_texture(gs_effect_get_param_by_name(effect,"image"),result);
                while(gs_effect_loop(effect,"Draw")) gs_draw_sprite(result,0,uint32_t(cw),uint32_t(ch));
                }
            }
            gs_blend_state_pop();
        }
        --render_depth;
    }
    obs_source_release(input);
}
void refresh_zone_controls(obs_properties_t *props, obs_data_t *settings, const mapping::Layout &layout)
{
    auto *list = obs_properties_get(props, "selected_zone");
    obs_property_list_clear(list);
    for (const auto &zone : layout.zones) obs_property_list_add_string(list, zone.name.c_str(), zone.id.c_str());
    const bool present = !layout.zones.empty();
    const char *fields[] = {"selected_zone", "zone_name", "zone_visible", "zone_dst_x", "zone_dst_y", "zone_dst_w", "zone_dst_h"};
    for (const char *field : fields) obs_property_set_enabled(obs_properties_get(props, field), present);
    for (const char *suffix : {"x", "y", "w", "h"})
        obs_property_set_enabled(obs_properties_get(props, (std::string("zone_uv_") + suffix).c_str()), present && layout.mode == mapping::Mode::Custom);
    mapping::load_editor(settings, layout, mapping::selection(settings, layout));
    auto *vertices = obs_properties_get(props,"selected_vertex");
    obs_property_list_clear(vertices);
    const auto *polygon = present ? &layout.zones[mapping::selection(settings,layout)].polygon : nullptr;
    if (polygon) for (size_t i = 0; i < polygon->vertices.size(); ++i)
        obs_property_list_add_int(vertices, (mapper_i18n::text("Vértice ") + std::to_string(i+1)).c_str(), static_cast<long long>(i));
    obs_property_set_enabled(obs_properties_get(props,"zone_polygon"),present);
    for (const char *field : {"selected_vertex", "vertex_action", "vertex_x", "vertex_y", "vertex_u", "vertex_v"})
        obs_property_set_enabled(obs_properties_get(props,field),polygon && polygon->enabled);
    const std::string status = !polygon || !polygon->enabled ? mapper_i18n::text("Polygon desactivado: destino rectangular.") :
        polygon->error.empty() ? mapper_i18n::text("Polígono válido: ") + std::to_string(polygon->vertices.size()) + mapper_i18n::text(" vértices.") : polygon->error + mapper_i18n::text(" Salida de esta zona transparente.");
    obs_property_set_description(obs_properties_get(props,"polygon_status"), status.c_str());
    const auto *mesh=present?&layout.zones[mapping::selection(settings,layout)].mesh:nullptr;
    obs_property_set_enabled(obs_properties_get(props,"zone_mesh"),present);
    auto *controls=obs_properties_get(props,"mesh_selected"); obs_property_list_clear(controls);
    if(mesh) for(size_t i=0;i<mesh->points.size();++i)
        obs_property_list_add_int(controls,(mapper_i18n::text("Fila ")+std::to_string(i/size_t(mesh->columns)+1)+mapper_i18n::text(", columna ")+std::to_string(i%size_t(mesh->columns)+1)).c_str(),static_cast<long long>(i));
    for(const char *field:{"mesh_columns","mesh_rows","mesh_smooth","mesh_selected","mesh_dx","mesh_dy","mesh_soft","mesh_radius","mesh_strength",
        "mesh_wave","mesh_horizontal","mesh_vertical","mesh_action"}) obs_property_set_enabled(obs_properties_get(props,field),mesh && mesh->enabled);
    std::string mesh_status=mapper_i18n::text("Malla desactivada.");
    if(mesh && mesh->enabled) {
        auto copy=*mesh; const auto surface=mapping::compile_surface(copy,layout.zones[mapping::selection(settings,layout)].polygon);
        mesh_status=surface.error.empty()?mapper_i18n::text("Malla válida; Polygon y malla pueden estar activos a la vez."):surface.error+mapper_i18n::text(" Salida de esta zona transparente.");
    }
    obs_property_set_description(obs_properties_get(props,"mesh_status"),mesh_status.c_str());
    const auto *mask=present?&layout.zones[mapping::selection(settings,layout)].mask:nullptr;
    obs_property_set_enabled(obs_properties_get(props,"mask_kind"),present);
    for(const char *field:{"mask_invert","mask_strength"}) obs_property_set_enabled(obs_properties_get(props,field),mask && mask->kind!=0);
    for(const char *field:{"mask_path","mask_luminance"}) obs_property_set_enabled(obs_properties_get(props,field),mask && mask->kind==3);
    obs_property_set_enabled(obs_properties_get(props,"mask_action"),mask&&mask->kind!=0);
    obs_property_set_enabled(obs_properties_get(props,"mask_feather"),mask && (mask->kind==1 || mask->kind==2));
    for(const auto &field:mapping::mask_dimensions)obs_property_set_enabled(obs_properties_get(props,field.key),mask&&(mask->kind==1||mask->kind==2)&&(std::string(field.key)!="mask_radius"||mask->kind==1));
    obs_property_set_description(obs_properties_get(props,"mask_status"),
        mask && !mapping::valid_mask(*mask)?mapper_i18n::text("Parámetros de máscara no válidos: salida transparente."):
        mapper_i18n::text("PNG hasta 4096 px; SVG nativo de Direct2D rasterizado a 1024 px. Si falla la carga, la zona queda transparente; consulta el registro OBS. Recarga para releer un archivo modificado."));
}
bool zone_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    auto layout = mapping::read(settings);
    auto index = mapping::selection(settings, layout);
    const std::string field = obs_property_name(property);
    if (field == "zone_action") {
        const std::string operation = obs_data_get_string(settings, "zone_action");
        mapping::action(layout, index, operation);
        obs_data_set_string(settings, "zone_action", "");
        if (!layout.zones.empty()) obs_data_set_string(settings, "selected_zone", layout.zones[index].id.c_str());
    } else if (!layout.zones.empty()) {
        auto &zone = layout.zones[index];
        if (field == "zone_name") {
            zone.name = obs_data_get_string(settings, "zone_name");
            if (zone.name.empty()) zone.name = mapper_i18n::text("Zona ") + zone.id;
        } else if (field == "zone_visible") zone.visible = obs_data_get_bool(settings, "zone_visible");
        else if (field.compare(0, 9, "zone_dst_") == 0) {
            const auto rect = mapping::read_rect(settings, "zone_dst_",false);
            const auto previous = zone.destination;
            if (rect.x != previous.x || rect.y != previous.y || rect.w != previous.w || rect.h != previous.h) layout.automatic = false;
            zone.destination = rect;
        } else if (field.compare(0, 8, "zone_uv_") == 0) zone.uv = mapping::read_rect(settings, "zone_uv_");
        else if (field == "zone_polygon") zone.polygon.enabled = obs_data_get_bool(settings,"zone_polygon");
        else if (field == "vertex_action") {
            auto &points = zone.polygon.vertices;
            auto vertex = points.empty() ? size_t(0) : size_t(std::clamp<long long>(obs_data_get_int(settings,"selected_vertex"),0,static_cast<long long>(points.size()-1)));
            const std::string operation = obs_data_get_string(settings,"vertex_action");
            if (operation == "reset") { zone.polygon.vertices = mapping::Polygon{}.vertices; vertex = 0; }
            else if (operation == "insert" && !points.empty()) {
                const auto a = points[vertex], b = points[(vertex+1)%points.size()];
                points.insert(points.begin()+vertex+1, {(a.x+b.x)/2,(a.y+b.y)/2,(a.u+b.u)/2,(a.v+b.v)/2}); ++vertex;
            } else if (operation == "remove" && points.size() > 3) {
                points.erase(points.begin()+vertex); vertex = std::min(vertex,points.size()-1);
            }
            obs_data_set_int(settings,"selected_vertex",static_cast<long long>(vertex));
            obs_data_set_string(settings,"vertex_action","");
        } else if (field.compare(0,7,"vertex_") == 0 && !zone.polygon.vertices.empty()) {
            const size_t vertex = size_t(std::clamp<long long>(obs_data_get_int(settings,"selected_vertex"),0,static_cast<long long>(zone.polygon.vertices.size()-1)));
            auto &point = zone.polygon.vertices[vertex];
            point = {mapping::bounded(obs_data_get_double(settings,"vertex_x")), mapping::bounded(obs_data_get_double(settings,"vertex_y")),
                mapping::bounded(obs_data_get_double(settings,"vertex_u")), mapping::bounded(obs_data_get_double(settings,"vertex_v"))};
        }
        mapping::compile_polygon(zone.polygon);
        auto &mesh=zone.mesh;
        if(field=="zone_mesh") mesh.enabled=obs_data_get_bool(settings,"zone_mesh");
        else if(field=="mesh_columns" || field=="mesh_rows") mapping::resize_mesh(mesh,
            int(std::clamp<long long>(obs_data_get_int(settings,"mesh_columns"),2,16)),int(std::clamp<long long>(obs_data_get_int(settings,"mesh_rows"),2,16)));
        else if(field=="mesh_smooth") mesh.smooth=obs_data_get_bool(settings,"mesh_smooth");
        else if(field=="mesh_soft") mesh.soft=obs_data_get_bool(settings,"mesh_soft");
        else if(field=="mesh_wave") mesh.wave=obs_data_get_bool(settings,"mesh_wave");
        else if(field=="mesh_radius") mesh.radius=float(obs_data_get_double(settings,"mesh_radius"));
        else if(field=="mesh_strength") mesh.strength=float(obs_data_get_double(settings,"mesh_strength"));
        else if(field=="mesh_horizontal") mesh.horizontal=float(obs_data_get_double(settings,"mesh_horizontal"));
        else if(field=="mesh_vertical") mesh.vertical=float(obs_data_get_double(settings,"mesh_vertical"));
        else if((field=="mesh_dx" || field=="mesh_dy") && !mesh.points.empty()) {
            const size_t control=size_t(std::clamp<long long>(obs_data_get_int(settings,"mesh_selected"),0,static_cast<long long>(mesh.points.size()-1)));
            mapping::move_control(mesh,control,float(obs_data_get_double(settings,"mesh_dx"))-mesh.points[control].x,
                float(obs_data_get_double(settings,"mesh_dy"))-mesh.points[control].y);
        } else if(field=="mesh_action") {
            const std::string operation=obs_data_get_string(settings,"mesh_action");
            if(operation=="reset_points") mesh.points.assign(size_t(mesh.columns*mesh.rows),{});
            if(operation=="reset_curves") mesh.horizontal=mesh.vertical=0;
            if(operation=="reset_all") { const bool enabled=mesh.enabled; mesh=mapping::Mesh{}; mesh.enabled=enabled; }
            obs_data_set_string(settings,"mesh_action","");
        }
        mapping::validate_mesh(mesh);
        if(field=="mask_kind") zone.mask.kind=int(obs_data_get_int(settings,"mask_kind"));
        else if(field=="mask_path") zone.mask.path=obs_data_get_string(settings,"mask_path");
        else if(field=="mask_luminance") zone.mask.luminance=obs_data_get_bool(settings,"mask_luminance");
        else if(field=="mask_invert") zone.mask.invert=obs_data_get_bool(settings,"mask_invert");
        else if(field=="mask_strength") zone.mask.strength=float(obs_data_get_double(settings,"mask_strength"));
        else if(field=="mask_feather") zone.mask.feather=float(obs_data_get_double(settings,"mask_feather"));
        else if(field=="mask_center_x") zone.mask.center_x=float(obs_data_get_double(settings,"mask_center_x"));
        else if(field=="mask_center_y") zone.mask.center_y=float(obs_data_get_double(settings,"mask_center_y"));
        else if(field=="mask_width") zone.mask.width=float(obs_data_get_double(settings,"mask_width"));
        else if(field=="mask_height") zone.mask.height=float(obs_data_get_double(settings,"mask_height"));
        else if(field=="mask_radius") zone.mask.radius=float(obs_data_get_double(settings,"mask_radius"));
        else if(field=="mask_action") {
            const std::string operation=obs_data_get_string(settings,"mask_action");
            if(operation=="reload") zone.mask.revision=zone.mask.revision<9223372036854775807LL?zone.mask.revision+1:0;
            if(operation=="reset_shape")mapping::reset_mask_shape(zone.mask);
            obs_data_set_string(settings,"mask_action","");
        }
    }
    mapping::write(settings, layout);
    refresh_zone_controls(props, settings, layout);
    return true;
}
bool open_editor(obs_properties_t *,obs_property_t *,void *data)
{
    auto *mapper=static_cast<Mapper*>(data); if(!mapper) return false;
    HMODULE own=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&open_editor),&own);
    wchar_t path[32768]{}; const DWORD length=GetModuleFileNameW(own,path,32768);
    std::wstring file(path,length);auto slash=file.find_last_of(L"\\/");
    if(slash==std::wstring::npos) return false;
    file.resize(slash+1);file+=L"obs-screen-mapper-ui.dll";
    static HMODULE ui=nullptr;
    if(!ui) ui=LoadLibraryExW(file.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    using Open=void(*)(obs_source_t*,const char*);
    Open open=nullptr;
    if(ui) { auto symbol=GetProcAddress(ui,"mapper_open_editor_v2");static_assert(sizeof(symbol)==sizeof(open));std::memcpy(&open,&symbol,sizeof(open)); }
    if(!open) { MessageBoxW(nullptr,mapping::wide_path(mapper_i18n::text("No se pudo cargar el editor. Coloca obs-screen-mapper-ui.dll junto a la DLL principal y usa OBS 32.x con Qt 6.")).c_str(),L"OBS Screen Mapper",MB_OK|MB_ICONERROR);return false; }
    char *directory=obs_module_config_path("presets");open(mapper->self,directory);bfree(directory);return false;
}
bool advanced_modified(obs_properties_t *properties,obs_property_t *,obs_data_t *settings)
{
    const bool show=obs_data_get_bool(settings,"show_advanced");
    for(auto *property=obs_properties_first(properties);property;obs_property_next(&property)) {
        const std::string key=obs_property_name(property);
        const bool compact=key=="open_editor"||key=="input_uuid"||key=="canvas_width"||key=="canvas_height"||key=="show_advanced"||key=="phase_info"||key=="author_credit";
        obs_property_set_visible(property,compact||show);
    }
    return true;
}
obs_properties_t *properties(void *data)
{
    auto *props = obs_properties_create();
    obs_properties_add_button2(props,"open_editor",mapper_i18n::text("Abrir Mapper Editor…"),open_editor,data);
    auto *list = obs_properties_add_list(props, "input_uuid", mapper_i18n::text("Fuente OBS existente"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(list, mapper_i18n::text("Ninguna (transparente)"), "");
    int category=-1;
    for(const auto &entry:mapper_sources::collect(data?static_cast<Mapper*>(data)->self:nullptr)) {
        if(category!=entry.category) {
            category=entry.category;
            auto index=obs_property_list_add_string(list,mapper_i18n::text(mapper_sources::label(category)),("__mapper_section_"+std::to_string(category)).c_str());
            obs_property_list_item_disable(list,index,true);
        }
        obs_property_list_add_string(list,entry.name.c_str(),entry.uuid.c_str());
    }
    obs_properties_add_int(props, "canvas_width", mapper_i18n::text("Ancho del canvas"), min_size, max_size, 1);
    obs_properties_add_int(props, "canvas_height", mapper_i18n::text("Alto del canvas"), min_size, max_size, 1);
    obs_property_set_modified_callback(obs_properties_add_bool(props,"show_advanced",mapper_i18n::text("Mostrar controles numéricos avanzados")),advanced_modified);
    auto *mode = obs_properties_add_list(props, "distribution", mapper_i18n::text("Distribución"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(mode, mapper_i18n::text("Split"), "split");
    obs_property_list_add_string(mode, mapper_i18n::text("Repeat"), "repeat");
    obs_property_list_add_string(mode, mapper_i18n::text("Custom UV"), "custom");
    obs_property_set_modified_callback(mode, zone_modified);
    auto *selected = obs_properties_add_list(props, "selected_zone", mapper_i18n::text("Zona seleccionada"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_set_modified_callback(selected, zone_modified);
    auto *operation = obs_properties_add_list(props, "zone_action", mapper_i18n::text("Acción de zona"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    for (const auto &item : {std::pair<const char *, const char *>{mapper_i18n::text("Sin acción"), ""}, {mapper_i18n::text("Añadir"), "add"},
            {mapper_i18n::text("Duplicar seleccionada"), "duplicate"}, {mapper_i18n::text("Eliminar seleccionada"), "delete"},
            {mapper_i18n::text("Subir en el orden"), "up"}, {mapper_i18n::text("Bajar en el orden"), "down"}, {mapper_i18n::text("Distribuir destinos en horizontal"), "arrange"}})
        obs_property_list_add_string(operation, item.first, item.second);
    obs_property_set_modified_callback(operation, zone_modified);
    obs_property_set_modified_callback(obs_properties_add_text(props, "zone_name", mapper_i18n::text("Nombre de zona"), OBS_TEXT_DEFAULT), zone_modified);
    obs_property_set_modified_callback(obs_properties_add_bool(props, "zone_visible", mapper_i18n::text("Zona visible")), zone_modified);
    obs_property_set_modified_callback(obs_properties_add_bool(props,"zone_polygon",mapper_i18n::text("Polygon")),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_list(props,"selected_vertex",mapper_i18n::text("Vértice seleccionado"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT),zone_modified);
    auto *vertex_action = obs_properties_add_list(props,"vertex_action",mapper_i18n::text("Acción de vértice"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(vertex_action,mapper_i18n::text("Sin acción"),"");
    obs_property_list_add_string(vertex_action,mapper_i18n::text("Insertar punto en la arista siguiente"),"insert");
    obs_property_list_add_string(vertex_action,mapper_i18n::text("Eliminar punto (mínimo tres)"),"remove");
    obs_property_list_add_string(vertex_action,mapper_i18n::text("Restablecer contorno rectangular"),"reset");
    obs_property_set_modified_callback(vertex_action,zone_modified);
    for (const auto &field : {std::pair<const char *,const char *>{"vertex_x",mapper_i18n::text("Punto X local (0–1)")}, {"vertex_y",mapper_i18n::text("Punto Y local (0–1)")},
        {"vertex_u",mapper_i18n::text("Punto U local (0–1)")},{"vertex_v",mapper_i18n::text("Punto V local (0–1)")}})
        obs_property_set_modified_callback(obs_properties_add_float(props,field.first,field.second,0,1,0.01),zone_modified);
    obs_properties_add_text(props,"polygon_status",mapper_i18n::text("Polygon"),OBS_TEXT_INFO);
    for(const auto &field:{std::pair<const char *,const char *>{"zone_mesh",mapper_i18n::text("Smooth Mesh")},{"mesh_smooth",mapper_i18n::text("Interpolación suave")},
        {"mesh_soft",mapper_i18n::text("Soft Selection")},{"mesh_wave",mapper_i18n::text("Curvatura con perfil de onda (desactivado: arco)")}})
        obs_property_set_modified_callback(obs_properties_add_bool(props,field.first,field.second),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_int(props,"mesh_columns",mapper_i18n::text("Columnas de puntos"),2,16,1),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_int(props,"mesh_rows",mapper_i18n::text("Filas de puntos"),2,16,1),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_list(props,"mesh_selected",mapper_i18n::text("Punto de malla"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT),zone_modified);
    for(const auto &field:{std::pair<const char *,const char *>{"mesh_dx",mapper_i18n::text("Desplazamiento X del punto")},{"mesh_dy",mapper_i18n::text("Desplazamiento Y del punto")}})
        obs_property_set_modified_callback(obs_properties_add_float(props,field.first,field.second,-1,1,.01),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_float(props,"mesh_radius",mapper_i18n::text("Radio suave (unidades locales)"),0,1.5,.01),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_float(props,"mesh_strength",mapper_i18n::text("Fuerza suave"),0,1,.01),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_float(props,"mesh_horizontal",mapper_i18n::text("Curvatura horizontal (desplaza Y)"),-.5,.5,.01),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_float(props,"mesh_vertical",mapper_i18n::text("Curvatura vertical (desplaza X)"),-.5,.5,.01),zone_modified);
    auto *mesh_action=obs_properties_add_list(props,"mesh_action",mapper_i18n::text("Acción de malla"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
    for(const auto &item:{std::pair<const char *,const char *>{mapper_i18n::text("Sin acción"),""},{mapper_i18n::text("Restablecer puntos"),"reset_points"},
        {mapper_i18n::text("Restablecer curvatura"),"reset_curves"},{mapper_i18n::text("Restablecer toda la malla"),"reset_all"}}) obs_property_list_add_string(mesh_action,item.first,item.second);
    obs_property_set_modified_callback(mesh_action,zone_modified);
    obs_properties_add_text(props,"mesh_status",mapper_i18n::text("Smooth Mesh"),OBS_TEXT_INFO);
    auto *mask_kind=obs_properties_add_list(props,"mask_kind",mapper_i18n::text("Máscara de zona"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    for(const auto &item:{std::pair<const char *,int>{mapper_i18n::text("Sin máscara"),0},{mapper_i18n::text("Elipse"),1},{mapper_i18n::text("Rectángulo"),2},{mapper_i18n::text("Imagen PNG / SVG"),3}})
        obs_property_list_add_int(mask_kind,item.first,item.second);
    obs_property_set_modified_callback(mask_kind,zone_modified);
    obs_property_set_modified_callback(obs_properties_add_path(props,"mask_path",mapper_i18n::text("Archivo de máscara"),OBS_PATH_FILE,mapper_i18n::text("Máscaras (*.png *.svg)"),nullptr),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_bool(props,"mask_luminance",mapper_i18n::text("Usar luminancia × alfa (desactivado: solo alfa)")),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_bool(props,"mask_invert",mapper_i18n::text("Invertir máscara")),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_float(props,"mask_strength",mapper_i18n::text("Intensidad de máscara"),0,1,.01),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_float(props,"mask_feather",mapper_i18n::text("Suavizado de borde (formas básicas)"),0,.25,.01),zone_modified);
    const char *shape_labels[]={"Centro X de máscara","Centro Y de máscara","Ancho de máscara","Alto de máscara","Radio de círculo / elipse"};
    for(size_t i=0;i<5;++i)obs_property_set_modified_callback(obs_properties_add_float(props,mapping::mask_dimensions[i].key,mapper_i18n::text(shape_labels[i]),0,1,.01),zone_modified);
    auto *mask_action=obs_properties_add_list(props,"mask_action",mapper_i18n::text("Acción de máscara"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(mask_action,mapper_i18n::text("Sin acción"),""); obs_property_list_add_string(mask_action,mapper_i18n::text("Recargar archivo"),"reload");
    obs_property_list_add_string(mask_action,mapper_i18n::text("Restablecer forma"),"reset_shape");
    obs_property_set_modified_callback(mask_action,zone_modified);
    obs_properties_add_text(props,"mask_status",mapper_i18n::text("Máscaras"),OBS_TEXT_INFO);
    for (const auto &item : {std::pair<const char *, const char *>{"zone_dst_x", mapper_i18n::text("Destino X (0–1)")},
            {"zone_dst_y", mapper_i18n::text("Destino Y (0–1)")}, {"zone_dst_w", mapper_i18n::text("Ancho destino (0–1)")}, {"zone_dst_h", mapper_i18n::text("Alto destino (0–1)")},
            {"zone_uv_x", mapper_i18n::text("UV X (0–1)")}, {"zone_uv_y", mapper_i18n::text("UV Y (0–1)")},
            {"zone_uv_w", mapper_i18n::text("Ancho UV (0–1)")}, {"zone_uv_h", mapper_i18n::text("Alto UV (0–1)")}})
        obs_property_set_modified_callback(obs_properties_add_float(props, item.first, item.second, 0, 1, 0.01), zone_modified);
    auto *settings = data ? obs_source_get_settings(static_cast<Mapper *>(data)->self) : obs_data_create();
    const auto layout = mapping::read(settings);
    refresh_zone_controls(props, settings, layout);
    obs_data_release(settings);
    obs_properties_add_text(props, "phase_info",
        mapper_i18n::text("Abre el editor para distribuir y deformar tus zonas. El fondo de la salida es transparente."), OBS_TEXT_INFO);
    obs_properties_add_text(props, "author_credit",
        "a plugin from <a href=\"https://discord.gg/dhcqZPDYh\">DjEmiMas</a>", OBS_TEXT_INFO);
    settings=data?obs_source_get_settings(static_cast<Mapper*>(data)->self):obs_data_create();advanced_modified(props,nullptr,settings);obs_data_release(settings);
    return props;
}
}

MODULE_EXPORT bool obs_module_load(void)
{
    obs_source_info info{};
    info.id = "obs_screen_mapper";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    info.get_name = name;
    info.create = create;
    info.destroy = destroy;
    info.get_defaults = defaults;
    info.get_properties = properties;
    info.update = update;
    info.get_width = width;
    info.get_height = height;
    info.video_render = render;
    info.video_tick = tick;
    info.enum_active_sources = enumerate;
    info.enum_all_sources = enumerate;
    obs_register_source(&info);
    return true;
}





