// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <obs-module.h>
#include "mask-image.hpp"
#include <memory>
#include <cmath>

namespace mapping {
struct MaskResource {
    MaskImage image;
    gs_texture_t *texture=nullptr;
    explicit MaskResource(const std::string &path):image(load_mask_image(path)) {}
    ~MaskResource() { if(texture) { obs_enter_graphics(); gs_texture_destroy(texture); obs_leave_graphics(); } }
    gs_texture_t *get_texture()
    {
        if(!texture && image.error.empty() && !image.pixels.empty()) {
            const uint8_t *pixels=image.pixels.data(); texture=gs_texture_create(image.width,image.height,GS_RGBA,1,&pixels,0);
            if(!texture) image.error="No se pudo crear la textura GPU de máscara.";
            else { std::vector<uint8_t> empty; image.pixels.swap(empty); }
        }
        return texture;
    }
};
struct Mask {
    // 0: disabled, 1: ellipse, 2: rectangle, 3: image
    int kind=0;
    bool luminance=false,invert=false;
    float strength=1,feather=0;
    std::string path;
    long long revision=0;
    std::shared_ptr<MaskResource> resource;
};
inline bool valid_mask(const Mask &mask)
{ return mask.kind>=0&&mask.kind<=3&&std::isfinite(mask.strength)&&mask.strength>=0&&mask.strength<=1&&std::isfinite(mask.feather)&&mask.feather>=0&&mask.feather<=.25f; }
inline const char *mask_shader=R"SHADER(
uniform float4x4 ViewProj;
uniform texture2d image;
uniform texture2d mask_image;
uniform float mask_kind;
uniform float mask_luminance;
uniform float mask_invert;
uniform float mask_strength;
uniform float mask_feather;
sampler_state linear_sampler { Filter=Linear; AddressU=Clamp; AddressV=Clamp; };
struct VertData { float4 pos:POSITION; float2 uv:TEXCOORD0; float2 mask_uv:TEXCOORD1; };
VertData VS(VertData input) { VertData output; output.pos=mul(float4(input.pos.xyz,1),ViewProj); output.uv=input.uv; output.mask_uv=input.mask_uv; return output; }
float4 PS(VertData input):TARGET {
    float value=1;
    if(mask_kind<1.5) {
        float distance=length((input.mask_uv-0.5)*2);
        if(mask_feather>0.00001) value=1-smoothstep(1-mask_feather*2,1,distance);
        else value=distance<=1?1:0;
    } else if(mask_kind<2.5) {
        float border=min(min(input.mask_uv.x,1-input.mask_uv.x),min(input.mask_uv.y,1-input.mask_uv.y));
        if(mask_feather>0.00001) value=smoothstep(0,mask_feather,border);
        else value=border>=0?1:0;
    } else {
        float4 mask=mask_image.Sample(linear_sampler,input.mask_uv);
        value=mask.a;
        if(mask_luminance>0.5) value=dot(mask.rgb,float3(0.2126,0.7152,0.0722))*mask.a;
    }
    if(mask_invert>0.5) value=1-value;
    float coverage=lerp(1,saturate(value),mask_strength);
    return image.Sample(linear_sampler,input.uv)*coverage;
}
technique Draw { pass { vertex_shader=VS(input); pixel_shader=PS(input); } }
)SHADER";
}
