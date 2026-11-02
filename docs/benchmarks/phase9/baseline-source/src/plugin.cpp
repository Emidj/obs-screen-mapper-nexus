// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs-module.h>
#include <atomic>
#include <new>
#include <mutex>
#include <string>
#include <memory>
#include <graphics/vec4.h>
#include "zones.hpp"
#include <unordered_map>
#include <util/bmem.h>
#include <cstring>

OBS_DECLARE_MODULE()
MODULE_EXPORT const char *obs_module_name(void) { return "OBS Screen Mapper"; }
MODULE_EXPORT const char *obs_module_description(void) { return "Independent mapping source - shared OBS input"; }
MODULE_EXPORT const char *obs_module_author(void) { return "DjEmiMas"; }

namespace {
constexpr int min_size = 16;
constexpr int max_size = 16384;
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
    std::unordered_map<std::string,std::weak_ptr<mapping::MaskResource>> masks;
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
    obs_data_set_default_string(settings, "zone_name", "Zona 1");
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
    mapper->height.store(dimension(settings, "canvas_height"), std::memory_order_relaxed);
    auto layout = mapping::read(settings);
    for(auto &zone:layout.zones) {
        zone.surface=mapping::compile_surface(zone.mesh,zone.polygon);
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
void draw_zone(const mapping::Zone &zone,mapping::Rect uv,float cw,float ch)
{
    const auto d=zone.destination;
    auto emit=[&](const mapping::Vertex &point,float mx,float my) {
        gs_texcoord(uv.x+point.u*uv.w,uv.y+point.v*uv.h,0); gs_texcoord(mx,my,1);
        gs_vertex2f((d.x+point.x*d.w)*cw,(d.y+point.y*d.h)*ch);
    };
    if(zone.mesh.enabled) {
        if(zone.surface.triangles.empty()) return;
        gs_render_start(true);
        for(const auto &point:zone.surface.triangles) emit(point,point.mask_x,point.mask_y);
        gs_render_stop(GS_TRIS); return;
    }
    if(zone.polygon.enabled) {
        if(zone.polygon.triangles.empty()) return;
        gs_render_start(true);
        for(size_t index:zone.polygon.triangles) { const auto &point=zone.polygon.vertices[index]; emit(point,point.x,point.y); }
        gs_render_stop(GS_TRIS); return;
    }
    gs_render_start(true);
    emit({0,0,0,0},0,0); emit({1,0,1,0},1,0); emit({0,1,0,1},0,1); emit({1,1,1,1},1,1);
    gs_render_stop(GS_TRISTRIP);
}
void render(void *data, gs_effect_t *)
{
    static thread_local unsigned render_depth = 0;
    if (render_depth >= 32) return; // Last-resort guard, including foreign plugins.
    auto *mapper = static_cast<Mapper *>(data);
    auto layout = std::atomic_load(&mapper->layout);
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
                if(!mapper->output) mapper->output=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
                if(!mapper->output) { gs_blend_state_pop(); --render_depth; obs_source_release(input); return; }
                gs_texrender_reset(mapper->output);
                if(!gs_texrender_begin(mapper->output,uint32_t(cw),uint32_t(ch))) { gs_blend_state_pop(); --render_depth; obs_source_release(input); return; }
                vec4 empty;vec4_zero(&empty);gs_clear(GS_CLEAR_COLOR,&empty,0,0);gs_ortho(0,cw,0,ch,-100,100);
                for(size_t i=0;i<layout->zones.size();++i) {
                    const auto &zone=layout->zones[i]; const auto d=zone.destination,uv=mapping::source_uv(*layout,i);
                    if(!zone.visible || d.w<=0 || d.h<=0 || uv.w<=0 || uv.h<=0 || !mapping::valid_mask(zone.mask)) continue;
                    auto *effect=obs_get_base_effect(OBS_EFFECT_DEFAULT);
                    if(zone.mask.kind!=0 && zone.mask.strength>0) {
                        if(!mapper->mask_effect && !mapper->mask_shader_failed) {
                            char *error=nullptr;
                            mapper->mask_effect=gs_effect_create(mapping::mask_shader,"obs-screen-mapper-mask.effect",&error);
                            if(!mapper->mask_effect) { mapper->mask_shader_failed=true; blog(LOG_WARNING,"[OBS Screen Mapper] Mask shader: %s",error?error:"failed"); }
                            bfree(error);
                        }
                        if(!mapper->mask_effect) continue;
                        effect=mapper->mask_effect;
                        auto *mask_texture=texture;
                        if(zone.mask.kind==3) {
                            if(!zone.mask.resource || !(mask_texture=zone.mask.resource->get_texture())) continue;
                        }
                        gs_effect_set_texture(gs_effect_get_param_by_name(effect,"mask_image"),mask_texture);
                        gs_effect_set_float(gs_effect_get_param_by_name(effect,"mask_kind"),float(zone.mask.kind));
                        gs_effect_set_float(gs_effect_get_param_by_name(effect,"mask_luminance"),zone.mask.luminance?1:0);
                        gs_effect_set_float(gs_effect_get_param_by_name(effect,"mask_invert"),zone.mask.invert?1:0);
                        gs_effect_set_float(gs_effect_get_param_by_name(effect,"mask_strength"),zone.mask.strength);
                        gs_effect_set_float(gs_effect_get_param_by_name(effect,"mask_feather"),zone.mask.feather);
                    }
                    gs_effect_set_texture(gs_effect_get_param_by_name(effect,"image"),texture);
                    while(gs_effect_loop(effect,"Draw")) draw_zone(zone,uv,cw,ch);
                }
                gs_texrender_end(mapper->output);
                auto *result=gs_texrender_get_texture(mapper->output);auto *effect=obs_get_base_effect(OBS_EFFECT_DEFAULT);
                gs_effect_set_texture(gs_effect_get_param_by_name(effect,"image"),result);
                while(gs_effect_loop(effect,"Draw")) gs_draw_sprite(result,0,uint32_t(cw),uint32_t(ch));
            }
            gs_blend_state_pop();
        }
        --render_depth;
    }
    obs_source_release(input);
}
struct ListContext { obs_property_t *list; obs_source_t *self; };
bool list_source(void *data, obs_source_t *source)
{
    auto *context = static_cast<ListContext *>(data);
    if (source != context->self && !obs_source_removed(source) &&
        (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO))
        obs_property_list_add_string(context->list, obs_source_get_name(source), obs_source_get_uuid(source));
    return true;
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
        obs_property_list_add_int(vertices, ("Vértice " + std::to_string(i+1)).c_str(), static_cast<long long>(i));
    obs_property_set_enabled(obs_properties_get(props,"zone_polygon"),present);
    for (const char *field : {"selected_vertex", "vertex_action", "vertex_x", "vertex_y", "vertex_u", "vertex_v"})
        obs_property_set_enabled(obs_properties_get(props,field),polygon && polygon->enabled);
    const std::string status = !polygon || !polygon->enabled ? "Polygon desactivado: destino rectangular." :
        polygon->error.empty() ? "Polígono válido: " + std::to_string(polygon->vertices.size()) + " vértices." : polygon->error + " Salida de esta zona transparente.";
    obs_property_set_description(obs_properties_get(props,"polygon_status"), status.c_str());
    const auto *mesh=present?&layout.zones[mapping::selection(settings,layout)].mesh:nullptr;
    obs_property_set_enabled(obs_properties_get(props,"zone_mesh"),present);
    auto *controls=obs_properties_get(props,"mesh_selected"); obs_property_list_clear(controls);
    if(mesh) for(size_t i=0;i<mesh->points.size();++i)
        obs_property_list_add_int(controls,("Fila "+std::to_string(i/size_t(mesh->columns)+1)+", columna "+std::to_string(i%size_t(mesh->columns)+1)).c_str(),static_cast<long long>(i));
    for(const char *field:{"mesh_columns","mesh_rows","mesh_smooth","mesh_selected","mesh_dx","mesh_dy","mesh_soft","mesh_radius","mesh_strength",
        "mesh_wave","mesh_horizontal","mesh_vertical","mesh_action"}) obs_property_set_enabled(obs_properties_get(props,field),mesh && mesh->enabled);
    std::string mesh_status="Malla desactivada.";
    if(mesh && mesh->enabled) {
        auto copy=*mesh; const auto surface=mapping::compile_surface(copy,layout.zones[mapping::selection(settings,layout)].polygon);
        mesh_status=surface.error.empty()?"Malla válida; Polygon y malla pueden estar activos a la vez.":surface.error+" Salida de esta zona transparente.";
    }
    obs_property_set_description(obs_properties_get(props,"mesh_status"),mesh_status.c_str());
    const auto *mask=present?&layout.zones[mapping::selection(settings,layout)].mask:nullptr;
    obs_property_set_enabled(obs_properties_get(props,"mask_kind"),present);
    for(const char *field:{"mask_invert","mask_strength"}) obs_property_set_enabled(obs_properties_get(props,field),mask && mask->kind!=0);
    for(const char *field:{"mask_path","mask_luminance","mask_action"}) obs_property_set_enabled(obs_properties_get(props,field),mask && mask->kind==3);
    obs_property_set_enabled(obs_properties_get(props,"mask_feather"),mask && (mask->kind==1 || mask->kind==2));
    obs_property_set_description(obs_properties_get(props,"mask_status"),
        mask && !mapping::valid_mask(*mask)?"Parámetros de máscara no válidos: salida transparente.":
        "PNG hasta 4096 px; SVG nativo de Direct2D rasterizado a 1024 px. Si falla la carga, la zona queda transparente; consulta el registro OBS. Recarga para releer un archivo modificado.");
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
            if (zone.name.empty()) zone.name = "Zona " + zone.id;
        } else if (field == "zone_visible") zone.visible = obs_data_get_bool(settings, "zone_visible");
        else if (field.compare(0, 9, "zone_dst_") == 0) {
            const auto rect = mapping::read_rect(settings, "zone_dst_");
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
        else if(field=="mask_action") {
            const std::string operation=obs_data_get_string(settings,"mask_action");
            if(operation=="reload") zone.mask.revision=zone.mask.revision<9223372036854775807LL?zone.mask.revision+1:0;
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
    if(!open) { MessageBoxW(nullptr,L"No se pudo cargar el editor. Coloca obs-screen-mapper-ui.dll junto a la DLL principal y usa OBS 32.x con Qt 6.",L"OBS Screen Mapper",MB_OK|MB_ICONERROR);return false; }
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
    obs_properties_add_button2(props,"open_editor","Abrir Mapper Editor…",open_editor,data);
    auto *list = obs_properties_add_list(props, "input_uuid", "Fuente OBS existente", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(list, "Ninguna (transparente)", "");
    ListContext context{list, data ? static_cast<Mapper *>(data)->self : nullptr};
    obs_enum_sources(list_source, &context);
    obs_enum_scenes(list_source, &context);
    obs_properties_add_int(props, "canvas_width", "Ancho del canvas", min_size, max_size, 1);
    obs_properties_add_int(props, "canvas_height", "Alto del canvas", min_size, max_size, 1);
    obs_property_set_modified_callback(obs_properties_add_bool(props,"show_advanced","Mostrar controles numéricos avanzados"),advanced_modified);
    auto *mode = obs_properties_add_list(props, "distribution", "Distribución", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(mode, "Split", "split");
    obs_property_list_add_string(mode, "Repeat", "repeat");
    obs_property_list_add_string(mode, "Custom UV", "custom");
    obs_property_set_modified_callback(mode, zone_modified);
    auto *selected = obs_properties_add_list(props, "selected_zone", "Zona seleccionada", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_set_modified_callback(selected, zone_modified);
    auto *operation = obs_properties_add_list(props, "zone_action", "Acción de zona", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    for (const auto &item : {std::pair<const char *, const char *>{"Sin acción", ""}, {"Añadir", "add"},
            {"Duplicar seleccionada", "duplicate"}, {"Eliminar seleccionada", "delete"},
            {"Subir en el orden", "up"}, {"Bajar en el orden", "down"}, {"Distribuir destinos en horizontal", "arrange"}})
        obs_property_list_add_string(operation, item.first, item.second);
    obs_property_set_modified_callback(operation, zone_modified);
    obs_property_set_modified_callback(obs_properties_add_text(props, "zone_name", "Nombre de zona", OBS_TEXT_DEFAULT), zone_modified);
    obs_property_set_modified_callback(obs_properties_add_bool(props, "zone_visible", "Zona visible"), zone_modified);
    obs_property_set_modified_callback(obs_properties_add_bool(props,"zone_polygon","Polygon"),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_list(props,"selected_vertex","Vértice seleccionado",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT),zone_modified);
    auto *vertex_action = obs_properties_add_list(props,"vertex_action","Acción de vértice",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(vertex_action,"Sin acción","");
    obs_property_list_add_string(vertex_action,"Insertar punto en la arista siguiente","insert");
    obs_property_list_add_string(vertex_action,"Eliminar punto (mínimo tres)","remove");
    obs_property_list_add_string(vertex_action,"Restablecer contorno rectangular","reset");
    obs_property_set_modified_callback(vertex_action,zone_modified);
    for (const auto &field : {std::pair<const char *,const char *>{"vertex_x","Punto X local (0–1)"}, {"vertex_y","Punto Y local (0–1)"},
        {"vertex_u","Punto U local (0–1)"},{"vertex_v","Punto V local (0–1)"}})
        obs_property_set_modified_callback(obs_properties_add_float(props,field.first,field.second,0,1,0.01),zone_modified);
    obs_properties_add_text(props,"polygon_status","Polygon",OBS_TEXT_INFO);
    for(const auto &field:{std::pair<const char *,const char *>{"zone_mesh","Smooth Mesh"},{"mesh_smooth","Interpolación suave"},
        {"mesh_soft","Soft Selection"},{"mesh_wave","Curvatura con perfil de onda (desactivado: arco)"}})
        obs_property_set_modified_callback(obs_properties_add_bool(props,field.first,field.second),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_int(props,"mesh_columns","Columnas de puntos",2,16,1),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_int(props,"mesh_rows","Filas de puntos",2,16,1),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_list(props,"mesh_selected","Punto de malla",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT),zone_modified);
    for(const auto &field:{std::pair<const char *,const char *>{"mesh_dx","Desplazamiento X del punto"},{"mesh_dy","Desplazamiento Y del punto"}})
        obs_property_set_modified_callback(obs_properties_add_float(props,field.first,field.second,-1,1,.01),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_float(props,"mesh_radius","Radio suave (unidades locales)",0,1.5,.01),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_float(props,"mesh_strength","Fuerza suave",0,1,.01),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_float(props,"mesh_horizontal","Curvatura horizontal (desplaza Y)",-.5,.5,.01),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_float(props,"mesh_vertical","Curvatura vertical (desplaza X)",-.5,.5,.01),zone_modified);
    auto *mesh_action=obs_properties_add_list(props,"mesh_action","Acción de malla",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
    for(const auto &item:{std::pair<const char *,const char *>{"Sin acción",""},{"Restablecer puntos","reset_points"},
        {"Restablecer curvatura","reset_curves"},{"Restablecer toda la malla","reset_all"}}) obs_property_list_add_string(mesh_action,item.first,item.second);
    obs_property_set_modified_callback(mesh_action,zone_modified);
    obs_properties_add_text(props,"mesh_status","Smooth Mesh",OBS_TEXT_INFO);
    auto *mask_kind=obs_properties_add_list(props,"mask_kind","Máscara de zona",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    for(const auto &item:{std::pair<const char *,int>{"Sin máscara",0},{"Elipse",1},{"Rectángulo",2},{"Imagen PNG / SVG",3}})
        obs_property_list_add_int(mask_kind,item.first,item.second);
    obs_property_set_modified_callback(mask_kind,zone_modified);
    obs_property_set_modified_callback(obs_properties_add_path(props,"mask_path","Archivo de máscara",OBS_PATH_FILE,"Máscaras (*.png *.svg)",nullptr),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_bool(props,"mask_luminance","Usar luminancia × alfa (desactivado: solo alfa)"),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_bool(props,"mask_invert","Invertir máscara"),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_float(props,"mask_strength","Intensidad de máscara",0,1,.01),zone_modified);
    obs_property_set_modified_callback(obs_properties_add_float(props,"mask_feather","Suavizado de borde (formas básicas)",0,.25,.01),zone_modified);
    auto *mask_action=obs_properties_add_list(props,"mask_action","Acción de máscara",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(mask_action,"Sin acción",""); obs_property_list_add_string(mask_action,"Recargar archivo","reload");
    obs_property_set_modified_callback(mask_action,zone_modified);
    obs_properties_add_text(props,"mask_status","Máscaras",OBS_TEXT_INFO);
    for (const auto &item : {std::pair<const char *, const char *>{"zone_dst_x", "Destino X (0–1)"},
            {"zone_dst_y", "Destino Y (0–1)"}, {"zone_dst_w", "Ancho destino (0–1)"}, {"zone_dst_h", "Alto destino (0–1)"},
            {"zone_uv_x", "UV X (0–1)"}, {"zone_uv_y", "UV Y (0–1)"},
            {"zone_uv_w", "Ancho UV (0–1)"}, {"zone_uv_h", "Alto UV (0–1)"}})
        obs_property_set_modified_callback(obs_properties_add_float(props, item.first, item.second, 0, 1, 0.01), zone_modified);
    auto *settings = data ? obs_source_get_settings(static_cast<Mapper *>(data)->self) : obs_data_create();
    const auto layout = mapping::read(settings);
    refresh_zone_controls(props, settings, layout);
    obs_data_release(settings);
    obs_properties_add_text(props, "phase_info",
        "Abre el editor para distribuir y deformar tus zonas. El fondo de la salida es transparente.", OBS_TEXT_INFO);
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
