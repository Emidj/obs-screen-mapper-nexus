// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <algorithm>
namespace mapping {
struct Corner {float x,y;};
struct Transform {
    float x=0,y=0,z=0,rx=0,ry=0,rz=0,scale=1,pivot_x=.5f,pivot_y=.5f;
    bool pinned=false;
    std::array<Corner,4> corners{{{0,0},{1,0},{1,1},{0,1}}};
};
struct TransformField {const char *key;float Transform::*member;float min,max;};
inline constexpr TransformField transform_fields[]={
    {"transform_x",&Transform::x,-10,10},{"transform_y",&Transform::y,-10,10},{"transform_z",&Transform::z,-1,10},
    {"transform_rx",&Transform::rx,-360,360},{"transform_ry",&Transform::ry,-360,360},{"transform_rz",&Transform::rz,-360,360},
    {"transform_scale",&Transform::scale,0,10},{"transform_pivot_x",&Transform::pivot_x,0,1},{"transform_pivot_y",&Transform::pivot_y,0,1}};
struct Homography {float a=1,b=0,c=0,d=0,e=1,f=0,g=0,h=0;bool valid=true;};
inline Homography homography(const Transform &t){
    Homography m;if(!t.pinned)return m;
    const auto &p=t.corners;
    for(size_t i=0;i<4;++i){const auto a=p[i],b=p[(i+1)%4],c=p[(i+2)%4];
        if(!std::isfinite(a.x)||!std::isfinite(a.y)||std::abs(a.x)>4||std::abs(a.y)>4||
           (double(b.x)-a.x)*(double(c.y)-b.y)-(double(b.y)-a.y)*(double(c.x)-b.x)<=1e-6){m.valid=false;return m;}}
    const double dx1=p[1].x-p[2].x,dx2=p[3].x-p[2].x,dx3=p[0].x-p[1].x+p[2].x-p[3].x;
    const double dy1=p[1].y-p[2].y,dy2=p[3].y-p[2].y,dy3=p[0].y-p[1].y+p[2].y-p[3].y;
    if(std::abs(dx3)+std::abs(dy3)>1e-8){const double det=dx1*dy2-dx2*dy1;if(std::abs(det)<1e-10){m.valid=false;return m;}
        m.g=float((dx3*dy2-dx2*dy3)/det);m.h=float((dx1*dy3-dx3*dy1)/det);}
    m.a=p[1].x-p[0].x+m.g*p[1].x;m.b=p[3].x-p[0].x+m.h*p[3].x;m.c=p[0].x;
    m.d=p[1].y-p[0].y+m.g*p[1].y;m.e=p[3].y-p[0].y+m.h*p[3].y;m.f=p[0].y;
    for(const auto uv:std::array<Corner,4>{{{0,0},{1,0},{1,1},{0,1}}})if(m.g*uv.x+m.h*uv.y+1<=.0001f)m.valid=false;
    return m;
}
inline bool valid_transform(const Transform &t){for(const auto &f:transform_fields){const float v=t.*(f.member);if(!std::isfinite(v)||v<f.min||v>f.max)return false;}return homography(t).valid;}
inline bool transformed(const Transform &t){return t.pinned||t.x!=0||t.y!=0||t.z!=0||t.rx!=0||t.ry!=0||t.rz!=0||t.scale!=1;}
struct Projected {float x=0,y=0,w=1;bool visible=true;};
inline Projected project(const Transform &t,float dx,float dy,float dw,float dh,float u,float v,float width,float height){
    if(!transformed(t))return {dx+dw*u,dy+dh*v,1,true};
    const auto h=homography(t);if(!h.valid||!valid_transform(t))return {0,0,0,false};
    const float hw=h.g*u+h.h*v+1;if(hw<=.0001f)return {0,0,0,false};
    const float lu=(h.a*u+h.b*v+h.c)/hw,lv=(h.d*u+h.e*v+h.f)/hw;
    const float pw=h.g*t.pivot_x+h.h*t.pivot_y+1;
    const float px=(dx+dw*(h.a*t.pivot_x+h.b*t.pivot_y+h.c)/pw)*width,py=(dy+dh*(h.d*t.pivot_x+h.e*t.pivot_y+h.f)/pw)*height;
    float x=((dx+dw*lu)*width-px)*t.scale,y=((dy+dh*lv)*height-py)*t.scale,z=0;
    constexpr float rad=.017453292519943295f;
    const float ax=t.rx*rad,ay=t.ry*rad,az=t.rz*rad;
    const float y1=y*std::cos(ax)-z*std::sin(ax),z1=y*std::sin(ax)+z*std::cos(ax);y=y1;z=z1;
    const float x1=x*std::cos(ay)+z*std::sin(ay),z2=-x*std::sin(ay)+z*std::cos(ay);x=x1;z=z2;
    const float x2=x*std::cos(az)-y*std::sin(az),y2=x*std::sin(az)+y*std::cos(az);x=x2+px+t.x*width;y=y2+py+t.y*height;
    const float camera=2*std::max(width,height),w=1+(z+t.z*std::max(width,height))/camera;
    if(w<.02f||w>100)return {0,0,hw*w,false};
    return {(.5f*width+(x-.5f*width)/w)/width,(.5f*height+(y-.5f*height)/w)/height,hw*w,true};
}
}
