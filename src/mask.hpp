// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "i18n.hpp"
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
            if(!texture) image.error=mapper_i18n::text("No se pudo crear la textura GPU de máscara.");
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
    float center_x=.5f,center_y=.5f,width=1,height=1,radius=.5f;
    std::string path;
    long long revision=0;
    std::shared_ptr<MaskResource> resource;
};
struct MaskDimension {const char *key;float Mask::*member;};
inline constexpr MaskDimension mask_dimensions[]={{"mask_center_x",&Mask::center_x},{"mask_center_y",&Mask::center_y},{"mask_width",&Mask::width},{"mask_height",&Mask::height},{"mask_radius",&Mask::radius}};
inline void reset_mask_shape(Mask &mask){mask.center_x=mask.center_y=mask.radius=.5f;mask.width=mask.height=1;}
inline bool valid_mask(const Mask &mask)
{
    auto unit=[](float value){return std::isfinite(value)&&value>=0&&value<=1;};
    return mask.kind>=0&&mask.kind<=3&&unit(mask.strength)&&std::isfinite(mask.feather)&&mask.feather>=0&&mask.feather<=.25f&&
        unit(mask.center_x)&&unit(mask.center_y)&&unit(mask.width)&&unit(mask.height)&&unit(mask.radius);
}
inline const char *mask_shader=R"SHADER(
uniform float4x4 ViewProj;
uniform texture2d image;
uniform texture2d mask_image;
uniform float mask_kind;
uniform float mask_luminance;
uniform float mask_invert;
uniform float mask_strength;
uniform float mask_feather;
uniform float mask_center_x;
uniform float mask_center_y;
uniform float mask_width;
uniform float mask_height;
uniform float mask_radius;
uniform float transform_enabled;
uniform float transform_x; uniform float transform_y; uniform float transform_z;
uniform float transform_rx; uniform float transform_ry; uniform float transform_rz;
uniform float transform_scale; uniform float transform_pivot_x; uniform float transform_pivot_y;
uniform float zone_x; uniform float zone_y; uniform float zone_w; uniform float zone_h;
uniform float canvas_w; uniform float canvas_h;
uniform float corner_a; uniform float corner_b; uniform float corner_c; uniform float corner_d;
uniform float corner_e; uniform float corner_f; uniform float corner_g; uniform float corner_h;
sampler_state linear_sampler { Filter=Linear; AddressU=Clamp; AddressV=Clamp; };
struct VertData { float4 pos:POSITION; float2 uv:TEXCOORD0; float2 mask_uv:TEXCOORD1; };
VertData VS(VertData input) {
    VertData output;output.pos=mul(float4(input.pos.xyz,1),ViewProj);
    if(transform_enabled>0.5){
        float2 local=(input.pos.xy-float2(zone_x,zone_y))/float2(zone_w,zone_h);
        float hw=corner_g*local.x+corner_h*local.y+1;
        float2 mapped=float2(corner_a*local.x+corner_b*local.y+corner_c,corner_d*local.x+corner_e*local.y+corner_f)/max(hw,0.000001);
        float pw=corner_g*transform_pivot_x+corner_h*transform_pivot_y+1;
        float2 pivotLocal=float2(corner_a*transform_pivot_x+corner_b*transform_pivot_y+corner_c,corner_d*transform_pivot_x+corner_e*transform_pivot_y+corner_f)/pw;
        float2 pivot=float2(zone_x,zone_y)+float2(zone_w,zone_h)*pivotLocal;
        float3 p=float3((float2(zone_x,zone_y)+mapped*float2(zone_w,zone_h)-pivot)*transform_scale,0);
        float ax=transform_rx*0.01745329252,ay=transform_ry*0.01745329252,az=transform_rz*0.01745329252;
        p=float3(p.x,p.y*cos(ax)-p.z*sin(ax),p.y*sin(ax)+p.z*cos(ax));
        p=float3(p.x*cos(ay)+p.z*sin(ay),p.y,-p.x*sin(ay)+p.z*cos(ay));
        p=float3(p.x*cos(az)-p.y*sin(az),p.x*sin(az)+p.y*cos(az),p.z);
        p.xy+=pivot+float2(transform_x*canvas_w,transform_y*canvas_h);
        float camera=2*max(canvas_w,canvas_h),w=1+(p.z+transform_z*max(canvas_w,canvas_h))/camera;
        float2 center=float2(canvas_w,canvas_h)*0.5;
        float2 projected=(p.xy-center)+center*w;
        output.pos=mul(float4(projected,0,w),ViewProj)*hw;
        output.pos.z=(w-0.02)*(100.0/99.98)*hw;
    }
    output.uv=input.uv;output.mask_uv=input.mask_uv;return output;
}
float4 PS(VertData input):TARGET {
    float value=1;
    if(mask_kind<0.5) { value=1; }
    else if(mask_kind<1.5) {
        float2 axes=float2(mask_width,mask_height)*mask_radius;
        float distance=length((input.mask_uv-float2(mask_center_x,mask_center_y))/max(axes,float2(0.000001,0.000001)));
        if(mask_feather>0.00001) value=1-smoothstep(1-mask_feather*2,1,distance);
        else value=distance<=1?1:0;
        if(min(axes.x,axes.y)<=0) value=0;
    } else if(mask_kind<2.5) {
        float2 border_xy=float2(mask_width,mask_height)*0.5-abs(input.mask_uv-float2(mask_center_x,mask_center_y));
        float border=min(border_xy.x,border_xy.y);
        if(mask_feather>0.00001) value=smoothstep(0,mask_feather,border);
        else value=border>=0?1:0;
        if(min(mask_width,mask_height)<=0) value=0;
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

