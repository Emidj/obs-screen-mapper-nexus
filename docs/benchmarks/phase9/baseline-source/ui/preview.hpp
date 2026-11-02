// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QWidget>
#include <QPointF>
#include <QImage>
#include <functional>
#include <memory>
#include <atomic>
#include "../src/zones.hpp"

struct PreviewState {
    mapping::Layout layout;
    size_t selected=0;
    size_t point=0;
    int tool=0; // select/move zone, polygon, mesh, pan
    float zoom=1;
    QPointF pan;
    bool grid=true,zones=true;
    uint32_t width=1920,height=1080;
};
class MapperPreview:public QWidget {
public:
    explicit MapperPreview(obs_source_t *draft,QWidget *parent=nullptr);
    ~MapperPreview() override;
    void setState(PreviewState state);
    void setZoom(float value);
    void fit();
    PreviewState viewState() const { return *std::atomic_load(&state); }
    void restoreView(float zoom,QPointF pan);
    QString referencePath() const { return referenceFile; }
    bool setReference(const QString &path);
    unsigned frameCount() const { return frames.load(); }
    QImage renderFrame(); // Internal GPU validation artifact, not a desktop capture.
    QPointF canvasPoint(QPointF widget) const;
    QPointF widgetPoint(QPointF normalized) const;
    std::function<void(size_t)> selectZone;
    std::function<void(size_t)> selectPoint;
    std::function<void(size_t,size_t,QPointF)> movePolygon;
    std::function<void(size_t,size_t,QPointF)> moveMesh;
    std::function<void(size_t,QPointF)> moveZone;
    std::function<void()> beginEdit,endEdit;
    std::function<void(float)> zoomChanged;
protected:
    void showEvent(QShowEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    QPaintEngine *paintEngine() const override { return nullptr; }
private:
    static void draw(void *,uint32_t,uint32_t);
    void createDisplay();
    obs_source_t *draft=nullptr;
    obs_display_t *display=nullptr;
    std::shared_ptr<const PreviewState> state;
    gs_texture_t *checker=nullptr,*reference=nullptr,*digits=nullptr;
    QPointF last;
    bool dragging=false;
    int point=-1;
    std::atomic<float> pixelRatio{1};
    std::atomic<unsigned> frames{0};
    QString referenceFile;
};
