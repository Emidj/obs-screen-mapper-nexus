// SPDX-License-Identifier: GPL-2.0-or-later
#include "presets.hpp"
#include "../src/zones.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QImage>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
static void require(bool ok,const QString &error) {if(!ok){std::fprintf(stderr,"FAIL: %s\n",error.toUtf8().constData());std::exit(1);}}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);require(argc==3,"usage: preset-benchmark TEMP_ROOT CSV");
    QDir().mkpath(QString::fromLocal8Bit(argv[1]));QTemporaryDir temporary(QString::fromLocal8Bit(argv[1])+"/benchmark-XXXXXX");require(temporary.isValid(),"temporary directory");
    QImage image(512,512,QImage::Format_RGBA8888);
    for(int y=0;y<512;++y) for(int x=0;x<512;++x) image.setPixelColor(x,y,QColor((x*13+y*7)%256,(x*3+y*17)%256,(x+y*11)%256,(x+y)%256));
    const auto asset=temporary.filePath("mask.png");require(image.save(asset),"PNG asset");
    std::ofstream report(argv[2]);require(bool(report),"CSV output");report<<"case,zones,library_entries,samples,bytes,p50_ms,p95_ms\n";
    for(int count:{24,96}) {
        auto *settings=obs_data_create();obs_data_set_int(settings,"canvas_width",1920);obs_data_set_int(settings,"canvas_height",1080);
        obs_data_set_string(settings,"distribution","split");
        mapping::Layout layout;
        for(int i=0;i<count;++i) {mapping::Zone zone;zone.id=std::to_string(i+1);zone.name="Zona "+zone.id;zone.mesh.enabled=true;zone.mesh.columns=8;zone.mesh.rows=6;zone.mesh.points.resize(48);zone.mask.kind=3;zone.mask.path=asset.toUtf8().toStdString();layout.zones.push_back(std::move(zone));}
        mapping::write(settings,layout);QString error;const auto bytes=MapperPresets::encode(settings,"Benchmark",error);require(!bytes.isEmpty(),error);
        MapperPresets store(temporary.filePath(QString("library-%1").arg(count)));QJsonObject result;QString name;
        require(store.materialize(bytes,result,name,error),error);
        QString last;
        for(int i=0;i<100;++i) {last=store.save(bytes,{},error);require(!last.isEmpty(),error);}
        auto measure=[&](const char *label,auto operation) {
            std::vector<double> times;
            for(int i=0;i<33;++i) {QElapsedTimer timer;timer.start();operation();const double elapsed=double(timer.nsecsElapsed())/1e6;if(i>=3)times.push_back(elapsed);}
            std::sort(times.begin(),times.end());report<<label<<','<<count<<",100,30,"<<bytes.size()<<','<<times[14]<<','<<times[28]<<'\n';
            std::printf("BENCH %s zones=%d p95=%.3f ms\n",label,count,times[28]);std::fflush(stdout);
        };
        measure("encode",[&]{require(!MapperPresets::encode(settings,"Benchmark",error).isEmpty(),error);});
        measure("validate",[&]{require(MapperPresets::validate(bytes,result,error),error);});
        measure("materialize_cached",[&]{require(store.materialize(bytes,result,name,error),error);});
        measure("library_list",[&]{require(store.list(error).size()==100,error);});
        measure("library_load",[&]{require(!store.load(last,error).isEmpty(),error);});
        obs_data_release(settings);
    }
    std::puts("PASS: isolated preset benchmark complete");
}
