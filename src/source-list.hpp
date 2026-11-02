// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <obs.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>
namespace mapper_sources {
struct Entry { std::string name, uuid; std::wstring sortName; int category; };
inline std::wstring wide(const char *text) {
    int size=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);
    std::wstring result(size>0?size:1,L'\0');
    if(size>0)MultiByteToWideChar(CP_UTF8,0,text,-1,result.data(),size);
    result.resize(result.size()-1);return result;
}
inline std::vector<Entry> collect(obs_source_t *self) {
    struct Context { obs_source_t *self; std::vector<Entry> entries; } context{self,{}};
    auto callback=[](void *data,obs_source_t *source) {
        auto &c=*static_cast<Context*>(data);
        if(source==c.self || obs_source_removed(source) || !(obs_source_get_output_flags(source)&OBS_SOURCE_VIDEO))return true;
        std::string uuid=obs_source_get_uuid(source);
        for(const auto &entry:c.entries)if(entry.uuid==uuid)return true;
        const char *name=obs_source_get_name(source);
        c.entries.push_back({name,uuid,wide(name),obs_source_is_group(source)?1:obs_source_get_type(source)==OBS_SOURCE_TYPE_SCENE?2:0});return true;
    };
    obs_enum_sources(callback,&context);obs_enum_scenes(callback,&context);
    const auto locale=wide(obs_get_locale());
    std::sort(context.entries.begin(),context.entries.end(),[&](const Entry &a,const Entry &b){
        if(a.category!=b.category)return a.category<b.category;
        int order=CompareStringEx(locale.c_str(),NORM_IGNORECASE,a.sortName.c_str(),-1,b.sortName.c_str(),-1,nullptr,nullptr,0);
        if(order==CSTR_LESS_THAN)return true;if(order==CSTR_GREATER_THAN)return false;
        if(a.name!=b.name)return a.name<b.name;return a.uuid<b.uuid;
    });return context.entries;
}
inline const char *label(int category){return category==0?"Fuentes":category==1?"Grupos":"Escenas";}
}

