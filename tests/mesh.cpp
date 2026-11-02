// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/mesh.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
static void check(bool ok,const char *label) { std::printf("%s: %s\n",ok?"PASS":"FAIL",label); if(!ok) std::exit(1); }
static bool near(float a,float b) { return std::abs(a-b)<1e-5f; }
int main()
{
    using namespace mapping;
    Mesh mesh; mesh.enabled=true;
    auto point=deform(mesh,{.37f,.62f,.2f,.8f});
    check(near(point.x,.37f)&&near(point.y,.62f)&&near(point.u,.2f)&&near(point.v,.8f),"neutral mesh preserves position and UV");
    mesh.points[5]={.12f,-.08f}; auto at_control=displacement(mesh,1.0f/3,1.0f/3);
    check(near(at_control.x,.12f)&&near(at_control.y,-.08f),"bicubic field interpolates its control points");
    const auto smooth_sample=displacement(mesh,.5f,.5f); mesh.smooth=false;
    const auto linear_sample=displacement(mesh,.5f,.5f);
    check(std::abs(smooth_sample.x-linear_sample.x)>.001f,"smooth interpolation differs from bilinear interpolation for nonaffine edits");
    mesh=Mesh{}; mesh.enabled=true;
    for(int y=0;y<4;++y) for(int x=0;x<4;++x) mesh.points[size_t(y*4+x)]={float(x)*.05f,float(y)*.03f};
    auto affine=displacement(mesh,.23f,.77f);
    check(near(affine.x,.23f*.15f)&&near(affine.y,.77f*.09f),"smooth field preserves affine displacements including edge tangents");
    resize_mesh(mesh,7,5); auto resampled=displacement(mesh,.23f,.77f);
    check(near(affine.x,resampled.x)&&near(affine.y,resampled.y),"resizing grid preserves affine deformation");
    mesh=Mesh{}; mesh.enabled=true; mesh.soft=true; mesh.radius=1; mesh.strength=.5f;
    move_control(mesh,5,.2f,0);
    check(near(mesh.points[5].x,.1f)&&mesh.points[6].x>0&&mesh.points[6].x<.1f,"soft selection weights selected and nearby controls");
    const float t=1.0f/3; check(near(mesh.points[6].x,.1f*(1-3*t*t+2*t*t*t)),"soft selection uses smoothstep distance falloff");
    mesh=Mesh{}; mesh.soft=true; mesh.radius=.2f; move_control(mesh,5,.1f,0);
    check(near(mesh.points[5].x,.1f)&&near(mesh.points[6].x,0),"controls outside soft radius remain unchanged");
    mesh=Mesh{}; mesh.soft=true; mesh.strength=0; move_control(mesh,5,.1f,.1f);
    check(near(mesh.points[5].x,0)&&near(mesh.points[5].y,0),"zero strength has no effect");
    mesh=Mesh{}; mesh.soft=true; mesh.radius=0; move_control(mesh,5,.1f,0);
    check(near(mesh.points[5].x,.1f)&&near(mesh.points[6].x,0),"zero radius moves selected control only");
    mesh=Mesh{}; mesh.enabled=true; mesh.horizontal=.2f; mesh.vertical=-.1f;
    auto curved=deform(mesh,{.5f,.5f,.2f,.3f});
    check(near(curved.x,.4f)&&near(curved.y,.7f)&&near(curved.u,.2f),"quick curvature adds independent horizontal and vertical offsets");
    mesh.wave=true; auto waved=deform(mesh,{.25f,0,0,0}); check(near(waved.y,.2f),"wave profile produces expected peak");
    mesh=Mesh{}; mesh.enabled=true;
    Polygon polygon; auto surface=compile_surface(mesh,polygon);
    check(surface.error.empty()&&!surface.triangles.empty(),"neutral rectangular surface tessellates");
    double area=0; for(size_t i=0;i<surface.triangles.size();i+=3) area+=cross(surface.triangles[i],surface.triangles[i+1],surface.triangles[i+2])/2;
    check(std::abs(area-1)<1e-5,"tessellation covers unit rectangle exactly");
    polygon.enabled=true; polygon.vertices={{0,0,0,0},{1,0,1,0},{0,1,0,1}}; mesh.horizontal=.15f;
    surface=compile_surface(mesh,polygon);
    check(surface.error.empty()&&!surface.triangles.empty(),"Polygon and curvature compile together");
    mesh=Mesh{}; mesh.enabled=true; resize_mesh(mesh,2,2); mesh.points[1].x=-1; mesh.points[3].x=-1;
    surface=compile_surface(mesh,Polygon{});
    check(!surface.error.empty()&&surface.triangles.empty(),"collapsed faces rejected");
    mesh=Mesh{}; mesh.enabled=true; mesh.points.clear(); surface=compile_surface(mesh,Polygon{});
    check(!surface.error.empty(),"inconsistent control count rejected");
    mesh=Mesh{}; mesh.enabled=true; mesh.points[0].x=std::numeric_limits<float>::quiet_NaN();
    check(!compile_surface(mesh,Polygon{}).error.empty(),"nonfinite control rejected");
    std::puts("PASS: mesh geometry validation complete");
}
