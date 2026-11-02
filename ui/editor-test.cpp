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
#include <QKeyEvent>
#include <QJsonDocument>
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
static int sceneBackgroundCreates=0;
static void *sceneBackgroundCreate(obs_data_t*,obs_source_t*){++sceneBackgroundCreates;return reinterpret_cast<void*>(1);}
static void sceneBackgroundRender(void*,gs_effect_t*){vec4 c;vec4_set(&c,.04f,.65f,.03f,1);auto *effect=obs_get_base_effect(OBS_EFFECT_SOLID);gs_effect_set_vec4(gs_effect_get_param_by_name(effect,"color"),&c);while(gs_effect_loop(effect,"Solid"))gs_draw_sprite(nullptr,0,1920,1080);}
static size_t countSceneSource(obs_scene_t *scene,obs_source_t *source){size_t count=0;mapper_scene::Items items(scene);for(auto *item:items.values){auto *child=obs_sceneitem_get_source(item);if(child==source)++count;else if(auto *nested=obs_group_or_scene_from_source(child))count+=countSceneSource(nested,source);}return count;}
static size_t filterCount(obs_source_t *source){size_t count=0;obs_source_enum_filters(source,[](obs_source_t*,obs_source_t*,void *data){++*static_cast<size_t*>(data);},&count);return count;}
static void testSceneContext(obs_source_t *target,const QString &output){
    obs_source_info info{};info.id="mapper_scene_background_qa";info.type=OBS_SOURCE_TYPE_INPUT;info.output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_CUSTOM_DRAW;info.get_name=fixtureName;info.create=sceneBackgroundCreate;info.destroy=fixtureDestroy;info.get_width=[](void*)->uint32_t{return 1920;};info.get_height=[](void*)->uint32_t{return 1080;};info.video_render=sceneBackgroundRender;obs_register_source_s(&info,sizeof(info));
    auto *background=obs_source_create_private(info.id,"Shared scene background",nullptr);
    auto *settings=obs_data_create();auto *original=obs_source_get_settings(target);obs_data_set_string(settings,"input_uuid",obs_data_get_string(original,"input_uuid"));obs_data_release(original);
    auto *source=obs_source_create_private("obs_screen_mapper","Scene context Mapper",settings);obs_data_release(settings);
    {MapperEditor editor(source);check(!editor.findChild<QPushButton*>("showSceneContext")->isEnabled(),"scene reference disabled until Mapper belongs to a scene");}
    auto *scene=obs_scene_create("Referencia completa QA");obs_scene_add(scene,background);auto *item=obs_scene_add(scene,source);
    obs_source_info filterInfo{};filterInfo.id="mapper_cold_scene_filter_qa";filterInfo.type=OBS_SOURCE_TYPE_FILTER;filterInfo.output_flags=OBS_SOURCE_VIDEO;
    filterInfo.get_name=fixtureName;filterInfo.create=fixtureCreate;filterInfo.destroy=fixtureDestroy;
    filterInfo.get_width=[](void*)->uint32_t{return 0;};filterInfo.get_height=[](void*)->uint32_t{return 0;};
    filterInfo.video_render=[](void*,gs_effect_t*){};obs_register_source_s(&filterInfo,sizeof(filterInfo));
    auto *cold=obs_source_create_private(filterInfo.id,"Cold scene filter QA",nullptr);obs_source_filter_add(obs_scene_get_source(scene),cold);
    check(obs_source_get_width(obs_scene_get_source(scene))==0,"regression fixture reproduces scene filter reporting zero width");
    obs_transform_info placement{};obs_sceneitem_get_info2(item,&placement);placement.pos.x=500;placement.pos.y=200;placement.scale.x=.35f;placement.scale.y=.4f;placement.rot=10;obs_sceneitem_set_info2(item,&placement);obs_sceneitem_force_update_transform(item);
    auto *saved=obs_save_source(obs_scene_get_source(scene));const std::string sceneBefore=obs_data_get_json(saved);obs_data_release(saved);
    auto *nested=obs_scene_create("Referencia anidada QA");obs_scene_add(nested,background);auto *groupSource=obs_source_create_private("group","Reference group QA",nullptr);auto *group=obs_group_from_source(groupSource);obs_scene_add(group,source);auto *groupItem=obs_scene_add(nested,groupSource);obs_sceneitem_get_info2(groupItem,&placement);placement.pos.x=300;placement.pos.y=120;placement.scale.x=.5f;placement.scale.y=.5f;obs_sceneitem_set_info2(groupItem,&placement);
    {
        MapperEditor editor(source);editor.show();pump();auto *picker=editor.findChild<QComboBox*>("referenceScene");const auto uuid=QString::fromUtf8(obs_source_get_uuid(obs_scene_get_source(scene)));picker->setCurrentIndex(picker->findData(uuid));
        const auto view=editor.preview->viewState();click(editor,"showSceneContext");auto context=editor.preview->sceneContext();check(context&&context->width==1920&&context->height==1080,"scene button displays full scene canvas");
        check(context->omittedFilters==1&&filterCount(obs_scene_get_source(context->scene))==0,"scene reference omits container filter with zero dimensions");
        check(filterCount(obs_scene_get_source(scene))==1&&obs_source_enabled(cold)&&obs_filter_get_parent(cold)==obs_scene_get_source(scene),"original scene filter remains enabled and attached");
        check(editor.findChild<QLabel*>("editorStatus")->text().contains(QString::fromUtf8(mapper_i18n::text(" · Los filtros de escena y grupo se omiten solo en esta vista; la salida de OBS los conserva."))),"scene view explains omitted filters");
        check(countSceneSource(context->scene,source)==0&&countSceneSource(context->scene,editor.previewSource())==1&&countSceneSource(context->scene,background)==1&&sceneBackgroundCreates==1,"private composition replaces Mapper and shares existing video sources");
        const QPointF point(.23,.47);check(QLineF(editor.preview->canvasPoint(editor.preview->widgetPoint(point)),point).length()<.00001,"scene placement and inverse picking agree under scale and rotation");
        editor.findChild<QCheckBox*>("showZones")->setChecked(false);editor.findChild<QCheckBox*>("showGrid")->setChecked(false);pump();
        auto sample=[&](const QImage &image,QPointF p){const auto q=editor.preview->widgetPoint(p)*editor.preview->devicePixelRatioF();return image.pixelColor(int(q.x()),int(q.y()));};
        auto image=editor.preview->renderFrame();auto color=sample(image,{.2,.5});check(color.blue()>25,"scene reference renders Mapper input at its OBS item placement");
        editor.findChild<QDoubleSpinBox*>("transform_x")->setValue(50);pump();image=editor.preview->renderFrame();color=sample(image,{.2,.5});check(color.green()>100&&color.blue()<25,"draft edit reveals other scene sources before Apply");
        auto *sourceSettings=obs_source_get_settings(source);check(mapping::read(sourceSettings).zones[0].transform.x==0,"scene view does not commit draft to original Mapper");obs_data_release(sourceSettings);
        saved=obs_save_source(obs_scene_get_source(scene));check(sceneBefore==obs_data_get_json(saved),"scene view preserves original items, order and transforms");obs_data_release(saved);
        click(editor,"undoEdit");editor.findChild<QCheckBox*>("showZones")->setChecked(true);pump();
        const auto outside=context->transform.inverted().map(QPointF(100,100));const auto backgroundPoint=editor.preview->widgetPoint({outside.x()/1920,outside.y()/1080});drag(editor.preview,backgroundPoint,backgroundPoint+QPointF(20,10));check(editor.workingLayout().zones[0].transform.x==0,"dragging another scene source does not move the Mapper");
        editor.findChild<QSpinBox*>("canvasWidth")->setValue(1600);pump();check(editor.preview->sceneContext()&&editor.preview->sceneContext()->mapperWidth==1600,"scene placement refreshes after changing draft canvas size");click(editor,"undoEdit");pump();check(editor.preview->sceneContext()->mapperWidth==1920,"undo restores scene placement after canvas resize");
        auto shot=editor.grab();image=editor.preview->renderFrame();{QPainter painter(&shot);painter.drawImage(QRect(editor.preview->mapTo(&editor,QPoint()),editor.preview->size()),image);}check(shot.save(output+"/full-scene-context-editor.png"),"full scene reference editor artifact saved");
        picker->setCurrentIndex(picker->findData(QString::fromUtf8(obs_source_get_uuid(obs_scene_get_source(nested)))));pump();context=editor.preview->sceneContext();check(context&&countSceneSource(context->scene,source)==0&&countSceneSource(context->scene,editor.previewSource())==1&&sceneBackgroundCreates==1,"nested group context replaces only containers and shares sources");
        check(QLineF(editor.preview->canvasPoint(editor.preview->widgetPoint(point)),point).length()<.00001,"nested group scene picking agrees with preview");
        click(editor,"showSceneContext");check(!editor.preview->sceneContext()&&editor.preview->viewState().zoom==view.zoom&&editor.preview->viewState().pan==view.pan,"leaving scene view restores isolated viewport");
        picker->setCurrentIndex(picker->findData(uuid));click(editor,"showSceneContext");obs_source_remove(obs_scene_get_source(scene));pump();check(!editor.preview->sceneContext()&&!editor.findChild<QPushButton*>("showSceneContext")->isChecked(),"removed reference scene returns safely to isolated view");editor.reject();
    }
    obs_source_remove(obs_scene_get_source(nested));obs_scene_release(nested);obs_source_release(groupSource);obs_scene_release(scene);obs_source_release(cold);obs_source_release(source);obs_source_release(background);pump();
}
static void testRepeatSize(obs_source_t *target,const QString &output){
    auto *settings=obs_data_create();auto *original=obs_source_get_settings(target);obs_data_set_string(settings,"input_uuid",obs_data_get_string(original,"input_uuid"));obs_data_release(original);auto *source=obs_source_create_private("obs_screen_mapper","Repeat width QA",settings);obs_data_release(settings);
    {
        MapperEditor editor(source);editor.show();pump();editor.findChild<QComboBox*>("distribution")->setCurrentIndex(1);editor.zoneAction("add");editor.select(1);pump();
        check(editor.workingLayout().zones[1].destination.x==.5f&&editor.workingLayout().zones[1].destination.w==.5f,"Repeat starts with two automatically distributed half-width zones");
        editor.findChild<QDoubleSpinBox*>("dstW")->setValue(100);pump();auto model=editor.workingLayout();check(!model.automatic&&model.zones[1].destination.x==.5f&&model.zones[1].destination.w==1&&editor.findChild<QDoubleSpinBox*>("dstW")->value()==100,"second Repeat zone accepts 100 percent width independently of its base X");
        check(model.zones[0].destination.w==.5f,"manual Repeat resizing leaves other zones unchanged");
        click(editor,"undoEdit");check(editor.workingLayout().zones[1].destination.w==.5f,"undo restores automatic half-width Repeat layout");click(editor,"redoEdit");check(editor.workingLayout().zones[1].destination.w==1,"redo restores full-width Repeat zone");
        QString error;const auto file=output+"/repeat-full-width.mapper.json";check(editor.exportPreset(file,"Repeat ancho completo",error),"full-width offset destination exports in preset");MapperPresets store(output+"/repeat-size-cache");QJsonObject restored;QString name;check(store.materialize(MapperPresets::readFile(file,error),restored,name,error),"preset accepts destination extending outside canvas");auto *roundTrip=obs_data_create_from_json(QJsonDocument(restored).toJson(QJsonDocument::Compact).constData());check(mapping::read(roundTrip).zones[1].destination.w==1,"preset round trip preserves full Repeat width");obs_data_release(roundTrip);
        editor.findChild<QDoubleSpinBox*>("transform_x")->setValue(-50);click(editor,"applyEditor");editor.reject();
    }
    {
        MapperEditor editor(source);check(editor.workingLayout().zones[1].destination.w==1&&editor.findChild<QDoubleSpinBox*>("dstW")->value()==100,"full Repeat width survives Apply and editor reopening");editor.reject();
    }
    settings=obs_source_get_settings(source);auto *props=obs_source_properties(source);obs_data_set_double(settings,"zone_dst_y",.5);obs_data_set_double(settings,"zone_dst_h",1);obs_property_modified(obs_properties_get(props,"zone_dst_h"),settings);auto model=mapping::read(settings);check(model.zones[1].destination.h==1&&model.zones[1].destination.y==.5f,"native properties allow full height independently of base Y");obs_properties_destroy(props);obs_data_release(settings);
    auto *rect=obs_data_create();obs_data_set_double(rect,"uv_x",.75);obs_data_set_double(rect,"uv_w",1);check(mapping::read_rect(rect,"uv_").w==.25f,"UV regions remain limited to actual source image");obs_data_release(rect);obs_source_release(source);
}
static void testMaskDomain(obs_source_t *target,const QString &output){
    auto *settings=obs_data_create();auto *original=obs_source_get_settings(target);obs_data_set_string(settings,"input_uuid",obs_data_get_string(original,"input_uuid"));obs_data_release(original);
    mapping::Layout layout;mapping::Zone zone;zone.id="1";zone.mask.kind=1;zone.mask.center_x=.6f;zone.mask.center_y=.45f;zone.mask.width=.8f;zone.mask.height=.6f;zone.mask.radius=.4f;zone.mesh.enabled=true;mapping::resize_mesh(zone.mesh,9,9);zone.polygon.enabled=true;zone.uv={.1f,.1f,.7f,.8f};layout.zones.push_back(zone);mapping::write(settings,layout);
    auto *source=obs_source_create_private("obs_screen_mapper","Mask domain QA",settings);obs_data_release(settings);
    {
        MapperEditor editor(source);editor.show();pump();editor.findChild<QCheckBox*>("showGrid")->setChecked(false);editor.findChild<QCheckBox*>("showZones")->setChecked(false);pump();const auto before=editor.preview->renderFrame();
        editor.findChild<QTabWidget*>("zoneTabs")->setCurrentIndex(2);click(editor,"fitMeshToMask");auto mesh=editor.workingLayout().zones[0].mesh;
        check(mesh.domain==1&&mesh.columns==9&&mesh.rows==9,"mask adaptation creates an editable ellipse control domain");
        check(editor.preview->renderFrame()==before,"adapting controls preserves exact GPU video pixels and UV framing");
        editor.findChild<QCheckBox*>("showZones")->setChecked(true);pump();auto shot=editor.grab();auto frame=editor.preview->renderFrame();{QPainter painter(&shot);painter.drawImage(QRect(editor.preview->mapTo(&editor,QPoint()),editor.preview->size()),frame);}check(shot.save(output+"/adapted-mask-mesh.png"),"adapted editable mesh artifact saved");editor.findChild<QCheckBox*>("showZones")->setChecked(false);
        bool positions=true,inverse=true;for(int y=0;y<9;++y)for(int x=0;x<9;++x){const auto p=mapping::control_base(mesh,x/8.f,y/8.f);const float dx=(p.x-.6f)/.32f,dy=(p.y-.45f)/.24f;positions&=dx*dx+dy*dy<1.00001f;const auto q=mapping::domain_parameter(mesh,p.x,p.y);inverse&=std::abs(q.x-x/8.f)<.002f&&std::abs(q.y-y/8.f)<.002f;}
        check(positions&&inverse,"all ellipse controls lie inside cutout and have matching interpolation coordinates");
        editor.findChild<QCheckBox*>("showZones")->setChecked(true);const auto a=editor.preview->widgetPoint({.6,.45}),b=editor.preview->widgetPoint({.63,.45});drag(editor.preview,a,b);mesh=editor.workingLayout().zones[0].mesh;
        check(mesh.points[40].x>.029f,"adapted center control deforms the cutout on subsequent drag");
        click(editor,"fitMeshToMask");check(editor.workingLayout().zones[0].mesh.points[40].x==mesh.points[40].x,"readapting never discards existing manual deformation");
        editor.findChild<QCheckBox*>("showZones")->setChecked(false);pump();check(editor.preview->renderFrame()!=before,"subsequent mesh edit changes GPU output");
        click(editor,"undoEdit");editor.findChild<QCheckBox*>("showZones")->setChecked(false);check(editor.workingLayout().zones[0].mesh.domain==1&&editor.preview->renderFrame()==before,"undo restores adapted unwarped image");
        click(editor,"undoEdit");check(editor.workingLayout().zones[0].mesh.domain==0,"mask adaptation is one undoable operation");click(editor,"redoEdit");
        QString error;check(editor.exportPreset(output+"/mask-domain.mapper.json","Malla recortada",error),"adapted control domain exports in portable preset");
        MapperPresets store(output+"/domain-preset-cache");QJsonObject restored;QString name;check(store.materialize(MapperPresets::readFile(output+"/mask-domain.mapper.json",error),restored,name,error),"adapted preset validates and materializes");auto *roundTrip=obs_data_create_from_json(QJsonDocument(restored).toJson(QJsonDocument::Compact).constData());check(mapping::read(roundTrip).zones[0].mesh.domain==1&&mapping::read(roundTrip).zones[0].mesh.center_x==.6f,"preset preserves adapted controls and domain");obs_data_release(roundTrip);
        editor.findChild<QTabWidget*>("zoneTabs")->setCurrentIndex(2);editor.findChild<QComboBox*>("maskType")->setCurrentIndex(2);editor.findChild<QDoubleSpinBox*>("mask_center_x")->setValue(50);editor.findChild<QDoubleSpinBox*>("mask_center_y")->setValue(50);editor.findChild<QDoubleSpinBox*>("mask_width")->setValue(60);editor.findChild<QDoubleSpinBox*>("mask_height")->setValue(40);pump();const auto rectangle=editor.preview->renderFrame();click(editor,"fitMeshToMask");mesh=editor.workingLayout().zones[0].mesh;
        const auto first=mapping::control_base(mesh,0,0),last=mapping::control_base(mesh,1,1);check(mesh.domain==2&&std::abs(first.x-.2f)<1e-6f&&std::abs(first.y-.3f)<1e-6f&&std::abs(last.x-.8f)<1e-6f&&std::abs(last.y-.7f)<1e-6f&&editor.preview->renderFrame()==rectangle,"rectangle controls follow crop bounds without stretching video");
        click(editor,"applyEditor");auto *saved=obs_source_get_settings(source);check(mapping::read(saved).zones[0].mesh.domain==2,"adapted domain persists in OBS settings");obs_data_release(saved);editor.reject();
    }
    {
        MapperEditor editor(source);check(editor.workingLayout().zones[0].mesh.domain==2,"adapted editable mesh survives editor reopening");editor.reject();
    }
    auto mesh=zone.mesh;mesh.domain=1;mesh.center_x=0;mesh.axis_x=.6f;mesh.axis_y=.4f;bool clipped=true;for(int y=0;y<9;++y)for(int x=0;x<9;++x){const auto p=mapping::control_base(mesh,x/8.f,y/8.f);clipped&=p.x>=-1e-6&&p.x<=1.000001&&p.y>=-1e-6&&p.y<=1.000001;const auto q=mapping::domain_parameter(mesh,p.x,p.y);clipped&=std::abs(q.x-x/8.f)<.003f&&std::abs(q.y-y/8.f)<.003f;}check(clipped,"partially cropped ellipse domain stays inside source and remains editable");
    obs_source_release(source);
}
static void testSimplifiedEditor(obs_source_t *target) {
    auto *settings=obs_data_create();auto *original=obs_source_get_settings(target);obs_data_set_string(settings,"input_uuid",obs_data_get_string(original,"input_uuid"));obs_data_release(original);
    auto *source=obs_source_create_private("obs_screen_mapper","Simplified editor QA",settings);obs_data_release(settings);
    {
        MapperEditor editor(source);editor.show();pump();auto *tabs=editor.findChild<QTabWidget*>("zoneTabs");
        check(tabs->currentIndex()==0&&!editor.findChild<QDoubleSpinBox*>("transform_pivot_x")->isVisible(),"position opens with advanced controls collapsed");
        for(const char *name:{"perspectiveSection","transformAdvanced"}){auto *toggle=editor.findChild<QPushButton*>(name);check(toggle->parentWidget()->layout()->indexOf(toggle)>=0&&toggle->height()<80,"fold section belongs to form and has compact geometry");}
        tabs->setCurrentIndex(1);click(editor,"tool2");click(editor,"tool1");check(editor.workingLayout().zones[0].mesh.enabled&&editor.workingLayout().zones[0].polygon.enabled,"contextual shape tools keep Polygon and Mesh combinable");
        tabs->setCurrentIndex(2);editor.findChild<QComboBox*>("maskType")->setCurrentIndex(1);editor.findChild<QDoubleSpinBox*>("mask_radius")->setValue(25);pump();
        auto start=editor.preview->widgetPoint({.5,.5}),finish=editor.preview->widgetPoint({.55,.5});drag(editor.preview,start,finish);check(editor.workingLayout().zones[0].mask.center_x>.54f,"mask tab activates direct center dragging");
        click(editor,"undoEdit");check(std::abs(editor.workingLayout().zones[0].mask.center_x-.5f)<.001f,"mask gesture is one undo step");
        start=editor.preview->widgetPoint({.75,.75});finish=editor.preview->widgetPoint({.8,.8});drag(editor.preview,start,finish);check(editor.workingLayout().zones[0].mask.radius>.29f,"mask outer handle adjusts ellipse radius");
        tabs->setCurrentIndex(3);editor.findChild<QDoubleSpinBox*>("uvW")->setValue(50);editor.findChild<QDoubleSpinBox*>("uvH")->setValue(50);editor.findChild<QDoubleSpinBox*>("uvX")->setValue(25);pump();
        start=editor.preview->widgetPoint({.5,.5});finish=editor.preview->widgetPoint({.55,.5});drag(editor.preview,start,finish);check(editor.workingLayout().mode==mapping::Mode::Custom&&editor.workingLayout().zones[0].uv.x<.24f,"video tab reframes UV without moving zone");
        const auto before=editor.preview->viewState().pan;const auto model=editor.workingLayout().zones[0].destination;
        QKeyEvent press(QEvent::KeyPress,Qt::Key_Space,Qt::NoModifier);QApplication::sendEvent(editor.preview,&press);drag(editor.preview,start,start+QPointF(30,20));QKeyEvent release(QEvent::KeyRelease,Qt::Key_Space,Qt::NoModifier);QApplication::sendEvent(editor.preview,&release);
        check(editor.preview->viewState().pan!=before&&editor.workingLayout().zones[0].destination.x==model.x,"Space drag pans without modifying zone");
        click(editor,"tool3");tabs->setCurrentIndex(0);check(!editor.findChild<QPushButton*>("tool3")->isChecked(),"changing tab exits hand tool");
        editor.preview->fit();pump();start=editor.preview->widgetPoint({1,1});finish=editor.preview->widgetPoint({1.05,1.05});drag(editor.preview,start,finish);check(editor.workingLayout().zones[0].transform.scale>1.09f,"position corner handle scales the zone");
        click(editor,"undoEdit");check(std::abs(editor.workingLayout().zones[0].transform.scale-1)<.001f,"scale gesture is one undo step");
        start=editor.preview->widgetPoint({.5,-.12});finish=editor.preview->widgetPoint({.55,-.12});drag(editor.preview,start,finish);check(editor.workingLayout().zones[0].transform.rz>4,"position upper handle rotates the zone");
        editor.reject();
    }
    obs_source_release(source);
}
static void testSourceSections(obs_source_t *target) {
    auto *a=obs_source_create("obs_screen_mapper","alpha selector QA",nullptr,nullptr);
    auto *z=obs_source_create("obs_screen_mapper","Zulu selector QA",nullptr,nullptr);
    auto *group=obs_source_create("group","Group selector QA",nullptr,nullptr);
    auto *scene=obs_scene_create("Scene selector QA");
    {
        MapperEditor editor(target);auto *combo=editor.findChild<QComboBox*>("inputSource");
        auto index=[&](obs_source_t *source){return combo->findData(QString::fromUtf8(obs_source_get_uuid(source)));};
        const int ai=index(a),zi=index(z),gi=index(group),si=index(obs_scene_get_source(scene));
        check(ai>=0&&zi>ai,"source selector sorts alphabetically ignoring case");
        const int sh=combo->findData(QString("__mapper_section_0")),gh=combo->findData(QString("__mapper_section_1")),ch=combo->findData(QString("__mapper_section_2"));
        check(sh>=0&&ai>sh&&gh>zi&&gi>gh&&ch>gi&&si>ch,"selector separates sources, groups and scenes");
        check(!(combo->model()->flags(combo->model()->index(sh,0))&Qt::ItemIsEnabled),"source headings cannot be selected");
        editor.reject();
    }
    obs_source_remove(a);obs_source_release(a);obs_source_remove(z);obs_source_release(z);obs_source_remove(group);obs_source_release(group);obs_source_remove(obs_scene_get_source(scene));obs_scene_release(scene);
}
static void testApplyAccept(obs_source_t *target)
{
    auto *original=obs_source_get_settings(target);auto *settings=obs_data_create();obs_data_set_string(settings,"input_uuid",obs_data_get_string(original,"input_uuid"));obs_data_release(original);
    auto *source=obs_source_create_private("obs_screen_mapper","Commit behavior fixture",settings);obs_data_release(settings);
    auto position=[&]{auto *state=obs_source_get_settings(source);const float value=mapping::read(state).zones[0].transform.x;obs_data_release(state);return value;};
    {
        MapperEditor editor(source);editor.show();pump();editor.findChild<QDoubleSpinBox*>("transform_x")->setValue(20);click(editor,"applyEditor");pump();
        check(editor.isVisible()&&position()==.2f,"Apply commits to OBS while keeping the editor open");
        editor.findChild<QDoubleSpinBox*>("transform_x")->setValue(40);pump();check(position()==.2f,"edits after Apply remain in working copy");
        click(editor,"cancelEditor");check(!editor.isVisible()&&position()==.2f,"Cancel preserves last Apply and discards subsequent edits");
    }
    {
        MapperEditor editor(source);editor.show();pump();editor.findChild<QDoubleSpinBox*>("transform_x")->setValue(-30);click(editor,"acceptEditor");pump();
        check(!editor.isVisible()&&editor.result()==QDialog::Accepted&&position()==-.3f,"Accept commits current transform and closes the editor");
    }
    {
        MapperEditor editor(source);editor.show();pump();editor.findChild<QDoubleSpinBox*>("transform_x")->setValue(10);click(editor,"applyEditor");click(editor,"applyEditor");
        check(editor.isVisible()&&position()==.1f,"repeated Apply is safe and leaves editor open");editor.findChild<QDoubleSpinBox*>("transform_x")->setValue(60);editor.reject();pump();check(position()==.1f,"closing without Accept preserves only the last Apply");
    }
    {
        MapperEditor editor(source);editor.show();pump();obs_source_remove(source);click(editor,"acceptEditor");check(editor.isVisible()&&editor.result()!=QDialog::Accepted,"Accept stays open if source was deleted");editor.reject();
    }
    obs_source_release(source);
}
static void testTransformEditor(obs_source_t *target,const QString &output)
{
    auto *settings=obs_source_get_settings(target);const std::string before=obs_data_get_json(settings);obs_data_release(settings);
    {
        MapperEditor editor(target);editor.select(0);editor.show();pump();editor.findChild<QTabWidget*>()->setCurrentIndex(3);
        editor.findChild<QDoubleSpinBox*>("transform_x")->setValue(-25);editor.findChild<QDoubleSpinBox*>("transform_z")->setValue(35);editor.findChild<QDoubleSpinBox*>("transform_ry")->setValue(30);pump();
        check(editor.workingLayout().zones[0].transform.x==-.25f&&editor.workingLayout().zones[0].transform.z==.35f&&editor.workingLayout().zones[0].transform.ry==30,"transform controls set outside position, depth and rotation");
        click(editor,"undoEdit");check(editor.workingLayout().zones[0].transform.ry==0,"3D rotation is undoable");click(editor,"redoEdit");
        click(editor,"resetTransform");check(!mapping::transformed(editor.workingLayout().zones[0].transform),"reset transform preserves identity");
        click(editor,"tool4");const auto d=editor.workingLayout().zones[0].destination;
        const auto point=editor.preview->widgetPoint({d.x,d.y});drag(editor.preview,point,point+QPointF(15,10));
        check(editor.workingLayout().zones[0].transform.corners[0].x>0&&editor.workingLayout().zones[0].transform.corners[0].y>0,"four-corner handle drag changes projective quad");
        const auto changed=editor.workingLayout().zones[0].transform.corners[0].x;click(editor,"undoEdit");check(editor.workingLayout().zones[0].transform.corners[0].x==0,"corner drag is one undoable gesture");click(editor,"redoEdit");check(editor.workingLayout().zones[0].transform.corners[0].x==changed,"corner drag redo restores exact quad");
        QString error;check(editor.exportPreset(output+"/transform-roundtrip.mapper.json","3D",error),"transformed working copy exports portable preset");
        click(editor,"resetTransform");editor.findChild<QCheckBox*>("polygon")->setChecked(false);editor.findChild<QCheckBox*>("mesh")->setChecked(false);editor.findChild<QComboBox*>("maskType")->setCurrentIndex(0);
        for(const char *key:{"dstX","dstY"})editor.findChild<QDoubleSpinBox*>(key)->setValue(0);for(const char *key:{"dstW","dstH"})editor.findChild<QDoubleSpinBox*>(key)->setValue(100);
        editor.findChild<QDoubleSpinBox*>("transform_x")->setValue(80);editor.preview->setZoom(.5f);pump();const auto exterior=editor.preview->widgetPoint({1.1,.5});const auto frame=editor.preview->renderFrame();
        const int xx=int(exterior.x()*editor.preview->devicePixelRatioF()),yy=int(exterior.y()*editor.preview->devicePixelRatioF());check(frame.rect().contains(xx,yy)&&frame.pixelColor(xx,yy).blue()>65,"editor renders transformed video beyond canvas border");
        editor.select(1);editor.findChild<QTabWidget*>("zoneTabs")->setCurrentIndex(0);drag(editor.preview,exterior,exterior);check(editor.findChild<QListWidget*>("zoneList")->currentRow()==0,"zone outside canvas remains selectable in editor");editor.reject();
    }
    settings=obs_source_get_settings(target);check(before==obs_data_get_json(settings),"Cancel preserves original source after 3D and pin edits");obs_data_release(settings);
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
        editor.findChild<QDoubleSpinBox*>("mask_radius")->setValue(25);editor.findChild<QDoubleSpinBox*>("mask_center_x")->setValue(40);pump();
        check(editor.workingLayout().zones[1].mask.radius==.25f&&editor.workingLayout().zones[1].mask.center_x==.4f,"contextual circle radius and center update working mask");
        click(editor,"undoEdit");check(editor.workingLayout().zones[1].mask.center_x==.5f,"mask position edit is undoable");click(editor,"redoEdit");
        editor.findChild<QComboBox*>("maskType")->setCurrentIndex(2);editor.findChild<QDoubleSpinBox*>("mask_width")->setValue(60);editor.findChild<QDoubleSpinBox*>("mask_height")->setValue(45);pump();
        check(editor.workingLayout().zones[1].mask.width==.6f&&editor.workingLayout().zones[1].mask.height==.45f&&editor.findChild<QDoubleSpinBox*>("mask_radius")->isHidden(),"rectangle controls set dimensions and hide circle radius");
        click(editor,"resetMaskShape");check(editor.workingLayout().zones[1].mask.width==1&&editor.workingLayout().zones[1].mask.center_x==.5f,"reset mask shape restores size and position");
        editor.findChild<QComboBox*>("maskType")->setCurrentIndex(1);
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
    testTransformEditor(target,output);
    testApplyAccept(target);testSourceSections(target);testSimplifiedEditor(target);testSceneContext(target,output);testMaskDomain(target,output);testRepeatSize(target,output);
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
        editor.findChild<QTabWidget*>()->setCurrentIndex(1);editor.findChild<QComboBox*>("maskType")->setCurrentIndex(1);editor.findChild<QDoubleSpinBox*>("mask_radius")->setValue(30);editor.findChild<QDoubleSpinBox*>("mask_center_x")->setValue(40);pump();
        auto maskImage=editor.grab();const auto maskFrame=editor.preview->renderFrame();{QPainter painter(&maskImage);painter.drawImage(QRect(editor.preview->mapTo(&editor,QPoint()),editor.preview->size()),maskFrame);}
        check(maskImage.save(output+"/mask-shape-editor.png"),"adjustable circle mask editor artifact saved");
        if(argc>5 && std::string(argv[5])=="--interactive") editor.exec();else editor.reject();
    }
    const QString reference=QString::fromUtf8("C:/Projects/OBS Screen Mapper/obs-screen-mapper/docs/references/billboards-reference.png");
    if(QFile::exists(reference)){
        settings=obs_source_get_settings(target);mapping::Layout billboards;billboards.mode=mapping::Mode::Repeat;billboards.automatic=false;
        const float corners[2][8]={{318,76,691,129,690,248,304,204},{719,230,1042,162,1056,656,721,680}};
        for(int i=0;i<2;++i){mapping::Zone z;z.id=std::to_string(i+1);z.name=i==0?"Cartel horizontal":"Cartel vertical";z.transform.pinned=true;for(int k=0;k<4;++k)z.transform.corners[size_t(k)]={corners[i][k*2]/1391,corners[i][k*2+1]/720};billboards.zones.push_back(z);}
        mapping::write(settings,billboards);obs_data_set_string(settings,"distribution","repeat");obs_data_set_int(settings,"canvas_width",1391);obs_data_set_int(settings,"canvas_height",720);obs_source_update(target,settings);obs_data_release(settings);pump();
        MapperEditor editor(target);QString exportError;check(editor.exportPreset(output+"/carteles-dos-pantallas.mapper.json","Carteles - dos pantallas",exportError),"two-billboard geometry exported without embedded photo");editor.show();pump();check(editor.preview->setReference(reference),"billboard photo loaded only as editor reference");click(editor,"tool4");pump();
        auto shot=editor.grab();auto frame=editor.preview->renderFrame();{QPainter painter(&shot);painter.drawImage(QRect(editor.preview->mapTo(&editor,QPoint()),editor.preview->size()),frame);}check(shot.save(output+"/billboards-transform-editor.png"),"billboard four-corner editor render saved");editor.reject();
    }
    obs_source_release(target);obs_source_release(input);pump();
    obs_enter_graphics();gs_effect_destroy(fixtureEffect);fixtureEffect=nullptr;obs_leave_graphics();obs_shutdown();
    std::puts("PASS: native Qt phase 8 editor and preset validation complete");return 0;
}









