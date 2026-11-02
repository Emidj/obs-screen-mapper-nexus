// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "i18n.hpp"
#include "polygon.hpp"

namespace mapping {
struct Offset { float x=0, y=0; };
struct Mesh {
    bool enabled=false, smooth=true, soft=false, wave=false;
    int columns=4, rows=4;
    std::vector<Offset> points=std::vector<Offset>(16);
    float horizontal=0, vertical=0, radius=.35f, strength=1;
    std::string error;
    int domain=0; // 0: original canvas, 1: cropped ellipse, 2: cropped rectangle
    float center_x=.5f,center_y=.5f,axis_x=.5f,axis_y=.5f;
};
struct Surface { std::vector<Vertex> triangles; std::string error; };
inline bool validate_mesh(Mesh &mesh)
{
    mesh.error.clear();
    if(mesh.domain<0||mesh.domain>2||!std::isfinite(mesh.center_x)||!std::isfinite(mesh.center_y)||!std::isfinite(mesh.axis_x)||!std::isfinite(mesh.axis_y)||mesh.center_x<0||mesh.center_x>1||mesh.center_y<0||mesh.center_y>1||mesh.axis_x<=0||mesh.axis_x>1||mesh.axis_y<=0||mesh.axis_y>1){mesh.error=mapper_i18n::text("Dominio de malla no válido.");return false;}
    if (mesh.columns<2 || mesh.columns>16 || mesh.rows<2 || mesh.rows>16 || mesh.points.size()!=size_t(mesh.columns*mesh.rows))
        mesh.error=mapper_i18n::text("Dimensiones o puntos de malla no válidos.");
    else if (!std::isfinite(mesh.horizontal) || !std::isfinite(mesh.vertical) || std::abs(mesh.horizontal)>.5f || std::abs(mesh.vertical)>.5f ||
        !std::isfinite(mesh.radius) || mesh.radius<0 || mesh.radius>1.5f || !std::isfinite(mesh.strength) || mesh.strength<0 || mesh.strength>1)
        mesh.error=mapper_i18n::text("Parámetros de malla no válidos.");
    else for (const auto &point:mesh.points) if (!std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x)>1 || std::abs(point.y)>1) {
        mesh.error=mapper_i18n::text("Desplazamientos de malla no válidos."); break;
    }
    return mesh.error.empty();
}
inline Offset mix(Offset a, Offset b,float t) { return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t}; }
inline Offset sample_control(const Mesh &mesh,int x,int y)
{
    // Linear extrapolation avoids flattening the tangent at the boundary.
    auto row=[&](int r) {
        auto get=[&](int c) { return mesh.points[size_t(r*mesh.columns+c)]; };
        if(x<0) return mix(get(0),get(1),float(x));
        if(x>=mesh.columns) return mix(get(mesh.columns-1),get(mesh.columns-2),float(mesh.columns-1-x));
        return get(x);
    };
    if(y<0) return mix(row(0),row(1),float(y));
    if(y>=mesh.rows) return mix(row(mesh.rows-1),row(mesh.rows-2),float(mesh.rows-1-y));
    return row(y);
}
inline Offset catmull(Offset a,Offset b,Offset c,Offset d,float t)
{
    auto curve=[&](float p0,float p1,float p2,float p3) { return .5f*((2*p1)+(-p0+p2)*t+(2*p0-5*p1+4*p2-p3)*t*t+(-p0+3*p1-3*p2+p3)*t*t*t); };
    return {curve(a.x,b.x,c.x,d.x),curve(a.y,b.y,c.y,d.y)};
}
inline Offset domain_anchor(const Mesh &m){return {(std::max(0.f,m.center_x-m.axis_x)+std::min(1.f,m.center_x+m.axis_x))*.5f,(std::max(0.f,m.center_y-m.axis_y)+std::min(1.f,m.center_y+m.axis_y))*.5f};}
inline double domain_radius(const Mesh &m,double dx,double dy){
    const auto a=domain_anchor(m);const double ox=(a.x-m.center_x)/m.axis_x,oy=(a.y-m.center_y)/m.axis_y,dot=ox*dx+oy*dy;
    double t=-dot+std::sqrt(std::max(0.,dot*dot+1-ox*ox-oy*oy));
    const double vx=m.axis_x*dx,vy=m.axis_y*dy;
    if(vx>1e-12)t=std::min(t,(1-a.x)/vx);else if(vx< -1e-12)t=std::min(t,-a.x/vx);
    if(vy>1e-12)t=std::min(t,(1-a.y)/vy);else if(vy< -1e-12)t=std::min(t,-a.y/vy);return std::max(t,1e-9);
}
inline Offset control_base(const Mesh &m,float u,float v){
    if(!m.domain)return {u,v};
    if(m.domain==2){const float l=std::max(0.f,m.center_x-m.axis_x),r=std::min(1.f,m.center_x+m.axis_x),t=std::max(0.f,m.center_y-m.axis_y),b=std::min(1.f,m.center_y+m.axis_y);return {l+(r-l)*u,t+(b-t)*v};}
    const double x=2*u-1,y=2*v-1,dx=x*std::sqrt(std::max(0.,1-y*y*.5)),dy=y*std::sqrt(std::max(0.,1-x*x*.5)),r=std::hypot(dx,dy);const auto a=domain_anchor(m);if(r<1e-12)return a;
    const double extent=domain_radius(m,dx/r,dy/r);return {float(a.x+dx*extent*m.axis_x),float(a.y+dy*extent*m.axis_y)};
}
inline Offset domain_parameter(const Mesh &m,float u,float v){
    if(!m.domain)return {u,v};
    if(m.domain==2){const float l=std::max(0.f,m.center_x-m.axis_x),r=std::min(1.f,m.center_x+m.axis_x),t=std::max(0.f,m.center_y-m.axis_y),b=std::min(1.f,m.center_y+m.axis_y);return {std::clamp((u-l)/std::max(r-l,1e-9f),0.f,1.f),std::clamp((v-t)/std::max(b-t,1e-9f),0.f,1.f)};}
    const auto a=domain_anchor(m);double x=(u-a.x)/m.axis_x,y=(v-a.y)/m.axis_y,r=std::hypot(x,y);if(r<1e-12)return {.5f,.5f};const double extent=domain_radius(m,x/r,y/r);x/=extent;y/=extent;r=std::hypot(x,y);if(r>1){x/=r;y/=r;}
    const double s=2+x*x-y*y,t=2-x*x+y*y,k=2*std::sqrt(2.);const double px=(std::sqrt(std::max(0.,s+k*x))-std::sqrt(std::max(0.,s-k*x)))*.5,py=(std::sqrt(std::max(0.,t+k*y))-std::sqrt(std::max(0.,t-k*y)))*.5;
    return {std::clamp(float((px+1)*.5),0.f,1.f),std::clamp(float((py+1)*.5),0.f,1.f)};
}
inline Offset displacement(const Mesh &mesh,float u,float v)
{
    const auto parameter=domain_parameter(mesh,u,v);u=parameter.x;v=parameter.y;
    const float x=u*float(mesh.columns-1), y=v*float(mesh.rows-1);
    const int ix=std::min(int(x),mesh.columns-2), iy=std::min(int(y),mesh.rows-2);
    const float tx=x-float(ix),ty=y-float(iy);
    if(!mesh.smooth) return mix(mix(sample_control(mesh,ix,iy),sample_control(mesh,ix+1,iy),tx),
        mix(sample_control(mesh,ix,iy+1),sample_control(mesh,ix+1,iy+1),tx),ty);
    Offset rows[4];
    for(int r=0;r<4;++r) rows[r]=catmull(sample_control(mesh,ix-1,iy+r-1),sample_control(mesh,ix,iy+r-1),
        sample_control(mesh,ix+1,iy+r-1),sample_control(mesh,ix+2,iy+r-1),tx);
    return catmull(rows[0],rows[1],rows[2],rows[3],ty);
}
inline Vertex deform(const Mesh &mesh,Vertex p)
{
    p.mask_x=p.x; p.mask_y=p.y;
    const auto offset=displacement(mesh,p.x,p.y);
    auto profile=[&](float t) { return mesh.wave ? std::sin(6.28318530718f*t) : 4*t*(1-t); };
    const float dx=mesh.vertical*profile(p.y),dy=mesh.horizontal*profile(p.x);
    p.x+=offset.x+dx; p.y+=offset.y+dy;
    return p;
}
inline void resize_mesh(Mesh &mesh,int columns,int rows)
{
    columns=std::clamp(columns,2,16); rows=std::clamp(rows,2,16);
    if(mesh.columns==columns && mesh.rows==rows) return;
    std::vector<Offset> points;
    const bool valid=validate_mesh(mesh);
    for(int y=0;y<rows;++y) for(int x=0;x<columns;++x) {
        const auto base=control_base(mesh,float(x)/float(columns-1),float(y)/float(rows-1));auto point=valid ? displacement(mesh,base.x,base.y) : Offset{};
        point.x=std::clamp(point.x,-1.0f,1.0f); point.y=std::clamp(point.y,-1.0f,1.0f);
        points.push_back(point);
    }
    mesh.columns=columns; mesh.rows=rows; mesh.points=std::move(points); validate_mesh(mesh);
}
inline void move_control(Mesh &mesh,size_t selected,float dx,float dy)
{
    if(!validate_mesh(mesh) || selected>=mesh.points.size() || !std::isfinite(dx) || !std::isfinite(dy)) return;
    const auto base=control_base(mesh,float(selected%size_t(mesh.columns))/float(mesh.columns-1),float(selected/size_t(mesh.columns))/float(mesh.rows-1));const float x=base.x,y=base.y;
    for(size_t i=0;i<mesh.points.size();++i) {
        float weight=i==selected ? 1.0f:0.0f;
        if(mesh.soft && mesh.radius>0) {
            const auto current=control_base(mesh,float(i%size_t(mesh.columns))/float(mesh.columns-1),float(i/size_t(mesh.columns))/float(mesh.rows-1));const float distance=std::hypot(current.x-x,current.y-y);
            const float t=std::clamp(distance/mesh.radius,0.0f,1.0f);
            weight=(1-3*t*t+2*t*t*t)*mesh.strength;
        } else if(mesh.soft) weight*=mesh.strength;
        mesh.points[i].x=std::clamp(mesh.points[i].x+dx*weight,-1.0f,1.0f);
        mesh.points[i].y=std::clamp(mesh.points[i].y+dy*weight,-1.0f,1.0f);
    }
}
inline Surface compile_surface(Mesh &mesh,const Polygon &polygon)
{
    Surface result;
    if(!mesh.enabled) return result;
    if(!validate_mesh(mesh)) { result.error=mesh.error; return result; }
    Polygon base=polygon;
    if(!base.enabled) { base=Polygon{}; base.enabled=true; }
    compile_polygon(base);
    if(!base.error.empty()) { result.error=base.error; return result; }
    const int subdivisions=std::clamp(std::max(mesh.columns-1,mesh.rows-1)*4,8,32);
    for(size_t triangle=0;triangle<base.triangles.size();triangle+=3) {
        const auto a=base.vertices[base.triangles[triangle]], b=base.vertices[base.triangles[triangle+1]], c=base.vertices[base.triangles[triangle+2]];
        const double orientation=cross(a,b,c)>0?1:-1;
        auto sample=[&](int i,int j) {
            const float s=float(i)/subdivisions,t=float(j)/subdivisions;
            return deform(mesh,{a.x+(b.x-a.x)*s+(c.x-a.x)*t,a.y+(b.y-a.y)*s+(c.y-a.y)*t,
                a.u+(b.u-a.u)*s+(c.u-a.u)*t,a.v+(b.v-a.v)*s+(c.v-a.v)*t});
        };
        auto emit_triangle=[&](Vertex p,Vertex q,Vertex r) {
            if(orientation*cross(p,q,r)<=1e-10) return false;
            result.triangles.insert(result.triangles.end(),{p,q,r}); return true;
        };
        for(int i=0;i<subdivisions;++i) for(int j=0;j<subdivisions-i;++j) {
            const auto p=sample(i,j),q=sample(i+1,j),r=sample(i,j+1);
            if(!emit_triangle(p,q,r) || (i+j<subdivisions-1 && !emit_triangle(q,sample(i+1,j+1),r))) {
                result.triangles.clear(); result.error=mapper_i18n::text("La deformación invierte o colapsa caras. Reduce el desplazamiento o la curvatura.");
                return result;
            }
        }
    }
    return result;
}
}

