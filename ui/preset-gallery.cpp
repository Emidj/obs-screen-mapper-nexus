// SPDX-License-Identifier: GPL-2.0-or-later
#include "presets.hpp"
#include "../src/zones.hpp"
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QFont>
#include <QJsonDocument>
#include <QThread>
#include <cstdio>
#include <cstdlib>
static void require(bool ok,const QString &message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message.toUtf8().constData());std::exit(1);}}
struct Example {QString file,name,description;mapping::Layout layout;};
static mapping::Zone zone(int id,mapping::Rect destination,float curve=0){
    mapping::Zone z;z.id=std::to_string(id);z.name="Zona "+z.id;z.destination=destination;
    if(curve!=0){z.mesh.enabled=true;z.mesh.horizontal=curve;z.mesh.soft=true;}
    return z;
}
static QImage render(obs_source_t *source){
    obs_enter_graphics();auto *target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);auto *stage=gs_stagesurface_create(1920,1080,GS_RGBA);
    require(target&&stage&&gs_texrender_begin(target,1920,1080),"GPU target");vec4 clear;vec4_zero(&clear);gs_clear(GS_CLEAR_COLOR,&clear,0,0);gs_ortho(0,1920,0,1080,-100,100);gs_matrix_identity();
    gs_blend_state_push();gs_blend_function(GS_BLEND_ONE,GS_BLEND_INVSRCALPHA);obs_source_video_render(source);gs_blend_state_pop();gs_texrender_end(target);
    gs_stage_texture(stage,gs_texrender_get_texture(target));uint8_t *data=nullptr;uint32_t stride=0;require(gs_stagesurface_map(stage,&data,&stride),"GPU readback");
    auto image=QImage(data,1920,1080,int(stride),QImage::Format_RGBA8888_Premultiplied).copy();gs_stagesurface_unmap(stage);gs_stagesurface_destroy(stage);gs_texrender_destroy(target);obs_leave_graphics();return image;
}
int main(int argc,char **argv){
    QApplication app(argc,argv);require(argc==6,"usage: gallery CORE DATA OBS_ROOT INPUT_PNG OUTPUT");require(obs_startup("es-ES",nullptr,nullptr),"libobs startup");mapper_i18n::set_locale("es-ES");
    const std::string root=argv[3],data=root+"/data/libobs/",graphics=root+"/bin/64bit/libobs-d3d11.dll";
#pragma warning(push)
#pragma warning(disable:4996)
    obs_add_data_path(data.c_str());
#pragma warning(pop)
    obs_video_info video{};video.graphics_module=graphics.c_str();video.fps_num=30;video.fps_den=1;video.base_width=1920;video.base_height=1080;video.output_width=1920;video.output_height=1080;video.output_format=VIDEO_FORMAT_RGBA;video.colorspace=VIDEO_CS_709;video.range=VIDEO_RANGE_FULL;video.scale_type=OBS_SCALE_BILINEAR;
    require(obs_reset_video(&video)==OBS_VIDEO_SUCCESS,"D3D11 startup");obs_module_t *core=nullptr,*images=nullptr;
    require(obs_open_module(&core,argv[1],argv[2])==MODULE_SUCCESS&&obs_init_module(core),"mapper module");
    const auto imageModule=root+"/obs-plugins/64bit/image-source.dll",imageData=root+"/data/obs-plugins/image-source";
    require(obs_open_module(&images,imageModule.c_str(),imageData.c_str())==MODULE_SUCCESS&&obs_init_module(images),"OBS image source module");
    auto *inputSettings=obs_data_create();obs_data_set_string(inputSettings,"file",argv[4]);auto *input=obs_source_create("image_source","Fotograma de Tokio",inputSettings,nullptr);obs_data_release(inputSettings);require(input&&obs_source_get_width(input)>0,"reference image loaded");
    std::vector<Example> examples;
    auto add=[&](const char *file,const char *name,const char *description,mapping::Mode mode=mapping::Mode::Repeat)->mapping::Layout&{Example e;e.file=file;e.name=QString::fromUtf8(name);e.description=QString::fromUtf8(description);e.layout.mode=mode;e.layout.automatic=false;examples.push_back(std::move(e));return examples.back().layout;};
    add("01-panoramica-curva","Panorámica curva","1 zona · malla suave").zones={zone(1,{.055f,.12f,.89f,.72f},.12f)};
    auto &dip=add("02-diptico","Díptico","2 zonas · Split",mapping::Mode::Split);dip.zones={zone(1,{.04f,.14f,.44f,.69f},-.09f),zone(2,{.52f,.14f,.44f,.69f},.09f)};
    auto &trip=add("03-triptico-curvo","Tríptico curvo","3 zonas · Custom UV",mapping::Mode::Custom);
    for(int i=0;i<3;++i){auto z=zone(i+1,{.035f+i*.325f,.14f,.29f,.70f},(i-1)*.1f);z.uv={i/3.0f,0,1/3.0f,1};trip.zones.push_back(z);}
    auto &six=add("04-escenario-seis","Escenario 3 × 2","6 zonas · Repeat + malla");
    for(int i=0;i<6;++i)six.zones.push_back(zone(i+1,{.035f+(i%3)*.325f,.07f+(i/3)*.46f,.29f,.39f},(i%3-1)*.10f));
    auto &wall=add("05-videowall","Videowall 4 × 3","12 zonas · Custom UV",mapping::Mode::Custom);
    for(int i=0;i<12;++i){auto z=zone(i+1,{.03f+(i%4)*.24f,.05f+(i/4)*.30f,.22f,.28f});z.uv={float(i%4)/4,float(i/4)/3,.25f,1/3.0f};wall.zones.push_back(z);}
    auto &fan=add("06-abanico","Abanico","5 zonas · Polygon + Custom UV",mapping::Mode::Custom);
    for(int i=0;i<5;++i){auto z=zone(i+1,{.03f+i*.19f,.12f,.18f,.73f});z.polygon.enabled=true;const float inset=.16f+.05f*std::abs(i-2);z.polygon.vertices={{inset,0,0,0},{1-inset,0,1,0},{1,1,1,1},{0,1,0,1}};z.uv={i*.2f,0,.2f,1};fan.zones.push_back(z);}
    auto &diamonds=add("07-rombos","Tres rombos","3 zonas · Polygon + malla");
    for(int i=0;i<3;++i){auto z=zone(i+1,{.035f+i*.325f,.14f,.29f,.72f},.025f);z.polygon.enabled=true;z.polygon.vertices={{.5f,0,.5f,0},{1,.5f,1,.5f},{.5f,1,.5f,1},{0,.5f,0,.5f}};diamonds.zones.push_back(z);}
    auto &orbit=add("08-orbital","Orbital","5 zonas · máscaras elípticas");orbit.zones={zone(1,{.35f,.23f,.30f,.533333f}),zone(2,{.055f,.10f,.16f,.284444f}),zone(3,{.785f,.10f,.16f,.284444f}),zone(4,{.055f,.61f,.16f,.284444f}),zone(5,{.785f,.61f,.16f,.284444f})};for(auto &z:orbit.zones){z.mask.kind=1;z.mask.feather=.012f;}
    auto &waves=add("09-ondas","Ondas","4 zonas · malla con perfil de onda",mapping::Mode::Custom);
    for(int i=0;i<4;++i){auto z=zone(i+1,{.035f+i*.242f,.20f,.21f,.59f},i%2?.09f:-.09f);z.mesh.wave=true;z.uv={i*.25f,0,.25f,1};waves.zones.push_back(z);}
    auto &booth=add("10-cabina-dj","Cabina DJ","5 zonas · Repeat + Polygon + malla");booth.zones={zone(1,{.25f,.12f,.50f,.44f},-.09f),zone(2,{.025f,.16f,.18f,.48f},.08f),zone(3,{.795f,.16f,.18f,.48f},-.08f),zone(4,{.065f,.76f,.42f,.13f},.07f),zone(5,{.515f,.76f,.42f,.13f},-.07f)};for(size_t i=1;i<3;++i){auto &z=booth.zones[i];z.polygon.enabled=true;z.polygon.vertices={{.1f,0,0,0},{.9f,.08f,1,0},{1,1,1,1},{0,.93f,0,1}};}
    const QString output=QString::fromLocal8Bit(argv[5]);QDir().mkpath(output);QDir().mkpath(output+"/presets");
    QImage sheet(1440,2320,QImage::Format_RGB32);sheet.fill(QColor("#08131d"));QPainter painter(&sheet);painter.setRenderHint(QPainter::SmoothPixmapTransform);painter.setPen(Qt::white);painter.setFont(QFont("Segoe UI",25,QFont::Bold));painter.drawText(24,44,"OBS Screen Mapper · 10 presets");painter.setFont(QFont("Segoe UI",12));painter.setPen(QColor("#aac4d8"));painter.drawText(24,72,QString::fromUtf8("Previews del motor real · mismo fotograma de tu vídeo · 1920 × 1080 · damero = transparencia"));
    QJsonArray manifest;
    for(size_t index=0;index<examples.size();++index){auto &example=examples[index];
        for(auto &z:example.layout.zones){mapping::compile_polygon(z.polygon);auto mesh=z.mesh;const auto surface=mapping::compile_surface(mesh,z.polygon);require(z.polygon.error.empty()&&surface.error.empty(),"invalid geometry: "+example.name);}
        auto *settings=obs_data_create();obs_data_set_int(settings,"canvas_width",1920);obs_data_set_int(settings,"canvas_height",1080);obs_data_set_string(settings,"input_uuid",obs_source_get_uuid(input));obs_data_set_string(settings,"distribution",example.layout.mode==mapping::Mode::Split?"split":example.layout.mode==mapping::Mode::Custom?"custom":"repeat");mapping::write(settings,example.layout);obs_data_set_string(settings,"selected_zone","1");
        QString error;const auto bytes=MapperPresets::encode(settings,example.name,error);require(!bytes.isEmpty(),error);QJsonObject document;require(MapperPresets::validate(bytes,document,error),error);require(!bytes.contains(obs_source_get_uuid(input)),"preset must not bind a source UUID");
        const auto file=output+"/presets/"+example.file+".mapper.json";require(MapperPresets::writeFile(file,bytes,error),error);
        MapperPresets store(output+"/validation-cache");QJsonObject restored;QString restoredName;require(store.materialize(bytes,restored,restoredName,error),error);auto *reloaded=obs_data_create_from_json(QJsonDocument(restored).toJson().constData());require(mapping::read(reloaded).zones.size()==example.layout.zones.size(),"roundtrip zone count");obs_data_set_string(reloaded,"input_uuid",obs_source_get_uuid(input));
        auto *source=obs_source_create_private("obs_screen_mapper","Preset render",reloaded);require(source!=nullptr,"mapper source");QThread::msleep(80);const auto image=render(source);require(!image.isNull()&&image.save(output+"/"+example.file+".png"),"render save");
        bool nonempty=false;for(int y=0;y<image.height();y+=11)for(int x=0;x<image.width();x+=11)nonempty|=image.pixelColor(x,y).alpha()>0;require(nonempty,"preset unexpectedly transparent");
        const int x=24+int(index%2)*708,y=108+int(index/2)*438;for(int yy=0;yy<378;yy+=18)for(int xx=0;xx<672;xx+=18)painter.fillRect(x+xx,y+yy,18,18,((xx/18+yy/18)%2)?QColor("#1a2a3b"):QColor("#23384b"));painter.drawImage(QRect(x,y,672,378),image);painter.setPen(Qt::white);painter.setFont(QFont("Segoe UI",15,QFont::Bold));painter.drawText(x,y+403,QString::number(index+1)+". "+example.name);painter.setPen(QColor("#aac4d8"));painter.setFont(QFont("Segoe UI",11));painter.drawText(x,y+425,example.description);
        manifest.append(QJsonObject{{"file",example.file+".mapper.json"},{"name",example.name},{"zones",int(example.layout.zones.size())},{"description",example.description}});obs_source_release(source);obs_data_release(reloaded);obs_data_release(settings);std::printf("PASS: preset exported, validated, roundtripped and GPU rendered: %s\n",example.file.toUtf8().constData());
    }
    painter.end();require(sheet.save(output+"/10-presets.png"),"contact sheet save");QString error;require(MapperPresets::writeFile(output+"/manifest.json",QJsonDocument(manifest).toJson(),error),error);obs_source_release(input);obs_shutdown();std::puts("PASS: 10 real GPU preset previews complete");return 0;
}
