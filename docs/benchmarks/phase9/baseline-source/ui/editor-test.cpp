// SPDX-License-Identifier: GPL-2.0-or-later
#include "editor.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QMouseEvent>
#include <QScreen>
#include <QDir>
#include <QPainter>
#include <QTemporaryDir>
#include <QFile>
#include <QUuid>
#include <graphics/vec4.h>
#include <cstdio>
#include <cstdlib>

static void check(bool ok,const char *label) { std::printf("%s: %s\n",ok?"PASS":"FAIL",label);std::fflush(stdout);if(!ok)std::exit(1); }
static void pump(int milliseconds=120) { QElapsedTimer clock;clock.start();while(clock.elapsed()<milliseconds){QApplication::processEvents();QThread::msleep(2);} }
static const char *fixtureName(void*) { return "Fuente de prueba GPU"; }
static void *fixtureCreate(obs_data_t*,obs_source_t*) { return reinterpret_cast<void*>(1); }
static void fixtureDestroy(void*) {}
static uint32_t fixtureWidth(void*) { return 960; }
static uint32_t fixtureHeight(void*) { return 540; }
static gs_effect_t *fixtureEffect=nullptr;
static void fixtureRender(void*,gs_effect_t*) {
    if(!fixtureEffect) fixtureEffect=gs_effect_create(R"(
uniform float4x4 ViewProj;
struct V {float4 pos:POSITION;float2 uv:TEXCOORD0;};
V VS(V i){V o;o.pos=mul(float4(i.pos.xyz,1),ViewProj);o.uv=i.uv;return o;}
float4 PS(V i):TARGET {
 float2 p=i.uv; float pulse=abs(sin(p.x*31+p.y*13+sin(p.y*16)*2));
 float grid=pow(abs(sin(p.x*90)*sin(p.y*50)),14);
 float3 color=lerp(float3(.02,.35,.95),float3(.8,.02,.65),p.x);
 color*=.25+pulse*.65; color+=grid*float3(.2,.5,.65);
 return float4(color,1);
}
technique Draw{pass{vertex_shader=VS(i);pixel_shader=PS(i);}}
)","editor-test-fixture.effect",nullptr);
    if(fixtureEffect) while(gs_effect_loop(fixtureEffect,"Draw"))gs_draw_sprite(nullptr,0,960,540);
}
static void click(MapperEditor &editor,const char *name) { auto *button=editor.findChild<QPushButton*>(name);check(button!=nullptr,name);button->click();pump(); }
static void drag(MapperPreview *preview,QPointF start,QPointF finish) {
    const auto globalStart=QPointF(preview->mapToGlobal(start.toPoint())),globalFinish=QPointF(preview->mapToGlobal(finish.toPoint()));
    QMouseEvent press(QEvent::MouseButtonPress,start,globalStart,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(preview,&press);
    QMouseEvent move(QEvent::MouseMove,finish,globalFinish,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(preview,&move);
    QMouseEvent release(QEvent::MouseButtonRelease,finish,globalFinish,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(preview,&release);pump();
}
static void testEditorPresets(obs_source_t *target,const QString &output)
{
    QTemporaryDir fixture(output+"/portable-ui-XXXXXX");check(fixture.isValid(),"isolated preset editor fixtures created");
    const auto reference=fixture.filePath(QString::fromUtf8("referencia-ñ.png"));QImage image(32,32,QImage::Format_RGBA8888);image.fill(QColor(255,255,255,128));check(image.save(reference),"portable editor image created");
    const auto portable=fixture.filePath("scenario.mapper.json");QString id,error;
    auto *settings=obs_source_get_settings(target);const std::string before=obs_data_get_json(settings),input=obs_data_get_string(settings,"input_uuid");obs_data_release(settings);
    {
        MapperEditor editor(target);editor.show();pump();click(editor,"addZone");click(editor,"tool2");editor.findChild<QCheckBox*>("polygon")->setChecked(true);
        editor.findChild<QDoubleSpinBox*>("curveH")->setValue(10);editor.findChild<QCheckBox*>("soft")->setChecked(true);
        editor.findChild<QComboBox*>("maskType")->setCurrentIndex(3);auto *path=editor.findChild<QLineEdit*>("maskFile");path->setText(reference);QMetaObject::invokeMethod(path,"editingFinished",Qt::DirectConnection);
        editor.findChild<QDoubleSpinBox*>("uvW")->setValue(40);editor.findChild<QCheckBox*>("showGrid")->setChecked(false);editor.preview->restoreView(1.6f,{23,-15});
        check(editor.preview->setReference(reference),"portable preset captures editor reference background");pump();
        id=editor.savePresetAs(QString::fromUtf8("Escenario ñ"),error);check(!id.isEmpty()&&error.isEmpty(),"Save As creates a local preset from editor working copy");
        check(editor.exportPreset(portable,QString::fromUtf8("Escenario ñ"),error),"editor exports self-contained portable preset");
        click(editor,"addZone");check(editor.workingLayout().zones.size()==4,"editor changes after saving do not alter saved preset");
        click(editor,"loadPreset");check(editor.workingLayout().zones.size()==3,"Load button restores saved zones from local preset");
        click(editor,"undoEdit");check(editor.workingLayout().zones.size()==4,"loading preset is undoable as a single change");click(editor,"redoEdit");check(editor.workingLayout().zones.size()==3,"preset load can be redone");
        auto *draft=obs_source_get_settings(editor.previewSource());check(std::string(obs_data_get_string(draft,"input_uuid"))==input,"local preset keeps current OBS input binding");obs_data_release(draft);
        const auto invalid=fixture.filePath("bad.mapper.json");check(MapperPresets::writeFile(invalid,"not a preset",error),"invalid import fixture created");
        const auto previous=editor.workingLayout().zones.size();check(editor.importPreset(invalid,error).isEmpty()&&editor.workingLayout().zones.size()==previous,"failed editor import leaves working configuration unchanged");
        check(QFile::rename(reference,reference+".moved"),"original portable image moved after export");
        MapperPresets store(qEnvironmentVariable("OBS_MAPPER_PRESET_DIR"));check(editor.savePreset(error),"loaded preset remains saveable after original images move");const auto stored=store.load(id,error);
        auto *missing=editor.findChild<QLineEdit*>("maskFile");missing->setText(fixture.filePath("missing.png"));QMetaObject::invokeMethod(missing,"editingFinished",Qt::DirectConnection);pump();
        check(!editor.savePreset(error)&&store.load(id,error)==stored,"failed save with missing selected image preserves previously saved preset");
        click(editor,"cancelEditor");settings=obs_source_get_settings(target);check(std::string(obs_data_get_json(settings))==before,"Cancel after preset operations leaves original source unchanged");obs_data_release(settings);
    }
    {
        MapperEditor editor(target);editor.show();pump();const auto imported=editor.importPreset(portable,error);
        check(!imported.isEmpty()&&editor.workingLayout().zones.size()==3,"portable preset imports after original images are moved");
        auto loaded=editor.workingLayout();check(loaded.zones[2].mask.kind==3&&QFileInfo::exists(QString::fromUtf8(loaded.zones[2].mask.path.c_str()))&&loaded.zones[2].polygon.enabled&&loaded.zones[2].mesh.enabled,"imported masks and combined geometry resolve from owned asset cache");
        auto *draft=obs_source_get_settings(editor.previewSource());check(std::string(obs_data_get_string(draft,"input_uuid"))==input,"import never replaces destination OBS source");obs_data_release(draft);
        click(editor,"applyEditor");pump();
    }
    {
        MapperEditor reopened(target);reopened.show();pump();const auto view=reopened.preview->viewState();
        check(!reopened.findChild<QCheckBox*>("showGrid")->isChecked()&&std::abs(view.zoom-1.6f)<1e-5&&view.pan==QPointF(23,-15),"Apply persists editor zoom, pan and grid across reopening");
        check(!reopened.currentPreset().isEmpty()&&!reopened.preview->referencePath().isEmpty()&&QFileInfo::exists(reopened.preview->referencePath()),"Apply persists preset selection and portable reference background");
        check(reopened.workingLayout().zones[2].mesh.horizontal==.1f&&reopened.workingLayout().zones[2].uv.w==.4f,"Apply persists preset geometry and Custom UV");reopened.reject();
    }
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);check(argc>=5,"editor host paths supplied");
    const QString output=QString::fromLocal8Bit(argv[4]);QDir().mkpath(output);
    qputenv("OBS_MAPPER_PRESET_DIR",(output+"/preset-library-"+QUuid::createUuid().toString(QUuid::WithoutBraces)).toUtf8());
    check(obs_startup("es-ES",nullptr,nullptr),"libobs initialized for Qt editor host");
#pragma warning(push)
#pragma warning(disable:4996)
    const std::string data=std::string(argv[3])+"/";obs_add_data_path(data.c_str());
#pragma warning(pop)
    const std::string graphics=std::string(argv[3])+"/../../bin/64bit/libobs-d3d11.dll";
    obs_video_info video{};video.graphics_module=graphics.c_str();video.fps_num=60;video.fps_den=1;video.base_width=1920;video.base_height=1080;video.output_width=1920;video.output_height=1080;
    video.output_format=VIDEO_FORMAT_RGBA;video.colorspace=VIDEO_CS_709;video.range=VIDEO_RANGE_FULL;video.scale_type=OBS_SCALE_BILINEAR;
    check(obs_reset_video(&video)==OBS_VIDEO_SUCCESS,"D3D11 editor preview initialized");
    obs_module_t *module=nullptr;check(obs_open_module(&module,argv[1],argv[2])==MODULE_SUCCESS&&obs_init_module(module),"native mapper loaded in Qt host");
    obs_source_info fixture{};fixture.id="mapper_editor_fixture";fixture.type=OBS_SOURCE_TYPE_INPUT;fixture.output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_CUSTOM_DRAW;
    fixture.get_name=fixtureName;fixture.create=fixtureCreate;fixture.destroy=fixtureDestroy;fixture.get_width=fixtureWidth;fixture.get_height=fixtureHeight;fixture.video_render=fixtureRender;obs_register_source(&fixture);
    auto *input=obs_source_create(fixture.id,"Fuente de prueba GPU",nullptr,nullptr);
    auto *settings=obs_data_create();obs_data_set_string(settings,"input_uuid",obs_source_get_uuid(input));
    auto *target=obs_source_create("obs_screen_mapper","Mapper original",settings,nullptr);obs_data_release(settings);
    settings=obs_source_get_settings(target);const std::string original=obs_data_get_json(settings);obs_data_release(settings);
    {
        MapperEditor editor(target);editor.show();pump(500);
        check(editor.workingLayout().zones.size()==1,"editor starts with existing source state");
        click(editor,"addZone");check(editor.workingLayout().zones.size()==2,"add zone through native Qt UI");
        settings=obs_source_get_settings(target);check(std::string(obs_data_get_json(settings))==original,"working copy leaves original settings untouched");obs_data_release(settings);
        click(editor,"tool2");check(editor.workingLayout().zones[1].mesh.enabled,"mesh tool enables editable mesh");
        editor.findChild<QCheckBox*>("soft")->setChecked(true);editor.findChild<QDoubleSpinBox*>("radius")->setValue(100);pump();
        auto point=editor.preview->widgetPoint({.5+.5/3,1.0/3});drag(editor.preview,point,point+QPointF(12,6));
        auto model=editor.workingLayout();check(model.zones[1].mesh.points[5].x>0&&model.zones[1].mesh.points[6].x>0,"direct mesh drag applies Soft Selection to neighbors");
        click(editor,"undoEdit");check(editor.workingLayout().zones[1].mesh.points[5].x==0,"undo reverts whole mesh gesture");
        click(editor,"redoEdit");check(editor.workingLayout().zones[1].mesh.points[5].x>0,"redo reapplies mesh gesture");
        editor.findChild<QCheckBox*>("mesh")->setChecked(false);click(editor,"tool1");
        point=editor.preview->widgetPoint({.5,0});drag(editor.preview,point,point+QPointF(10,10));model=editor.workingLayout();
        check(model.zones[1].polygon.vertices[0].x>0&&model.zones[1].polygon.vertices[0].y>0&&model.zones[1].polygon.vertices[0].u==0,"direct Polygon drag edits position and preserves UV");
        editor.findChild<QComboBox*>("maskType")->setCurrentIndex(1);editor.findChild<QDoubleSpinBox*>("uvW")->setValue(50);pump();
        check(editor.workingLayout().zones[1].mask.kind==1&&editor.workingLayout().mode==mapping::Mode::Custom,"contextual mask and UV controls update working state");
        auto *name=editor.findChild<QLineEdit*>("zoneName");name->setText(QString::fromUtf8("Zona ñ"));QMetaObject::invokeMethod(name,"editingFinished",Qt::DirectConnection);pump();
        check(editor.workingLayout().zones[1].name=="Zona ñ","Unicode zone rename");
        click(editor,"zoneUp");check(editor.workingLayout().zones[0].name=="Zona ñ","native reorder buttons preserve selected zone identity");click(editor,"undoEdit");
        auto *list=editor.findChild<QListWidget*>("zoneList");list->item(1)->setCheckState(Qt::Unchecked);pump();check(!editor.workingLayout().zones[1].visible,"zone list checkbox hides a zone without deleting it");
        auto *draftSettings=obs_source_get_settings(editor.previewSource());const std::string draftBefore=obs_data_get_json(draftSettings);obs_data_release(draftSettings);
        QImage reference(64,64,QImage::Format_RGBA8888);reference.fill(QColor(180,30,20));const QString referencePath=output+"/reference-test.png";reference.save(referencePath);
        check(editor.preview->setReference(referencePath),"editor reference background loads");
        draftSettings=obs_source_get_settings(editor.previewSource());check(std::string(obs_data_get_json(draftSettings))==draftBefore,"reference background does not enter source settings or output");obs_data_release(draftSettings);
        for(int i=0;i<7;++i)editor.zoneAction("add");pump();check(editor.workingLayout().zones.size()==9&&list->count()==9,"editor supports more than six zones");
        click(editor,"cancelEditor");settings=obs_source_get_settings(target);check(std::string(obs_data_get_json(settings))==original,"Cancel discards geometry, mask, UV and name changes");obs_data_release(settings);
    }
    {
        MapperEditor editor(target);editor.show();pump();click(editor,"addZone");editor.findChild<QSpinBox*>("canvasWidth")->setValue(1280);editor.findChild<QSpinBox*>("canvasHeight")->setValue(720);click(editor,"applyEditor");pump();
        settings=obs_source_get_settings(target);check(mapping::read(settings).zones.size()==2&&obs_source_get_width(target)==1280&&obs_source_get_height(target)==720,"Apply commits source model and canvas size");obs_data_release(settings);
    }
    {
        auto *scene=obs_scene_create("Escena que contiene el mapper");auto *item=obs_scene_add(scene,target);
        MapperEditor editor(target);editor.show();pump();auto *sources=editor.findChild<QComboBox*>("inputSource");
        const auto uuid=QString::fromUtf8(obs_source_get_uuid(obs_scene_get_source(scene)));const int index=sources->findData(uuid);check(index>=0,"OBS scenes available in editor source selector");sources->setCurrentIndex(index);pump();
        auto *draftSettings=obs_source_get_settings(editor.previewSource());check(std::string(obs_data_get_string(draftSettings,"input_uuid"))==obs_source_get_uuid(input),"editor rejects circular scene input before applying");obs_data_release(draftSettings);editor.reject();obs_sceneitem_remove(item);obs_source_remove(obs_scene_get_source(scene));obs_scene_release(scene);
    }
    {
        bool bridgeOpened=false;
        QTimer::singleShot(400,[&]{for(auto *widget:QApplication::topLevelWidgets())if(widget->objectName()=="mapperEditor"){bridgeOpened=true;if(auto *dialog=qobject_cast<QDialog*>(widget))dialog->reject();}});
        auto *properties=obs_source_properties(target);auto *property=obs_properties_get(properties,"open_editor");check(property!=nullptr,"source exposes Open Mapper Editor action");
        check(!obs_property_visible(obs_properties_get(properties,"zone_name")),"native source properties hide advanced numeric controls by default");
        obs_property_button_clicked(property,target);obs_properties_destroy(properties);
        check(bridgeOpened,"core opens editor DLL in process through native properties action");
    }
    testEditorPresets(target,output);
    settings=obs_source_get_settings(target);auto layout=mapping::read(settings);layout.zones.clear();layout.automatic=false;layout.mode=mapping::Mode::Split;obs_data_set_string(settings,"distribution","split");
    for(int i=0;i<6;++i) {
        mapping::Zone zone;zone.id=std::to_string(i+1);zone.name="Zona "+zone.id;zone.destination={.035f+float(i%3)*.325f,.07f+float(i/3)*.46f,.29f,.39f};
        zone.polygon.enabled=true;zone.polygon.vertices={{0,.03f,0,0},{1,0,1,0},{.98f,1,1,1},{.02f,.98f,0,1}};
        zone.mesh.enabled=true;zone.mesh.horizontal=(i%3-1)*.12f;zone.mesh.soft=true;zone.mesh.strength=.7f;layout.zones.push_back(std::move(zone));
    }
    mapping::write(settings,layout);auto *emptyWorkspace=obs_data_create();obs_data_set_obj(settings,"editor",emptyWorkspace);obs_data_release(emptyWorkspace);obs_data_set_int(settings,"canvas_width",1920);obs_data_set_int(settings,"canvas_height",1080);obs_source_update(target,settings);obs_data_release(settings);pump();
    {
        MapperEditor editor(target);editor.select(0);editor.show();pump(700);click(editor,"tool2");pump(400);
        QString error;check(!editor.savePresetAs("Escenario curvo (6 zonas)",error).isEmpty(),"six-zone example saved through preset API");check(editor.exportPreset(output+"/escenario-curvo-6-zonas.mapper.json","Escenario curvo (6 zonas)",error),"reusable six-zone example preset exported");pump();
        check(editor.preview->frameCount()>5,"native preview display renders GPU frames");
        auto image=editor.grab();const auto gpu=editor.preview->renderFrame();check(!gpu.isNull(),"GPU preview frame readback available");gpu.save(output+"/preview-gpu.png");
        int colored=0;for(int y=0;y<gpu.height();y+=5)for(int x=0;x<gpu.width();x+=5){auto color=gpu.pixelColor(x,y);if(color.blue()>80&&color.blue()>color.red()*1.2)++colored;}
        check(colored>200,"GPU preview contains rendered input texture rather than a blank surface");
        {QPainter painter(&image);painter.drawImage(QRect(editor.preview->mapTo(&editor,QPoint(0,0)),editor.preview->size()),gpu);}
        check(!image.isNull()&&image.save(output+"/editor-phase8.png"),"three-panel preset editor internal render artifact saved");
        if(argc>5 && std::string(argv[5])=="--interactive") editor.exec();else editor.reject();
    }
    obs_source_release(target);obs_source_release(input);pump();
    obs_enter_graphics();gs_effect_destroy(fixtureEffect);fixtureEffect=nullptr;obs_leave_graphics();obs_shutdown();
    std::puts("PASS: native Qt phase 8 editor and preset validation complete");return 0;
}
