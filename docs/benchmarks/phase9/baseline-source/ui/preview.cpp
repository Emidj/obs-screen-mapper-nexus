// SPDX-License-Identifier: GPL-2.0-or-later
#include "preview.hpp"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QLineF>
#include <QWindow>
#include <QTimer>
#include <QPolygonF>
#include <graphics/vec4.h>

namespace {
struct View { float scale,x,y; };
View view(const PreviewState &s,float width,float height) {
    const float scale=std::min(width/float(s.width),height/float(s.height))*.92f*s.zoom;
    return {scale,(width-float(s.width)*scale)/2+float(s.pan.x()),(height-float(s.height)*scale)/2+float(s.pan.y())};
}
void color(float r,float g,float b,float a=1) {
    vec4 value; vec4_set(&value,r*a,g*a,b*a,a);
    auto *effect=obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_effect_set_vec4(gs_effect_get_param_by_name(effect,"color"),&value);
}
void box(float x,float y,float w,float h,float r,float g,float b,float alpha=1) {
    color(r,g,b,alpha); auto *effect=obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_matrix_push(); gs_matrix_translate3f(x,y,0);
    while(gs_effect_loop(effect,"Solid")) gs_draw_sprite(nullptr,0,uint32_t(std::max(1.0f,w)),uint32_t(std::max(1.0f,h)));
    gs_matrix_pop();
}
void line(const std::vector<QPointF> &points,float r,float g,float b) {
    color(r,g,b); auto *effect=obs_get_base_effect(OBS_EFFECT_SOLID);
    while(gs_effect_loop(effect,"Solid")) {
        gs_render_start(true); for(auto p:points) gs_vertex2f(float(p.x()),float(p.y())); gs_render_stop(GS_LINESTRIP);
    }
}
QPointF localPoint(const mapping::Zone &zone,mapping::Vertex p) {
    if(zone.mesh.enabled && zone.mesh.error.empty()) p=mapping::deform(zone.mesh,p);
    return {zone.destination.x+p.x*zone.destination.w,zone.destination.y+p.y*zone.destination.h};
}
}
MapperPreview::MapperPreview(obs_source_t *source,QWidget *parent):QWidget(parent),draft(obs_source_get_ref(source)) {
    setObjectName("mapperPreview"); setAttribute(Qt::WA_NativeWindow); setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_StaticContents);setAttribute(Qt::WA_NoSystemBackground);setAttribute(Qt::WA_OpaquePaintEvent);setAttribute(Qt::WA_DontCreateNativeAncestors);
    winId();connect(windowHandle(),&QWindow::visibleChanged,this,[this](bool visible){if(visible)QTimer::singleShot(0,this,[this]{createDisplay();});});
    setMinimumSize(320,240); setMouseTracking(true); setFocusPolicy(Qt::StrongFocus);
    state=std::make_shared<const PreviewState>();
    QImage atlas(320,48,QImage::Format_RGBA8888_Premultiplied);atlas.fill(Qt::transparent);
    {QPainter painter(&atlas);painter.setRenderHint(QPainter::TextAntialiasing);painter.setPen(Qt::white);painter.setFont(QFont("Segoe UI",25,QFont::Bold));for(int digit=0;digit<10;++digit)painter.drawText(QRect(digit*32,0,32,48),Qt::AlignCenter,QString::number(digit));}
    obs_enter_graphics();const uint8_t *pixels=atlas.constBits();digits=gs_texture_create(320,48,GS_RGBA,1,&pixels,0);obs_leave_graphics();
    obs_source_inc_showing(draft);
}
MapperPreview::~MapperPreview() {
    if(display) { obs_display_remove_draw_callback(display,draw,this); obs_display_destroy(display); }
    if(checker || reference || digits) { obs_enter_graphics(); gs_texture_destroy(checker); gs_texture_destroy(reference);gs_texture_destroy(digits); obs_leave_graphics(); }
    obs_source_dec_showing(draft); obs_source_release(draft);
}
void MapperPreview::createDisplay() {
    if(display || !isVisible()) return;
    gs_init_data graphics{}; graphics.cx=uint32_t(width()*devicePixelRatioF()); graphics.cy=uint32_t(height()*devicePixelRatioF());
    pixelRatio.store(float(devicePixelRatioF()));
    graphics.format=GS_BGRA; graphics.zsformat=GS_ZS_NONE; graphics.window.hwnd=reinterpret_cast<HWND>(winId());
    display=obs_display_create(&graphics,0xFF130D09);
    if(display) obs_display_add_draw_callback(display,draw,this);
}
void MapperPreview::showEvent(QShowEvent *) { QTimer::singleShot(0,this,[this]{createDisplay();}); }
void MapperPreview::resizeEvent(QResizeEvent *) { pixelRatio.store(float(devicePixelRatioF())); if(display) obs_display_resize(display,uint32_t(width()*devicePixelRatioF()),uint32_t(height()*devicePixelRatioF())); }
void MapperPreview::paintEvent(QPaintEvent *) {}
void MapperPreview::setState(PreviewState next) {
    auto previous=std::atomic_load(&state); next.zoom=previous->zoom; next.pan=previous->pan;
    std::atomic_store(&state,std::make_shared<const PreviewState>(std::move(next)));
}
void MapperPreview::setZoom(float value) { auto next=*std::atomic_load(&state); next.zoom=std::clamp(value,.2f,8.0f); std::atomic_store(&state,std::make_shared<const PreviewState>(std::move(next))); if(zoomChanged) zoomChanged(std::atomic_load(&state)->zoom); }
void MapperPreview::fit() { auto next=*std::atomic_load(&state); next.zoom=1; next.pan={}; std::atomic_store(&state,std::make_shared<const PreviewState>(std::move(next))); if(zoomChanged) zoomChanged(1); }
void MapperPreview::restoreView(float zoom,QPointF pan) { auto next=*std::atomic_load(&state);next.zoom=std::clamp(zoom,.2f,8.0f);next.pan=pan;std::atomic_store(&state,std::make_shared<const PreviewState>(std::move(next)));if(zoomChanged)zoomChanged(std::atomic_load(&state)->zoom); }
bool MapperPreview::setReference(const QString &path) {
    QImage image; if(!path.isEmpty() && !image.load(path)) return false;
    image=image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    obs_enter_graphics();
    gs_texture_destroy(reference); reference=nullptr;
    if(!image.isNull()) { const uint8_t *pixels=image.constBits(); reference=gs_texture_create(uint32_t(image.width()),uint32_t(image.height()),GS_RGBA,1,&pixels,0); }
    obs_leave_graphics();referenceFile=path; return true;
}
QPointF MapperPreview::canvasPoint(QPointF p) const {
    auto s=std::atomic_load(&state); auto v=view(*s,float(width()),float(height()));
    return {(p.x()-v.x)/(v.scale*s->width),(p.y()-v.y)/(v.scale*s->height)};
}
QPointF MapperPreview::widgetPoint(QPointF normalized) const {
    auto s=std::atomic_load(&state); auto v=view(*s,float(width()),float(height()));
    return {v.x+normalized.x()*v.scale*s->width,v.y+normalized.y()*v.scale*s->height};
}
void MapperPreview::draw(void *opaque,uint32_t width,uint32_t height) {
    auto *self=static_cast<MapperPreview*>(opaque); auto s=std::atomic_load(&self->state);
    ++self->frames;
    // Convert pan from Qt logical pixels; derive ratio from actual display dimensions.
    const float ratio=self->pixelRatio.load();
    auto v=view(*s,float(width)/ratio,float(height)/ratio); v.scale*=ratio;v.x*=ratio;v.y*=ratio;
    gs_matrix_push();gs_matrix_identity();
    gs_ortho(0,float(width),0,float(height),-100,100);
    gs_blend_state_push(); gs_blend_function(GS_BLEND_ONE,GS_BLEND_INVSRCALPHA);
    gs_matrix_push(); gs_matrix_translate3f(v.x,v.y,0); gs_matrix_scale3f(v.scale,v.scale,1);
    if(!self->checker) {
        std::vector<uint8_t> pixels(256*144*4);
        for(int y=0;y<144;++y) for(int x=0;x<256;++x) { const uint8_t c=((x/8+y/8)%2)?25:35; auto *p=pixels.data()+size_t(y*256+x)*4; p[0]=c;p[1]=c;p[2]=c+5;p[3]=255; }
        const uint8_t *p=pixels.data(); self->checker=gs_texture_create(256,144,GS_RGBA,1,&p,0);
    }
    auto draw_texture=[&](gs_texture_t *texture) {
        auto *effect=obs_get_base_effect(OBS_EFFECT_DEFAULT); gs_effect_set_texture(gs_effect_get_param_by_name(effect,"image"),texture);
        while(gs_effect_loop(effect,"Draw")) gs_draw_sprite(texture,0,s->width,s->height);
    };
    if(self->checker) draw_texture(self->checker);
    if(self->reference) draw_texture(self->reference);
    obs_source_video_render(self->draft);
    auto pixel=[&](QPointF p) { return QPointF(p.x()*s->width,p.y()*s->height); };
    if(s->grid) for(int i=1;i<8;++i) {
        line({{float(s->width)*i/8,0},{float(s->width)*i/8,float(s->height)}},.10f,.19f,.25f);
        line({{0,float(s->height)*i/8},{float(s->width),float(s->height)*i/8}},.10f,.19f,.25f);
    }
    if(s->zones) for(size_t z=0;z<s->layout.zones.size();++z) {
        const auto &zone=s->layout.zones[z]; if(!zone.visible) continue;
        const bool selected=z==s->selected;
        std::vector<mapping::Vertex> boundary=zone.polygon.enabled?zone.polygon.vertices:mapping::Polygon{}.vertices;
        std::vector<QPointF> outline;
        for(size_t i=0;i<boundary.size();++i) {
            auto a=boundary[i],b=boundary[(i+1)%boundary.size()];
            const int count=zone.mesh.enabled?16:1;
            for(int j=0;j<count;++j) { const float t=float(j)/count; outline.push_back(pixel(localPoint(zone,{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,0,0}))); }
        }
        if(!outline.empty()) outline.push_back(outline.front());
        line(outline,selected?.1f:.4f,selected?.85f:.5f,selected?1.0f:.65f);
        if(self->digits) {
            const std::string label=std::to_string(z+1);const auto center=pixel(localPoint(zone,{.5f,.5f,0,0}));const float scale=20/(48*v.scale);
            box(float(center.x())-float(label.size()*32)*scale/2-5/v.scale,float(center.y())-14/v.scale,float(label.size()*32)*scale+10/v.scale,28/v.scale,selected?0.0f:.02f,selected?.3f:.06f,selected?.55f:.1f,.9f);
            auto *effect=obs_get_base_effect(OBS_EFFECT_DEFAULT);gs_effect_set_texture(gs_effect_get_param_by_name(effect,"image"),self->digits);
            gs_matrix_push();gs_matrix_translate3f(float(center.x())-float(label.size()*32)*scale/2,float(center.y())-24*scale,0);gs_matrix_scale3f(scale,scale,1);
            for(size_t index=0;index<label.size();++index) {gs_matrix_push();gs_matrix_translate3f(float(index*32),0,0);while(gs_effect_loop(effect,"Draw"))gs_draw_sprite_subregion(self->digits,0,uint32_t(label[index]-'0')*32,0,32,48);gs_matrix_pop();}
            gs_matrix_pop();
        }
        if(selected && s->tool==1 && zone.polygon.enabled) for(size_t index=0;index<boundary.size();++index) { auto q=pixel(localPoint(zone,boundary[index])); const bool active=index==s->point;const float size=(active?10:7)/v.scale; box(float(q.x())-size/2,float(q.y())-size/2,size,size,active?1.0f:.1f,active?.7f:.9f,active?.2f:1.0f); }
        if(selected && s->tool==2 && zone.mesh.enabled && zone.mesh.error.empty()) {
            const auto &mesh=zone.mesh;
            for(int y=0;y<mesh.rows;++y) {
                std::vector<QPointF> row;
                for(int x=0;x<mesh.columns;++x) row.push_back(pixel(localPoint(zone,{float(x)/(mesh.columns-1),float(y)/(mesh.rows-1),0,0})));
                line(row,.1f,.65f,.9f);
            }
            for(int x=0;x<mesh.columns;++x) {
                std::vector<QPointF> column;
                for(int y=0;y<mesh.rows;++y) column.push_back(pixel(localPoint(zone,{float(x)/(mesh.columns-1),float(y)/(mesh.rows-1),0,0})));
                line(column,.1f,.65f,.9f);
            }
            for(int y=0;y<mesh.rows;++y) for(int x=0;x<mesh.columns;++x) {
                auto q=pixel(localPoint(zone,{float(x)/(mesh.columns-1),float(y)/(mesh.rows-1),0,0}));const bool active=size_t(y*mesh.columns+x)==s->point; const float size=(active?10:6)/v.scale;
                box(float(q.x())-size/2,float(q.y())-size/2,size,size,active?1.0f:.2f,active?.7f:.9f,active?.2f:1.0f);
            }
        }
    }
    gs_matrix_pop(); gs_blend_state_pop();gs_matrix_pop();
}
QImage MapperPreview::renderFrame() {
    const uint32_t w=uint32_t(width()*devicePixelRatioF()),h=uint32_t(height()*devicePixelRatioF());
    QImage image;
    obs_enter_graphics();
    auto *target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);auto *stage=gs_stagesurface_create(w,h,GS_RGBA);
    if(target&&stage&&gs_texrender_begin(target,w,h)) {
        vec4 clear;vec4_set(&clear,.035f,.075f,.11f,1);gs_clear(GS_CLEAR_COLOR,&clear,0,0);draw(this,w,h);gs_texrender_end(target);
        gs_stage_texture(stage,gs_texrender_get_texture(target));uint8_t *pixels=nullptr;uint32_t stride=0;
        if(gs_stagesurface_map(stage,&pixels,&stride)) {image=QImage(pixels,int(w),int(h),int(stride),QImage::Format_RGBA8888_Premultiplied).copy();gs_stagesurface_unmap(stage);}
    }
    gs_stagesurface_destroy(stage);gs_texrender_destroy(target);obs_leave_graphics();return image;
}
void MapperPreview::mousePressEvent(QMouseEvent *event) {
    if(event->button()!=Qt::LeftButton && event->button()!=Qt::MiddleButton) return;
    setFocus(); last=event->position(); auto s=std::atomic_load(&state); point=-1;
    if(s->tool==3 || event->button()==Qt::MiddleButton) { dragging=true; grabMouse(); return; }
    if(s->selected<s->layout.zones.size()) {
        const auto &zone=s->layout.zones[s->selected];
        float best=14;
        if(s->tool==1 && zone.polygon.enabled) for(size_t i=0;i<zone.polygon.vertices.size();++i) {
            const auto p=widgetPoint(localPoint(zone,zone.polygon.vertices[i])); const float distance=float(QLineF(p,last).length()); if(distance<best) { best=distance; point=int(i); }
        }
        if(s->tool==2 && zone.mesh.enabled && zone.mesh.error.empty()) for(size_t i=0;i<zone.mesh.points.size();++i) {
            const auto p=widgetPoint(localPoint(zone,{float(i%size_t(zone.mesh.columns))/(zone.mesh.columns-1),float(i/size_t(zone.mesh.columns))/(zone.mesh.rows-1),0,0}));
            const float distance=float(QLineF(p,last).length()); if(distance<best) { best=distance; point=int(i); }
        }
    }
    if(point<0) {
        const auto p=canvasPoint(last);
        for(size_t i=s->layout.zones.size();i>0;--i) { const auto &z=s->layout.zones[i-1];
            QPolygonF outline;const auto boundary=z.polygon.enabled?z.polygon.vertices:mapping::Polygon{}.vertices;
            for(size_t j=0;j<boundary.size();++j){auto a=boundary[j],b=boundary[(j+1)%boundary.size()];const int count=z.mesh.enabled?16:1;
                for(int step=0;step<count;++step){const float t=float(step)/count;outline<<localPoint(z,{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,0,0});}}
            if(z.visible && outline.containsPoint(p,Qt::OddEvenFill)) { if(selectZone) selectZone(i-1); break; }
        }
    }
    else if(selectPoint) selectPoint(size_t(point));
    dragging=true; grabMouse(); if(beginEdit) beginEdit();
}
void MapperPreview::mouseMoveEvent(QMouseEvent *event) {
    if(!dragging) return;
    auto s=std::atomic_load(&state); const auto current=event->position();
    if(s->tool==3 || (event->buttons()&Qt::MiddleButton)) { auto next=*s; next.pan+=current-last; std::atomic_store(&state,std::make_shared<const PreviewState>(std::move(next))); }
    else if(s->selected<s->layout.zones.size()) {
        const auto delta=canvasPoint(current)-canvasPoint(last); const auto d=s->layout.zones[s->selected].destination;
        if(point>=0 && d.w>0 && d.h>0) {
            const QPointF local(delta.x()/d.w,delta.y()/d.h);
            if(s->tool==1 && movePolygon) movePolygon(s->selected,size_t(point),local);
            if(s->tool==2 && moveMesh) moveMesh(s->selected,size_t(point),local);
        } else if(s->tool==0 && moveZone) moveZone(s->selected,delta);
    }
    last=current;
}
void MapperPreview::mouseReleaseEvent(QMouseEvent *) { if(dragging) { dragging=false; releaseMouse(); if(endEdit) endEdit(); } }
void MapperPreview::wheelEvent(QWheelEvent *event) { auto s=std::atomic_load(&state); setZoom(s->zoom*(event->angleDelta().y()>0?1.15f:1/1.15f)); event->accept(); }
