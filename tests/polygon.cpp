// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/polygon.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
static void check(bool ok,const char *label) { std::printf("%s: %s\n",ok?"PASS":"FAIL",label); if(!ok) std::exit(1); }
int main()
{
    using namespace mapping;
    Polygon polygon; polygon.enabled=true;
    compile_polygon(polygon); check(polygon.error.empty() && polygon.triangles.size()==6,"rectangle triangulates");
    std::reverse(polygon.vertices.begin(),polygon.vertices.end()); compile_polygon(polygon);
    check(polygon.error.empty() && polygon.triangles.size()==6,"reverse winding triangulates");
    polygon.vertices={{0,0,0,0},{1,0,1,0},{1,.375f,1,.375f},{.375f,.375f,.375f,.375f},{.375f,1,.375f,1},{0,1,0,1}};
    compile_polygon(polygon);
    check(polygon.error.empty() && polygon.triangles.size()==12,"concave contour triangulates");
    double area=0; for(size_t i=0;i<polygon.triangles.size();i+=3)
        area+=std::abs(cross(polygon.vertices[polygon.triangles[i]],polygon.vertices[polygon.triangles[i+1]],polygon.vertices[polygon.triangles[i+2]]))/2;
    check(std::abs(area-0.609375)<1e-8,"triangle areas cover concave polygon exactly");
    polygon.vertices={{0,0,0,0},{.5f,0,.5f,0},{1,0,1,0},{1,1,1,1},{0,1,0,1}}; compile_polygon(polygon);
    check(polygon.error.empty() && polygon.triangles.size()==9,"inserted collinear edge point remains valid");
    polygon.vertices={{0,0,0,0},{1,1,1,1},{1,0,1,0},{0,1,0,1}}; compile_polygon(polygon);
    check(!polygon.error.empty() && polygon.triangles.empty(),"crossing contour rejected");
    polygon.vertices={{0,0,0,0},{1,0,1,0},{1,0,1,0},{0,1,0,1}}; compile_polygon(polygon);
    check(!polygon.error.empty(),"duplicate consecutive vertex rejected");
    polygon.vertices={{0,0,0,0},{.5f,.5f,.5f,.5f},{1,1,1,1}}; compile_polygon(polygon);
    check(!polygon.error.empty(),"zero-area contour rejected");
    polygon.vertices={{0,0,0,0},{1,0,1,0}}; compile_polygon(polygon);
    check(!polygon.error.empty(),"fewer than three vertices rejected");
    polygon=Polygon{}; polygon.enabled=true; polygon.vertices[0].x=std::numeric_limits<float>::quiet_NaN(); compile_polygon(polygon);
    check(!polygon.error.empty(),"nonfinite coordinates rejected");
    polygon=Polygon{}; polygon.enabled=true; polygon.vertices[0].u=2; compile_polygon(polygon);
    check(!polygon.error.empty(),"out-of-range UV rejected");
    polygon.enabled=false; compile_polygon(polygon); check(polygon.triangles.empty() && polygon.error.empty(),"disabled polygon does not triangulate");
    std::puts("PASS: polygon geometry validation complete");
}
