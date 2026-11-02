// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "i18n.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

namespace mapping {
struct Vertex { float x, y, u, v; float mask_x=0,mask_y=0; };
struct Polygon {
    bool enabled = false;
    std::vector<Vertex> vertices{{0,0,0,0}, {1,0,1,0}, {1,1,1,1}, {0,1,0,1}};
    std::vector<size_t> triangles;
    std::string error;
};
inline double cross(const Vertex &a, const Vertex &b, const Vertex &c)
{ return (double(b.x)-a.x)*(double(c.y)-a.y) - (double(b.y)-a.y)*(double(c.x)-a.x); }
inline bool on_segment(const Vertex &a, const Vertex &b, const Vertex &p)
{
    return std::abs(cross(a,b,p)) <= 1e-8 && p.x >= std::min(a.x,b.x)-1e-8 && p.x <= std::max(a.x,b.x)+1e-8 &&
        p.y >= std::min(a.y,b.y)-1e-8 && p.y <= std::max(a.y,b.y)+1e-8;
}
inline bool intersects(const Vertex &a, const Vertex &b, const Vertex &c, const Vertex &d)
{
    return (cross(a,b,c)*cross(a,b,d) < 0 && cross(c,d,a)*cross(c,d,b) < 0) ||
        on_segment(a,b,c) || on_segment(a,b,d) || on_segment(c,d,a) || on_segment(c,d,b);
}
inline void compile_polygon(Polygon &polygon)
{
    polygon.triangles.clear(); polygon.error.clear();
    if (!polygon.enabled) return;
    const auto &p = polygon.vertices;
    auto fail = [&](const char *message) { polygon.triangles.clear(); polygon.error = message; };
    if (p.size() < 3) { fail(mapper_i18n::text("Se necesitan al menos tres vértices.")); return; }
    for (const auto &v : p) if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.u) || !std::isfinite(v.v) ||
        v.x < 0 || v.x > 1 || v.y < 0 || v.y > 1 || v.u < 0 || v.u > 1 || v.v < 0 || v.v > 1) {
        fail(mapper_i18n::text("Coordenadas no válidas; usa valores finitos entre 0 y 1.")); return;
    }
    for (size_t i = 0; i < p.size(); ++i) {
        const auto &a = p[i], &b = p[(i+1)%p.size()];
        if (std::hypot(double(a.x)-b.x, double(a.y)-b.y) <= 1e-8) { fail(mapper_i18n::text("Hay vértices consecutivos coincidentes.")); return; }
        for (size_t j = i+1; j < p.size(); ++j) {
            if (j == i+1 || (i == 0 && j+1 == p.size())) continue;
            if (intersects(a,b,p[j],p[(j+1)%p.size()])) { fail(mapper_i18n::text("El contorno se cruza o se toca a sí mismo.")); return; }
        }
    }
    double area = 0;
    for (size_t i = 0; i < p.size(); ++i) area += double(p[i].x)*p[(i+1)%p.size()].y - double(p[i].y)*p[(i+1)%p.size()].x;
    if (std::abs(area) <= 1e-8) { fail(mapper_i18n::text("El polígono no tiene área suficiente.")); return; }
    const double winding = area > 0 ? 1 : -1;
    std::vector<size_t> remaining(p.size()); std::iota(remaining.begin(), remaining.end(), 0);
    while (remaining.size() > 3) {
        bool found = false;
        for (size_t i = 0; i < remaining.size(); ++i) {
            const size_t a = remaining[(i+remaining.size()-1)%remaining.size()], b = remaining[i], c = remaining[(i+1)%remaining.size()];
            if (winding*cross(p[a],p[b],p[c]) <= 1e-8) continue;
            bool contains = false;
            for (size_t j : remaining) if (j != a && j != b && j != c &&
                winding*cross(p[a],p[b],p[j]) >= -1e-8 && winding*cross(p[b],p[c],p[j]) >= -1e-8 && winding*cross(p[c],p[a],p[j]) >= -1e-8) { contains = true; break; }
            if (contains) continue;
            polygon.triangles.insert(polygon.triangles.end(), {a,b,c});
            remaining.erase(remaining.begin()+i); found = true; break;
        }
        if (!found) { fail(mapper_i18n::text("No se puede triangular este contorno degenerado.")); return; }
    }
    if (winding*cross(p[remaining[0]],p[remaining[1]],p[remaining[2]]) <= 1e-8) { fail(mapper_i18n::text("Hay una cara degenerada.")); return; }
    polygon.triangles.insert(polygon.triangles.end(), remaining.begin(), remaining.end());
}
}

