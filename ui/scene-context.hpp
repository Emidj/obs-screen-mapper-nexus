// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <obs.h>
#include <graphics/matrix4.h>
#include <QTransform>
#include <memory>
#include <set>
#include <vector>

namespace mapper_scene {
struct Items {
    std::vector<obs_sceneitem_t*> values;
    explicit Items(obs_scene_t *scene){obs_scene_enum_items(scene,[](obs_scene_t*,obs_sceneitem_t *item,void *data){obs_sceneitem_addref(item);static_cast<Items*>(data)->values.push_back(item);return true;},this);}
    ~Items(){for(auto *item:values)obs_sceneitem_release(item);}
};
inline bool contains(obs_scene_t *scene,obs_source_t *target,std::set<obs_scene_t*> &seen,int depth=0){
    if(!scene||depth>32||!seen.insert(scene).second)return false;
    Items items(scene);for(auto *item:items.values){auto *source=obs_sceneitem_get_source(item);if(source==target)return true;if(contains(obs_group_or_scene_from_source(source),target,seen,depth+1))return true;}return false;
}
inline bool contains(obs_scene_t *scene,obs_source_t *target){std::set<obs_scene_t*> seen;return contains(scene,target,seen);}
inline void copyItem(obs_sceneitem_t *to,obs_sceneitem_t *from){
    obs_transform_info info{};obs_sceneitem_get_info2(from,&info);obs_sceneitem_crop crop{};obs_sceneitem_get_crop(from,&crop);
    obs_sceneitem_set_crop(to,&crop);obs_sceneitem_set_info2(to,&info);obs_sceneitem_set_visible(to,obs_sceneitem_visible(from));obs_sceneitem_set_locked(to,obs_sceneitem_locked(from));
    obs_sceneitem_set_scale_filter(to,obs_sceneitem_get_scale_filter(from));obs_sceneitem_set_blending_method(to,obs_sceneitem_get_blending_method(from));obs_sceneitem_set_blending_mode(to,obs_sceneitem_get_blending_mode(from));
    obs_sceneitem_set_order_position(to,obs_sceneitem_get_order_position(from));obs_sceneitem_force_update_transform(to);
}
// Container filters may report zero dimensions until rendered or change the
// coordinate system. Omit them only on the private editing reference. Filters
// on shared video sources remain intact, as does every original scene filter.
inline size_t removeReferenceFilters(obs_source_t *source){
    std::vector<obs_source_t*> filters;
    obs_source_enum_filters(source,[](obs_source_t*,obs_source_t *filter,void *data){
        if(auto *reference=obs_source_get_ref(filter))static_cast<std::vector<obs_source_t*>*>(data)->push_back(reference);
    },&filters);
    for(auto *filter:filters){obs_source_filter_remove(source,filter);obs_source_release(filter);}
    return filters.size();
}
// Only scene/group containers on the edited path are copied. Video sources are shared.
inline obs_scene_t *clone(obs_scene_t *scene,obs_source_t *target,obs_source_t *draft,size_t &omittedFilters,int depth=0){
    if(depth>32)return nullptr;
    auto *copy=obs_scene_duplicate(scene,"Mapper scene reference",OBS_SCENE_DUP_PRIVATE_REFS);if(!copy)return nullptr;
    omittedFilters+=removeReferenceFilters(obs_scene_get_source(copy));
    Items items(copy);for(auto *item:items.values){auto *source=obs_sceneitem_get_source(item);auto *child=obs_group_or_scene_from_source(source);obs_scene_t *nested=nullptr;obs_source_t *replacement=nullptr;
        if(source==target)replacement=draft;
        else if(child&&contains(child,target)){nested=clone(child,target,draft,omittedFilters,depth+1);if(!nested){obs_scene_release(copy);return nullptr;}replacement=obs_scene_get_source(nested);}
        if(replacement){auto *added=obs_scene_add(copy,replacement);if(!added){if(nested)obs_scene_release(nested);obs_scene_release(copy);return nullptr;}copyItem(added,item);obs_sceneitem_remove(item);if(nested)obs_scene_release(nested);}
    }return copy;
}
struct Placement {QTransform transform;bool visible=true,boundsCrop=false;};
inline void placements(obs_scene_t *scene,obs_source_t *draft,const QTransform &parent,bool visible,bool boundsCrop,std::vector<Placement> &result,int depth=0){
    if(depth>32)return;
    Items items(scene);for(auto *item:items.values){obs_sceneitem_force_update_transform(item);matrix4 matrix;obs_sceneitem_get_draw_transform(item,&matrix);obs_sceneitem_crop crop{};obs_sceneitem_get_crop(item,&crop);
        QTransform local(matrix.x.x,matrix.x.y,matrix.y.x,matrix.y.y,matrix.t.x,matrix.t.y);
        const auto transform=QTransform::fromTranslate(-crop.left,-crop.top)*local*parent;
        const bool shown=visible&&obs_sceneitem_visible(item),cropped=boundsCrop||obs_sceneitem_get_bounds_crop(item);
        auto *source=obs_sceneitem_get_source(item);if(source==draft)result.push_back({transform,shown,cropped});
        else if(auto *child=obs_group_or_scene_from_source(source))placements(child,draft,transform,shown,cropped,result,depth+1);
    }
}
struct Context {
    obs_scene_t *scene=nullptr;
    obs_source_t *original=nullptr;
    QTransform transform;
    matrix4 matrix{};
    uint32_t width=0,height=0;
    uint32_t mapperWidth=0,mapperHeight=0;
    size_t occurrences=0;
    size_t omittedFilters=0;
    bool visible=true;
    ~Context(){if(scene){obs_source_dec_showing(obs_scene_get_source(scene));obs_scene_release(scene);}obs_source_release(original);}
};
inline std::shared_ptr<Context> create(obs_scene_t *original,obs_source_t *target,obs_source_t *draft,QString &error){
    if(!contains(original,target)){error=QString::fromUtf8(mapper_i18n::text("La escena ya no contiene este Mapper."));return {};}
    size_t omittedFilters=0;
    auto *copy=clone(original,target,draft,omittedFilters);if(!copy){error=QString::fromUtf8(mapper_i18n::text("No se pudo crear la referencia de escena."));return {};}
    std::vector<Placement> found;placements(copy,draft,{},true,false,found);
    auto chosen=std::find_if(found.begin(),found.end(),[](const Placement &p){return p.visible;});if(chosen==found.end()&&!found.empty())chosen=found.begin();
    if(chosen==found.end()||!chosen->transform.isInvertible()||chosen->boundsCrop||!obs_source_get_width(obs_scene_get_source(copy))||!obs_source_get_height(obs_scene_get_source(copy))){obs_scene_release(copy);error=QString::fromUtf8(mapper_i18n::text("La posición del Mapper no permite editar en contexto: revisa escala y recorte a límites."));return {};}
    auto context=std::make_shared<Context>();context->scene=copy;context->original=obs_source_get_ref(obs_scene_get_source(original));context->transform=chosen->transform;context->occurrences=found.size();context->visible=chosen->visible;context->omittedFilters=omittedFilters;
    context->width=obs_source_get_width(obs_scene_get_source(copy));context->height=obs_source_get_height(obs_scene_get_source(copy));
    context->mapperWidth=obs_source_get_width(draft);context->mapperHeight=obs_source_get_height(draft);
    matrix4_identity(&context->matrix);const auto &m=context->transform;context->matrix.x.x=float(m.m11());context->matrix.x.y=float(m.m12());context->matrix.y.x=float(m.m21());context->matrix.y.y=float(m.m22());context->matrix.t.x=float(m.dx());context->matrix.t.y=float(m.dy());
    obs_source_inc_showing(obs_scene_get_source(copy));return context;
}
}

