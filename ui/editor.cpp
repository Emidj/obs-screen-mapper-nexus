// SPDX-License-Identifier: GPL-2.0-or-later
#include "editor.hpp"
#include "../src/source-list.hpp"
#include <QApplication>
#include <QBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QScrollArea>
#include <QFileDialog>
#include <QFileInfo>
#include <QButtonGroup>
#include <QKeyEvent>
#include <QMessageBox>
#include <QAbstractItemModel>
#include <QFrame>
#include <QKeySequence>
#include <QSlider>
#include <QInputDialog>
#include <QToolButton>
#include <QMenu>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QLocale>
#include <QSignalBlocker>

namespace {
QString text(const std::string &value) { return QString::fromUtf8(value.c_str()); }
std::string utf8(const QString &value) { return value.toUtf8().toStdString(); }
QPushButton *button(const QString &label,const char *name,QBoxLayout *row) {
    auto *result=new QPushButton(label); result->setObjectName(name); result->setMinimumHeight(34);result->setAutoDefault(false); row->addWidget(result); return result;
}
QGroupBox *group(const QString &title,QVBoxLayout *column) { auto *result=new QGroupBox(title); column->addWidget(result); return result; }
QWidget *scrollPage(QTabWidget *tabs,const QString &title,QFormLayout *&form) {
    auto *scroll=new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto *body=new QWidget; form=new QFormLayout(body); form->setContentsMargins(10,12,10,12); form->setSpacing(10); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    scroll->setWidget(body); tabs->addTab(scroll,title); return body;
}
}
MapperEditor::MapperEditor(obs_source_t *source,QWidget *parent,QString presetDirectory):QDialog(parent),target(obs_source_get_ref(source)) {
    mapper_i18n::set_locale(obs_get_locale());
    setLocale(QLocale(QString::fromUtf8(obs_get_locale())));
    setObjectName("mapperEditor"); setWindowTitle(mapper_i18n::text("OBS Screen Mapper Editor")); setMinimumSize(1100,720); resize(1480,930);
    auto *original=obs_source_get_settings(target);
    settings=obs_data_create_from_json(obs_data_get_json(original));
    const char *mode=obs_data_get_string(original,"distribution");obs_data_set_string(settings,"distribution",mode&&*mode?mode:"split");
    obs_data_set_int(settings,"canvas_width",obs_source_get_width(target)); obs_data_set_int(settings,"canvas_height",obs_source_get_height(target));
    obs_data_release(original);
    layout=mapping::read(settings); selectedIndex=mapping::selection(settings,layout);
    const auto overrideDirectory=qEnvironmentVariable("OBS_MAPPER_PRESET_DIR");if(!overrideDirectory.isEmpty())presetDirectory=overrideDirectory;
    if(presetDirectory.isEmpty())presetDirectory=QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)+"/obs-screen-mapper/presets";
    presetStore=std::make_unique<MapperPresets>(presetDirectory);
    auto *previewSettings=obs_data_create_from_json(obs_data_get_json(settings));obs_data_set_bool(previewSettings,"_mapper_preview_unclipped",true);
    draft=obs_source_create_private("obs_screen_mapper","Mapper Editor Preview",previewSettings);obs_data_release(previewSettings);
    build();restoreWorkspace();refreshPresets(); refreshSources(); refresh(); publish();
    connect(&timer,&QTimer::timeout,this,[this]{if(auto scene=preview->sceneContext();scene&&obs_source_removed(scene->original)){sceneButton->setChecked(false);status->setText(mapper_i18n::text("La escena de referencia ya no está disponible."));}if(dirty)publish();if(auto scene=preview->sceneContext();scene&&(scene->mapperWidth!=obs_source_get_width(draft)||scene->mapperHeight!=obs_source_get_height(draft)))updateSceneContext();}); timer.start(33);
}
MapperEditor::~MapperEditor() { timer.stop(); obs_source_release(draft); obs_data_release(settings); obs_source_release(target); }
mapping::Zone *MapperEditor::selected() { return selectedIndex<layout.zones.size()?&layout.zones[selectedIndex]:nullptr; }
void MapperEditor::sync() {
    mapping::write(settings,layout);
    if(auto *zone=selected()) obs_data_set_string(settings,"selected_zone",zone->id.c_str());
    else obs_data_set_string(settings,"selected_zone","");
    if(preview) {
        const auto view=preview->sceneContext()?isolatedView:preview->viewState();const auto reference=preview->referencePath().isEmpty()?workspaceReference:preview->referencePath();
        QJsonObject editor{{"version",1},{"grid",checks["showGrid"]->isChecked()},{"zones",checks["showZones"]->isChecked()},
            {"zoom",view.zoom},{"pan_x",view.pan.x()},{"pan_y",view.pan.y()},{"tool",tool},{"vertex",double(vertexIndex)},{"control",double(meshIndex)},
            {"reference",reference},{"last_preset",activePreset},{"corner",int(cornerIndex)}};
        auto *preferences=obs_data_create_from_json(QJsonDocument(editor).toJson(QJsonDocument::Compact).constData());
        obs_data_set_obj(settings,"editor",preferences);obs_data_release(preferences);
    }
}
std::string MapperEditor::json() { sync(); return obs_data_get_json(settings); }
void MapperEditor::remember() { undo.push_back(json()); if(undo.size()>100) undo.erase(undo.begin()); redo.clear(); }
void MapperEditor::change(const std::function<void()> &fn,bool history) {
    if(loading) return;
    if(history && !gesture) remember(); fn(); mapping::arrange(layout); sync(); dirty=true; refresh();
}
void MapperEditor::beginGesture() { if(gesture) return; gestureBefore=json(); remember(); gesture=true; }
void MapperEditor::endGesture() { if(!gesture) return; gesture=false; if(json()==gestureBefore && !undo.empty()) undo.pop_back(); refresh(); }
void MapperEditor::restore(const std::string &snapshot) {
    obs_data_release(settings); settings=obs_data_create_from_json(snapshot.c_str()); layout=mapping::read(settings);
    selectedIndex=mapping::selection(settings,layout);restoreWorkspace();refreshPresets(); dirty=true; refresh();
}
void MapperEditor::undoEdit() { if(undo.empty()) return; auto previous=undo.back(); undo.pop_back(); redo.push_back(json()); restore(previous); }
void MapperEditor::redoEdit() { if(redo.empty()) return; auto next=redo.back(); redo.pop_back(); undo.push_back(json()); restore(next); }
void MapperEditor::select(size_t index) { if(index>=layout.zones.size()) return; selectedIndex=index; vertexIndex=meshIndex=0; sync(); refresh(); }
void MapperEditor::restoreWorkspace(bool restorePreset) {
    const auto root=QJsonDocument::fromJson(QByteArray(obs_data_get_json(settings))).object();const auto editor=root.value("editor").toObject();
    const bool previous=loading;loading=true;
    checks["showGrid"]->setChecked(editor.value("grid").toBool(true));checks["showZones"]->setChecked(editor.value("zones").toBool(true));
    tool=std::clamp(editor.value("tool").toInt(0),0,6);vertexIndex=size_t(std::clamp(editor.value("vertex").toInt(0),0,511));meshIndex=size_t(std::clamp(editor.value("control").toInt(0),0,255));cornerIndex=size_t(std::clamp(editor.value("corner").toInt(0),0,3));
    activateTool(tool);
    auto boundedView=[](double value,double min,double max,double fallback){return std::isfinite(value)?std::clamp(value,min,max):fallback;};
    MapperPreview::ViewState savedView{float(boundedView(editor.value("zoom").toDouble(1),.2,8,1)),{boundedView(editor.value("pan_x").toDouble(),-1000000,1000000,0),boundedView(editor.value("pan_y").toDouble(),-1000000,1000000,0)}};
    if(preview->sceneContext())isolatedView=savedView;else preview->restoreView(savedView.zoom,savedView.pan);
    workspaceReference=editor.value("reference").toString();workspaceWarning.clear();
    if(!preview->setReference(workspaceReference)){preview->setReference({});workspaceWarning=mapper_i18n::text("Falta el fondo de referencia; vuelve a elegirlo o quítalo.");}
    if(restorePreset)activePreset=editor.value("last_preset").toString();loading=previous;
}
void MapperEditor::refreshPresets() {
    QString error;presetEntries=presetStore->list(error);presetWarning=error;const bool previous=loading;loading=true;presetList->clear();presetList->addItem(mapper_i18n::text("Sin preset"),QString());
    for(const auto &entry:presetEntries)presetList->addItem(entry.name,entry.id);
    presetList->setCurrentIndex(std::max(0,presetList->findData(activePreset)));loading=previous;if(!error.isEmpty()&&status)status->setText(error);
}
QString MapperEditor::savePresetAs(const QString &name,QString &error) {
    sync();const auto bytes=MapperPresets::encode(settings,name,error);if(bytes.isEmpty())return {};
    const auto id=presetStore->save(bytes,{},error);if(id.isEmpty())return {};activePreset=id;sync();refreshPresets();return id;
}
bool MapperEditor::savePreset(QString &error) {
    auto found=std::find_if(presetEntries.begin(),presetEntries.end(),[&](const PresetEntry &entry){return entry.id==activePreset;});
    if(found==presetEntries.end()){error=mapper_i18n::text("Elige Guardar como para crear un preset.");return false;}
    sync();const auto bytes=MapperPresets::encode(settings,found->name,error);if(bytes.isEmpty())return false;
    if(presetStore->save(bytes,activePreset,error).isEmpty())return false;sync();refreshPresets();return true;
}
bool MapperEditor::applyPreset(const QByteArray &bytes,const QString &id,QString &error) {
    QJsonObject imported;QString name;if(!presetStore->materialize(bytes,imported,name,error))return false;
    // Merge only the portable payload: source UUID and unrelated settings remain current.
    auto current=QJsonDocument::fromJson(QByteArray(json().c_str())).object();
    for(auto entry=imported.begin();entry!=imported.end();++entry)current[entry.key()]=entry.value();
    const auto encoded=QJsonDocument(current).toJson(QJsonDocument::Compact);
    change([&]{obs_data_release(settings);settings=obs_data_create_from_json(encoded.constData());layout=mapping::read(settings);selectedIndex=mapping::selection(settings,layout);activePreset=id;restoreWorkspace(false);});
    refreshPresets();return true;
}
bool MapperEditor::loadPreset(const QString &id,QString &error) {
    const auto bytes=presetStore->load(id,error);if(bytes.isEmpty())return false;return applyPreset(bytes,id,error);
}
bool MapperEditor::exportPreset(const QString &path,const QString &name,QString &error) {
    sync();const auto bytes=MapperPresets::encode(settings,name,error);return !bytes.isEmpty()&&MapperPresets::writeFile(path,bytes,error);
}
QString MapperEditor::importPreset(const QString &path,QString &error) {
    const auto bytes=MapperPresets::readFile(path,error);if(bytes.isEmpty())return {};
    QJsonObject document;if(!MapperPresets::validate(bytes,document,error))return {};
    // Validate/cache all embedded images before recording a library entry.
    QJsonObject imported;QString name;if(!presetStore->materialize(bytes,imported,name,error))return {};
    const auto id=presetStore->save(bytes,{},error);if(id.isEmpty())return {};
    if(!applyPreset(bytes,id,error))return {};return id;
}
void MapperEditor::zoneAction(const std::string &operation) { change([&]{mapping::action(layout,selectedIndex,operation); vertexIndex=meshIndex=0;}); }
bool MapperEditor::validInput(const std::string &uuid) {
    if(uuid.empty()) return true;
    auto *candidate=obs_get_source_by_uuid(uuid.c_str());
    if(!candidate) return false;
    struct Scan { obs_source_t *target; bool cycle; } scan{target,candidate==target};
    obs_source_enum_full_tree(candidate,[](obs_source_t *,obs_source_t *child,void *opaque){auto *s=static_cast<Scan*>(opaque); if(child==s->target) s->cycle=true;},&scan);
    const bool valid=!scan.cycle&&!obs_source_removed(candidate)&&(obs_source_get_output_flags(candidate)&OBS_SOURCE_VIDEO);
    obs_source_release(candidate); return valid;
}
void MapperEditor::publish() {
    sync();auto *previewSettings=obs_data_create_from_json(obs_data_get_json(settings));obs_data_set_bool(previewSettings,"_mapper_preview_unclipped",!preview->sceneContext());obs_source_update(draft,previewSettings);obs_data_release(previewSettings);dirty=false;
    PreviewState state; state.layout=layout; state.selected=selectedIndex; state.tool=tool;state.point=tool==1?vertexIndex:tool==4?cornerIndex:meshIndex;
    state.width=uint32_t(obs_data_get_int(settings,"canvas_width")); state.height=uint32_t(obs_data_get_int(settings,"canvas_height"));
    state.grid=checks["showGrid"]->isChecked(); state.zones=checks["showZones"]->isChecked();
    preview->setState(std::move(state));
    if(auto scene=preview->sceneContext();scene&&!refreshingScene&&(scene->mapperWidth!=obs_source_get_width(draft)||scene->mapperHeight!=obs_source_get_height(draft)))updateSceneContext();
}
bool MapperEditor::commitEdit() {
    if(obs_source_removed(target)) { status->setText(mapper_i18n::text("La fuente original se ha eliminado. Cierra el editor sin aplicar.")); return false; }
    if(!validInput(obs_data_get_string(settings,"input_uuid"))) { status->setText(mapper_i18n::text("La entrada no está disponible o crea una referencia circular.")); return false; }
    sync(); obs_source_update(target,settings); return true;
}
void MapperEditor::applyEdit(){if(commitEdit())status->setText(mapper_i18n::text("Cambios aplicados en OBS. Puedes seguir editando."));}
void MapperEditor::acceptEdit(){if(commitEdit())accept();}
QDoubleSpinBox *MapperEditor::number(const char *key,double min,double max,double step) {
    auto *spin=new QDoubleSpinBox; spin->setObjectName(key); spin->setRange(min,max); spin->setDecimals(2); spin->setSingleStep(step); spin->setKeyboardTracking(false); spin->setSuffix(" %"); numbers[key]=spin; return spin;
}
QCheckBox *MapperEditor::check(const char *key,const QString &label) { auto *box=new QCheckBox(label); box->setObjectName(key); checks[key]=box; return box; }
void MapperEditor::refreshSources() {
    const bool previous=loading; loading=true; sources->clear(); sources->addItem(mapper_i18n::text("Ninguna · transparente"),QString());
    int category=-1;
    for(const auto &entry:mapper_sources::collect(target)) {
        if(category!=entry.category) {
            category=entry.category;
            sources->addItem(mapper_i18n::text(mapper_sources::label(category)),QString("__mapper_section_%1").arg(category));
            sources->setItemData(sources->count()-1,0,Qt::UserRole-1);
        }
        sources->addItem(QString::fromUtf8(entry.name.c_str()),QString::fromUtf8(entry.uuid.c_str()));
    }
    const auto uuid=QString::fromUtf8(obs_data_get_string(settings,"input_uuid")); int index=sources->findData(uuid);
    if(index<0) { sources->addItem(mapper_i18n::text("Entrada no disponible"),uuid); index=sources->count()-1; }
    sources->setCurrentIndex(index); loading=previous;refreshSceneChoices();
}
void MapperEditor::refreshSceneChoices(){
    const auto previous=scenePicker->currentData().toString();QSignalBlocker blocked(scenePicker);scenePicker->clear();
    QString preferred=previous;
    if(preferred.isEmpty())if(auto module=GetModuleHandleW(L"obs-frontend-api.dll")){using Current=obs_source_t *(*)(void);auto get=reinterpret_cast<Current>(GetProcAddress(module,"obs_frontend_get_current_preview_scene"));obs_source_t *current=get?get():nullptr;if(!current){get=reinterpret_cast<Current>(GetProcAddress(module,"obs_frontend_get_current_scene"));current=get?get():nullptr;}if(current){preferred=QString::fromUtf8(obs_source_get_uuid(current));obs_source_release(current);}}
    for(const auto &entry:mapper_sources::collect(target))if(entry.category==2){auto *source=obs_get_source_by_uuid(entry.uuid.c_str());if(source&&mapper_scene::contains(obs_scene_from_source(source),target))scenePicker->addItem(QString::fromUtf8(entry.name.c_str()),QString::fromUtf8(entry.uuid.c_str()));obs_source_release(source);}
    const int index=scenePicker->findData(preferred);scenePicker->setCurrentIndex(index>=0?index:scenePicker->count()?0:-1);
    sceneButton->setEnabled(scenePicker->count()>0);sceneButton->setToolTip(mapper_i18n::text(scenePicker->count()?"Ver las otras fuentes de la escena mientras editas este Mapper.":"Añade este Mapper a una escena de OBS para verla como referencia."));
    if(sceneButton->isChecked())updateSceneContext();
}
void MapperEditor::updateSceneContext(){
    if(!sceneButton->isChecked()||refreshingScene)return;refreshingScene=true;publish();
    auto *source=obs_get_source_by_uuid(scenePicker->currentData().toString().toUtf8().constData());QString error;
    auto context=source&&!obs_source_removed(source)?mapper_scene::create(obs_scene_from_source(source),target,draft,error):nullptr;obs_source_release(source);
    if(!context){if(error.isEmpty())error=mapper_i18n::text("La escena de referencia ya no está disponible.");sceneButton->setChecked(false);status->setText(error);refreshingScene=false;return;}
    const auto previous=preview->sceneContext();const bool fit=!previous||previous->original!=context->original;
    preview->setSceneContext(std::move(context));if(fit)preview->fit();publish();refresh();refreshingScene=false;
}
void MapperEditor::refresh() {
    loading=true;
    distribution->setCurrentIndex(layout.mode==mapping::Mode::Repeat?1:layout.mode==mapping::Mode::Custom?2:0);
    const auto input=QString::fromUtf8(obs_data_get_string(settings,"input_uuid")); sources->setCurrentIndex(std::max(0,sources->findData(input)));
    canvasWidth->setValue(int(obs_data_get_int(settings,"canvas_width"))); canvasHeight->setValue(int(obs_data_get_int(settings,"canvas_height")));
    zones->clear();
    for(size_t i=0;i<layout.zones.size();++i) {
        const auto &zone=layout.zones[i]; auto *item=new QListWidgetItem(text(zone.name),zones);
        item->setData(Qt::UserRole,text(zone.id)); item->setFlags(item->flags()|Qt::ItemIsUserCheckable|Qt::ItemIsEditable|Qt::ItemIsDragEnabled);
        item->setCheckState(zone.visible?Qt::Checked:Qt::Unchecked); item->setSizeHint(QSize(0,45));
    }
    if(selectedIndex>=layout.zones.size()) selectedIndex=layout.zones.empty()?0:layout.zones.size()-1;
    zones->setCurrentRow(layout.zones.empty()?-1:int(selectedIndex));
    auto *zone=selected(); tabs->setEnabled(zone!=nullptr); zoneName->setEnabled(zone!=nullptr); checks["visible"]->setEnabled(zone!=nullptr);
    if(zone) {
        for(const auto &field:mapping::transform_fields)numbers[field.key]->setValue(zone->transform.*(field.member)*(field.key[10]=='r'?1:100));
        checks["cornerPin"]->setChecked(zone->transform.pinned);cornerList->setCurrentIndex(int(cornerIndex));numbers["cornerX"]->setValue(zone->transform.corners[cornerIndex].x*100);numbers["cornerY"]->setValue(zone->transform.corners[cornerIndex].y*100);
        zoneName->setText(text(zone->name)); checks["visible"]->setChecked(zone->visible);
        const auto d=zone->destination,u=zone->uv;
        numbers["dstX"]->setValue(d.x*100);numbers["dstY"]->setValue(d.y*100);numbers["dstW"]->setValue(d.w*100);numbers["dstH"]->setValue(d.h*100);
        numbers["uvX"]->setValue(u.x*100);numbers["uvY"]->setValue(u.y*100);numbers["uvW"]->setValue(u.w*100);numbers["uvH"]->setValue(u.h*100);
        checks["polygon"]->setChecked(zone->polygon.enabled); checks["mesh"]->setChecked(zone->mesh.enabled);
        vertices->clear(); for(size_t i=0;i<zone->polygon.vertices.size();++i) vertices->addItem(mapper_i18n::text("Vértice ")+QString::number(i+1));
        vertexIndex=zone->polygon.vertices.empty()?0:std::min(vertexIndex,zone->polygon.vertices.size()-1); vertices->setCurrentIndex(int(vertexIndex));
        if(!zone->polygon.vertices.empty()) {
            auto p=zone->polygon.vertices[vertexIndex]; numbers["vertexX"]->setValue(p.x*100);numbers["vertexY"]->setValue(p.y*100);numbers["vertexU"]->setValue(p.u*100);numbers["vertexV"]->setValue(p.v*100);
        }
        columns->setValue(zone->mesh.columns); rows->setValue(zone->mesh.rows); checks["smooth"]->setChecked(zone->mesh.smooth);
        checks["soft"]->setChecked(zone->mesh.soft); checks["wave"]->setChecked(zone->mesh.wave);
        numbers["radius"]->setValue(zone->mesh.radius*100);numbers["strength"]->setValue(zone->mesh.strength*100);
        numbers["curveH"]->setValue(zone->mesh.horizontal*100);numbers["curveV"]->setValue(zone->mesh.vertical*100);
        meshPoints->clear(); for(size_t i=0;i<zone->mesh.points.size();++i) meshPoints->addItem(QString(mapper_i18n::text("Fila %1 · columna %2")).arg(i/zone->mesh.columns+1).arg(i%zone->mesh.columns+1));
        meshIndex=zone->mesh.points.empty()?0:std::min(meshIndex,zone->mesh.points.size()-1); meshPoints->setCurrentIndex(int(meshIndex));
        if(!zone->mesh.points.empty()) { numbers["meshX"]->setValue(zone->mesh.points[meshIndex].x*100); numbers["meshY"]->setValue(zone->mesh.points[meshIndex].y*100); }
        maskKind->setCurrentIndex(std::clamp(zone->mask.kind,0,3)); maskPath->setText(text(zone->mask.path));
        checks["luminance"]->setChecked(zone->mask.luminance); checks["invert"]->setChecked(zone->mask.invert);
        numbers["maskStrength"]->setValue(zone->mask.strength*100);numbers["feather"]->setValue(zone->mask.feather*100);
        maskPath->setEnabled(zone->mask.kind==3); checks["luminance"]->setEnabled(zone->mask.kind==3);
        numbers["feather"]->setEnabled(zone->mask.kind==1||zone->mask.kind==2);
        for(const auto &field:mapping::mask_dimensions){numbers[field.key]->setValue(zone->mask.*(field.member)*100);numbers[field.key]->setEnabled((zone->mask.kind==1||zone->mask.kind==2)&&(std::string(field.key)!="mask_radius"||zone->mask.kind==1));}
        const bool basicMask=zone->mask.kind==1||zone->mask.kind==2;
        for(const auto &field:mapping::mask_dimensions)maskForm->setRowVisible(numbers[field.key],basicMask&&(std::string(field.key)!="mask_radius"||zone->mask.kind==1));
        maskForm->setRowVisible(findChild<QPushButton*>("resetMaskShape"),basicMask);
        for(auto *widget:{static_cast<QWidget*>(maskPath),static_cast<QWidget*>(checks["luminance"]),static_cast<QWidget*>(findChild<QPushButton*>("browseMask")),static_cast<QWidget*>(findChild<QPushButton*>("reloadMask"))})maskForm->setRowVisible(widget,zone->mask.kind==3);
        for(const char *key:{"vertexX","vertexY","vertexU","vertexV"}) numbers[key]->setEnabled(zone->polygon.enabled);
        for(const char *key:{"meshX","meshY","curveH","curveV","radius","strength"}) numbers[key]->setEnabled(zone->mesh.enabled);
        const bool showPolygon=zone->polygon.enabled&&tool==1,showMesh=zone->mesh.enabled&&tool==2;
        geometryForm->setRowVisible(vertices,showPolygon);geometryForm->setRowVisible(vertexActionRow,showPolygon);
        for(const char *key:{"vertexX","vertexY","vertexU","vertexV"})polygonNumbersForm->setRowVisible(numbers[key],showPolygon);
        for(auto *widget:{static_cast<QWidget*>(columns),static_cast<QWidget*>(rows),static_cast<QWidget*>(checks["smooth"]),static_cast<QWidget*>(checks["wave"]),static_cast<QWidget*>(checks["soft"])})geometryForm->setRowVisible(widget,showMesh);
        meshNumbersForm->setRowVisible(meshPoints,showMesh&&tool==2);meshNumbersForm->setRowVisible(numbers["meshX"],showMesh&&tool==2);meshNumbersForm->setRowVisible(numbers["meshY"],showMesh&&tool==2);
        for(const char *key:{"curveH","curveV","radius","strength"}) { auto *row=numbers[key]->parentWidget();geometryForm->setRowVisible(row,showMesh); }
        for(const char *name:{"polygonAdvanced","meshAdvanced"}){const bool show=std::string(name)=="polygonAdvanced"?showPolygon:showMesh;auto *toggle=findChild<QPushButton*>(name);geometryForm->setRowVisible(toggle,show);geometryForm->setRowVisible(findChild<QWidget*>(QString(name)+"Body"),show&&toggle->isChecked());}
        geometryForm->setRowVisible(findChild<QPushButton*>("resetMesh"),showMesh);
        for(const char *key:{"radius","strength"})geometryForm->setRowVisible(numbers[key]->parentWidget(),showMesh&&zone->mesh.soft);
        auto polygon=zone->polygon; mapping::compile_polygon(polygon); auto mesh=zone->mesh; auto surface=mapping::compile_surface(mesh,polygon);
        QString message=QString(mapper_i18n::text("%1 zonas · %2 × %3 · salida transparente")).arg(layout.zones.size()).arg(canvasWidth->value()).arg(canvasHeight->value());
        if(!polygon.error.empty()) message=text(polygon.error)+mapper_i18n::text(" La zona queda transparente.");
        else if(!surface.error.empty()) message=text(surface.error)+mapper_i18n::text(" La zona queda transparente.");
        else if(zone->mask.kind==3 && !QFileInfo::exists(text(zone->mask.path))) message=mapper_i18n::text("Falta el archivo de máscara; la zona queda transparente.");
        if(!mapping::valid_transform(zone->transform))message=mapper_i18n::text("Transformación no válida; revisa las cuatro esquinas.");
                if(auto scene=preview->sceneContext()){message+=QString(mapper_i18n::text(" · Escena: %1")).arg(QString::fromUtf8(obs_source_get_name(scene->original)));if(scene->occurrences>1)message+=QString(mapper_i18n::text(" · %1 apariciones: editas todas; tiradores en la primera visible.")).arg(scene->occurrences);if(!scene->visible)message+=mapper_i18n::text(" · El Mapper está oculto en esta escena.");if(scene->omittedFilters)message+=mapper_i18n::text(" · Los filtros de escena y grupo se omiten solo en esta vista; la salida de OBS los conserva.");}
        status->setText(!workspaceWarning.isEmpty()?workspaceWarning:!presetWarning.isEmpty()?presetWarning:message);
    } else { zoneName->clear(); status->setText(mapper_i18n::text("Sin zonas · añade una para comenzar.")); }
    undoButton->setEnabled(!undo.empty()); redoButton->setEnabled(!redo.empty()); loading=false;
    PreviewState state; state.layout=layout; state.selected=selectedIndex;state.tool=tool;state.point=tool==1?vertexIndex:tool==4?cornerIndex:meshIndex;state.width=uint32_t(canvasWidth->value());state.height=uint32_t(canvasHeight->value());
    state.grid=checks["showGrid"]->isChecked();state.zones=checks["showZones"]->isChecked(); preview->setState(std::move(state));
}
void MapperEditor::activateTool(int value,bool enable) {
    tool=value;if(value==1||value==2)shapeTool=value;
    const bool previous=loading;loading=true;
    if(value!=3)tabs->setCurrentIndex(value==1||value==2?1:value==5?2:value==6?3:0);
    for(int i=0;i<5;++i)if(auto *b=findChild<QPushButton*>(QString("tool%1").arg(i)))b->setChecked(i==value);
    if(value==4)findChild<QPushButton*>("perspectiveSection")->setChecked(true);
    loading=previous;
    if(enable&&selected())change([&]{if(value==1)selected()->polygon.enabled=true;else if(value==2)selected()->mesh.enabled=true;else if(value==4)selected()->transform.pinned=true;});
    else if(!loading)refresh();
}
void MapperEditor::build() {
    setStyleSheet(R"CSS(
QDialog,QWidget{background:#09131d;color:#e5f0fa;font-family:'Segoe UI';font-size:12px;}
QGroupBox{background:#0c1a26;border:1px solid #243c50;border-radius:7px;margin-top:10px;padding:14px 8px 8px;font-weight:600;}
QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 4px;}
QPushButton{background:#172a3b;border:1px solid #304d66;border-radius:5px;padding:6px 9px;}
QPushButton:hover{background:#203e56;border-color:#23a6ff;} QPushButton:checked,QPushButton#acceptEditor{background:#007cdf;border:1px solid #1ebcff;color:white;}
QPushButton:disabled{color:#647b8b;background:#10202d;}
QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox{background:#07121d;border:1px solid #2b455b;border-radius:4px;padding:5px;min-height:22px;}
QListWidget{background:#0a1723;border:1px solid #263f55;border-radius:5px;outline:none;}
QListWidget::item{padding:6px;border-bottom:1px solid #1b3041;} QListWidget::item:selected{background:#075a9d;color:white;}
QTabWidget::pane{border:1px solid #243e54;} QTabBar::tab{background:#152735;padding:10px 6px;}QTabBar::tab:selected{background:#075a9d;}
QScrollBar:vertical{background:#0b1721;width:9px;} QScrollBar::handle:vertical{background:#36546c;min-height:30px;border-radius:4px;}
QLabel#muted{color:#a1bed2;} QLabel#title{font-size:25px;font-weight:700;} QLabel#brand{color:#16b8ff;border:1px solid #7a44e7;border-radius:4px;padding:3px 7px;}
)CSS");
    auto *root=new QVBoxLayout(this); root->setContentsMargins(14,12,14,12); root->setSpacing(10);
    auto *header=new QHBoxLayout; auto *title=new QLabel("OBS Screen Mapper");title->setObjectName("title");header->addWidget(title);
    auto *brand=new QLabel("NEXUS");brand->setObjectName("brand");header->addWidget(brand); header->addStretch();
    header->addWidget(new QLabel(mapper_i18n::text("Preset:")));presetList=new QComboBox;presetList->setObjectName("presetList");presetList->setMinimumWidth(170);presetList->setMaximumWidth(240);header->addWidget(presetList);
    auto *save=button(mapper_i18n::text("Guardar"),"savePreset",header),*load=button(mapper_i18n::text("Cargar"),"loadPreset",header);
    auto *options=new QToolButton;options->setText(mapper_i18n::text("Presets ⋯"));options->setObjectName("presetOptions");options->setPopupMode(QToolButton::InstantPopup);auto *menu=new QMenu(options);options->setMenu(menu);header->addWidget(options);
    auto *saveAs=menu->addAction(mapper_i18n::text("Guardar como…")),*importAction=menu->addAction(mapper_i18n::text("Importar…")),*exportAction=menu->addAction(mapper_i18n::text("Exportar…"));
    auto promptSave=[this]{bool ok=false;auto name=QInputDialog::getText(this,mapper_i18n::text("Guardar preset"),mapper_i18n::text("Nombre del preset"),QLineEdit::Normal,mapper_i18n::text("Mi preset"),&ok);if(!ok)return;QString error;if(savePresetAs(name,error).isEmpty())status->setText(error);else status->setText(mapper_i18n::text("Preset guardado en la biblioteca local."));};
    connect(saveAs,&QAction::triggered,this,promptSave);
    connect(save,&QPushButton::clicked,this,[this,promptSave]{const auto id=presetList->currentData().toString();if(id.isEmpty()){promptSave();return;}const auto previous=activePreset;activePreset=id;QString error;if(!savePreset(error)){activePreset=previous;status->setText(error);}else status->setText(mapper_i18n::text("Preset guardado."));});
    connect(load,&QPushButton::clicked,this,[this]{QString error;const auto id=presetList->currentData().toString();if(id.isEmpty()){status->setText(mapper_i18n::text("Elige un preset de la biblioteca."));return;}if(!loadPreset(id,error))status->setText(error);else status->setText(mapper_i18n::text("Preset cargado en la copia de trabajo. La fuente de entrada se mantiene; usa Aplicar para confirmar."));});
    connect(importAction,&QAction::triggered,this,[this]{auto path=QFileDialog::getOpenFileName(this,mapper_i18n::text("Importar preset"),{},mapper_i18n::text("Preset Mapper (*.mapper.json *.json)"));if(path.isEmpty())return;QString error;if(importPreset(path,error).isEmpty())status->setText(error);else status->setText(mapper_i18n::text("Preset importado y cargado en la copia de trabajo."));});
    connect(exportAction,&QAction::triggered,this,[this]{auto path=QFileDialog::getSaveFileName(this,mapper_i18n::text("Exportar preset"),"mi-preset.mapper.json",mapper_i18n::text("Preset Mapper (*.mapper.json)"));if(path.isEmpty())return;QString error;const QString name=presetList->currentData().toString().isEmpty()?QFileInfo(path).completeBaseName():presetList->currentText();if(!exportPreset(path,name,error))status->setText(error);else status->setText(mapper_i18n::text("Preset exportado con sus imágenes incluidas."));});
    undoButton=button(mapper_i18n::text("↶ Deshacer"),"undoEdit",header);redoButton=button(mapper_i18n::text("↷ Rehacer"),"redoEdit",header);
    connect(undoButton,&QPushButton::clicked,this,&MapperEditor::undoEdit);connect(redoButton,&QPushButton::clicked,this,&MapperEditor::redoEdit);root->addLayout(header);
    auto *subtitle=new QLabel(mapper_i18n::text("Mapea, divide y deforma tu vídeo en múltiples superficies"));subtitle->setObjectName("muted");root->addWidget(subtitle);
    auto *split=new QSplitter;root->addWidget(split,1);
    auto *left=new QWidget;left->setMinimumWidth(240);left->setMaximumWidth(340);auto *leftLayout=new QVBoxLayout(left);leftLayout->setContentsMargins(0,0,5,0);
    auto *sourceGroup=group(mapper_i18n::text("1. FUENTE Y DISTRIBUCIÓN"),leftLayout); auto *sourceLayout=new QVBoxLayout(sourceGroup);
    sourceLayout->addWidget(new QLabel(mapper_i18n::text("Fuente OBS existente")));sources=new QComboBox;sources->setObjectName("inputSource");sourceLayout->addWidget(sources);
    auto *reload=new QPushButton(mapper_i18n::text("Actualizar fuentes"));sourceLayout->addWidget(reload);connect(reload,&QPushButton::clicked,this,&MapperEditor::refreshSources);
    distribution=new QComboBox;distribution->setObjectName("distribution");distribution->addItems({mapper_i18n::text("Split"),mapper_i18n::text("Repeat"),mapper_i18n::text("Custom UV")});sourceLayout->addWidget(distribution);
    connect(sources,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){if(loading||index<0)return;const auto uuid=utf8(sources->itemData(index).toString());if(!validInput(uuid)){refresh();status->setText(mapper_i18n::text("Esta entrada crea un ciclo o no está disponible."));return;}change([&]{obs_data_set_string(settings,"input_uuid",uuid.c_str());});});
    connect(distribution,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){change([&]{layout.mode=index==1?mapping::Mode::Repeat:index==2?mapping::Mode::Custom:mapping::Mode::Split;obs_data_set_string(settings,"distribution",index==1?"repeat":index==2?"custom":"split");});});
    auto *zoneGroup=group(mapper_i18n::text("2. ZONAS"),leftLayout);auto *zoneLayout=new QVBoxLayout(zoneGroup);auto *actions=new QHBoxLayout;
    connect(button(mapper_i18n::text("＋ Añadir"),"addZone",actions),&QPushButton::clicked,this,[this]{zoneAction("add");});connect(button(mapper_i18n::text("Duplicar"),"duplicateZone",actions),&QPushButton::clicked,this,[this]{zoneAction("duplicate");});connect(button(mapper_i18n::text("Eliminar"),"deleteZone",actions),&QPushButton::clicked,this,[this]{zoneAction("delete");});zoneLayout->addLayout(actions);
    zones=new QListWidget;zones->setObjectName("zoneList");zones->setDragDropMode(QAbstractItemView::InternalMove);zoneLayout->addWidget(zones,1);
    connect(zones,&QListWidget::currentRowChanged,this,[this](int index){if(!loading&&index>=0)QTimer::singleShot(0,this,[this,index]{select(size_t(index));});});
    connect(zones,&QListWidget::itemChanged,this,[this](QListWidgetItem *item){if(loading)return;const auto id=utf8(item->data(Qt::UserRole).toString());const auto label=utf8(item->text());const bool visible=item->checkState()==Qt::Checked;QTimer::singleShot(0,this,[this,id,label,visible]{change([&]{for(auto &z:layout.zones)if(z.id==id){z.name=label;z.visible=visible;}});});});
    connect(zones->model(),&QAbstractItemModel::rowsMoved,this,[this]{if(loading)return;std::vector<std::string> ids;for(int i=0;i<zones->count();++i)ids.push_back(utf8(zones->item(i)->data(Qt::UserRole).toString()));const auto selectedId=selected()?selected()->id:"";QTimer::singleShot(0,this,[this,ids,selectedId]{change([&]{std::vector<mapping::Zone> ordered;for(const auto &id:ids)for(const auto &z:layout.zones)if(z.id==id)ordered.push_back(z);layout.zones=std::move(ordered);for(size_t i=0;i<layout.zones.size();++i)if(layout.zones[i].id==selectedId)selectedIndex=i;});});});
    auto *order=new QHBoxLayout;connect(button("↑","zoneUp",order),&QPushButton::clicked,this,[this]{zoneAction("up");});connect(button("↓","zoneDown",order),&QPushButton::clicked,this,[this]{zoneAction("down");});connect(button(mapper_i18n::text("Distribuir"),"arrangeZones",order),&QPushButton::clicked,this,[this]{zoneAction("arrange");});zoneLayout->addLayout(order);leftLayout->setStretchFactor(zoneGroup,1);
    split->addWidget(left);
    auto *center=new QWidget;auto *central=new QVBoxLayout(center);central->setContentsMargins(6,0,6,0);auto *viewBar=new QHBoxLayout;viewBar->addWidget(new QLabel(mapper_i18n::text("OBS Screen Mapper Editor")));viewBar->addStretch();
    connect(button("−","zoomOut",viewBar),&QPushButton::clicked,this,[this]{preview->setZoom(float(zoomLabel->text().remove('%').toDouble()/100/1.2));});zoomLabel=new QLabel("100%");viewBar->addWidget(zoomLabel);
    connect(button("＋","zoomIn",viewBar),&QPushButton::clicked,this,[this]{preview->setZoom(float(zoomLabel->text().remove('%').toDouble()/100*1.2));});connect(button(mapper_i18n::text("Ajustar al lienzo"),"fitCanvas",viewBar),&QPushButton::clicked,this,[this]{preview->fit();});central->addLayout(viewBar);
    auto *contextBar=new QHBoxLayout;sceneButton=button(mapper_i18n::text("Ver escena completa"),"showSceneContext",contextBar);sceneButton->setCheckable(true);
    scenePicker=new QComboBox;scenePicker->setObjectName("referenceScene");scenePicker->setMinimumWidth(160);scenePicker->setMaximumWidth(300);scenePicker->hide();contextBar->addWidget(scenePicker);
    refreshSceneButton=button(QString::fromUtf8("↻"),"refreshSceneContext",contextBar);refreshSceneButton->setToolTip(mapper_i18n::text("Actualizar referencia de escena"));refreshSceneButton->hide();contextBar->addStretch();central->addLayout(contextBar);
    preview=new MapperPreview(draft);central->addWidget(preview,1);preview->zoomChanged=[this](float zoom){zoomLabel->setText(QString::number(zoom*100,'f',0)+"%");};
    connect(sceneButton,&QPushButton::toggled,this,[this](bool enabled){scenePicker->setVisible(enabled);refreshSceneButton->setVisible(enabled);if(enabled){isolatedView=preview->viewState();updateSceneContext();}else{preview->setSceneContext({});preview->restoreView(isolatedView.zoom,isolatedView.pan);publish();refresh();}});
    connect(scenePicker,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{if(sceneButton->isChecked())updateSceneContext();});connect(refreshSceneButton,&QPushButton::clicked,this,&MapperEditor::refreshSceneChoices);
    preview->selectZone=[this](size_t index){select(index);};preview->beginEdit=[this]{beginGesture();};preview->endEdit=[this]{endGesture();};
    preview->selectPoint=[this](size_t index){if(tool==1)vertexIndex=index;else if(tool==4)cornerIndex=index;else if(tool==2)meshIndex=index;refresh();};
    preview->moveTransform=[this](size_t index,int point,QPointF delta){change([&]{auto &t=layout.zones[index].transform;const double x=(point==0?1:.5)-t.pivot_x,y=(point==0?1:-.12)-t.pivot_y;if(!point){const double length=x*x+y*y;if(length>1e-8)t.scale=std::clamp(float(t.scale*(1+(delta.x()*x+delta.y()*y)/length)),.001f,10.f);}else{const double angle=(std::atan2(y+delta.y(),x+delta.x())-std::atan2(y,x))*180/3.141592653589793;t.rz=float(std::remainder(t.rz+angle,360));}},false);};
    preview->moveRegion=[this](size_t index,int point,QPointF delta){change([&]{auto &z=layout.zones[index];const float dx=float(delta.x()),dy=float(delta.y());if(tool==5){auto &m=z.mask;if(!point){m.center_x=std::clamp(m.center_x+dx,0.f,1.f);m.center_y=std::clamp(m.center_y+dy,0.f,1.f);}else if(m.kind==1){const float denominator=m.width*m.width+m.height*m.height;if(denominator>1e-8f)m.radius=std::clamp(m.radius+(dx*m.width+dy*m.height)/denominator,0.f,1.f);}else{m.width=std::clamp(m.width+dx*2,0.f,1.f);m.height=std::clamp(m.height+dy*2,0.f,1.f);}}else if(tool==6){layout.mode=mapping::Mode::Custom;obs_data_set_string(settings,"distribution","custom");auto &u=z.uv;if(!point){u.x=std::clamp(u.x-dx*u.w,0.f,1.f-u.w);u.y=std::clamp(u.y-dy*u.h,0.f,1.f-u.h);}else{u.w=std::clamp(u.w+dx,0.f,1.f-u.x);u.h=std::clamp(u.h+dy,0.f,1.f-u.y);}}},false);};
    preview->moveZone=[this](size_t index,QPointF delta){change([&]{auto &z=layout.zones[index];const auto h=mapping::homography(z.transform);const auto p=mapping::project(z.transform,z.destination.x,z.destination.y,z.destination.w,z.destination.h,.5f,.5f,float(canvasWidth->value()),float(canvasHeight->value()));const float camera=p.w/(h.g*.5f+h.h*.5f+1);if(p.visible){z.transform.x=std::clamp(z.transform.x+float(delta.x())*camera,-10.0f,10.0f);z.transform.y=std::clamp(z.transform.y+float(delta.y())*camera,-10.0f,10.0f);}},false);};
    preview->moveMesh=[this](size_t index,size_t point,QPointF delta){change([&]{meshIndex=point;mapping::move_control(layout.zones[index].mesh,point,float(delta.x()),float(delta.y()));},false);};
    preview->movePolygon=[this](size_t index,size_t point,QPointF delta){change([&]{auto &z=layout.zones[index];vertexIndex=point;auto &p=z.polygon.vertices[point];
        float dx=float(delta.x()),dy=float(delta.y());
        p.x=mapping::bounded(p.x+dx);p.y=mapping::bounded(p.y+dy);mapping::compile_polygon(z.polygon);},false);};
    preview->moveCorner=[this](size_t index,size_t point,QPointF delta){change([&]{auto &z=layout.zones[index];auto t=z.transform;const float u=point==1||point==2?1.0f:0,v=point>=2?1.0f:0,eps=.0001f;
        auto sample=[&](const mapping::Transform &q){return mapping::project(q,z.destination.x,z.destination.y,z.destination.w,z.destination.h,u,v,float(canvasWidth->value()),float(canvasHeight->value()));};
        const auto base=sample(t);auto tx=t,ty=t;tx.corners[point].x+=eps;ty.corners[point].y+=eps;const auto px=sample(tx),py=sample(ty);const float a=(px.x-base.x)/eps,b=(py.x-base.x)/eps,c=(px.y-base.y)/eps,d=(py.y-base.y)/eps,det=a*d-b*c;
        if(base.visible&&px.visible&&py.visible&&std::abs(det)>1e-7f){t.corners[point].x=std::clamp(t.corners[point].x+float((d*delta.x()-b*delta.y())/det),-4.0f,4.0f);t.corners[point].y=std::clamp(t.corners[point].y+float((-c*delta.x()+a*delta.y())/det),-4.0f,4.0f);if(mapping::valid_transform(t))z.transform=t;}},false);};
    auto *hand=button(mapper_i18n::text("Mover lienzo"),"tool3",viewBar);hand->setText(QString::fromUtf8("✋"));hand->setMaximumWidth(38);hand->setCheckable(true);hand->setToolTip(mapper_i18n::text("Espacio + arrastrar para mover el lienzo"));
    connect(hand,&QPushButton::clicked,this,[this](bool value){activateTool(value?3:tabs->currentIndex()==0?0:tabs->currentIndex()==1?shapeTool:tabs->currentIndex()==2?5:6);});
    auto *viewButton=new QToolButton;viewButton->setText(mapper_i18n::text("Vista"));viewButton->setObjectName("viewMenu");viewButton->setPopupMode(QToolButton::InstantPopup);auto *viewMenu=new QMenu(viewButton);viewButton->setMenu(viewMenu);viewBar->addWidget(viewButton);
    for(const char *key:{"showGrid","showZones"}) {
        auto *box=check(key,mapper_i18n::text(std::string(key)=="showGrid"?"Cuadrícula":"Mostrar zonas"));box->setParent(this);box->hide();box->setChecked(true);
        auto *action=viewMenu->addAction(box->text());action->setObjectName(QString(key)+"Action");action->setCheckable(true);action->setChecked(true);
        connect(action,&QAction::toggled,box,&QCheckBox::setChecked);connect(box,&QCheckBox::toggled,action,&QAction::setChecked);connect(box,&QCheckBox::toggled,this,[this]{if(!loading)refresh();});
    }
    viewMenu->addSeparator();auto *referenceAction=viewMenu->addAction(mapper_i18n::text("Fondo de referencia…"));
    connect(referenceAction,&QAction::triggered,this,[this]{auto path=QFileDialog::getOpenFileName(this,mapper_i18n::text("Fondo del editor"),{},mapper_i18n::text("Imágenes (*.png *.jpg *.jpeg *.bmp)"));if(path.isEmpty())return;if(!preview->setReference(path))status->setText(mapper_i18n::text("No se pudo abrir la imagen de referencia."));else{workspaceReference=path;workspaceWarning.clear();}});
    auto *clearReference=viewMenu->addAction(mapper_i18n::text("Quitar fondo"));connect(clearReference,&QAction::triggered,this,[this]{preview->setReference({});workspaceReference.clear();workspaceWarning.clear();refresh();});split->addWidget(center);
    auto *right=new QWidget(this);right->setMinimumWidth(300);right->setMaximumWidth(390);auto *rightLayout=new QVBoxLayout(right);rightLayout->setContentsMargins(5,0,0,0);rightLayout->addWidget(new QLabel(mapper_i18n::text("3. PROPIEDADES DE LA ZONA")));
    auto *nameRow=new QHBoxLayout;zoneName=new QLineEdit;zoneName->setObjectName("zoneName");nameRow->addWidget(zoneName);nameRow->addWidget(check("visible",mapper_i18n::text("Visible")));rightLayout->addLayout(nameRow);
    connect(zoneName,&QLineEdit::editingFinished,this,[this]{if(selected()&&!loading){auto name=utf8(zoneName->text());change([&]{selected()->name=name.empty()?mapper_i18n::text("Zona ")+selected()->id:name;});}});
    connect(checks["visible"],&QCheckBox::toggled,this,[this](bool value){if(selected())change([&]{selected()->visible=value;});});
    tabs=new QTabWidget;tabs->setObjectName("zoneTabs");rightLayout->addWidget(tabs,1);QFormLayout *geometry,*mask,*uv,*transform;
    scrollPage(tabs,mapper_i18n::text("Posición"),transform);scrollPage(tabs,mapper_i18n::text("Forma"),geometry);scrollPage(tabs,mapper_i18n::text("Máscara"),mask);scrollPage(tabs,mapper_i18n::text("Vídeo / UV"),uv);
    auto fold=[&](QFormLayout *parent,const char *label,const char *name,bool attach=true){auto *toggle=new QPushButton(mapper_i18n::text(label));toggle->setObjectName(name);toggle->setCheckable(true);if(attach)parent->addRow(toggle);else toggle->setParent(parent->parentWidget());auto *body=new QWidget;body->setObjectName(QString(name)+"Body");auto *form=new QFormLayout(body);form->setContentsMargins(0,4,0,4);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);if(attach)parent->addRow(body);else body->setParent(parent->parentWidget());body->hide();connect(toggle,&QPushButton::toggled,body,&QWidget::setVisible);return form;};
    auto *perspective=fold(transform,"Perspectiva","perspectiveSection",false);auto *advanced=fold(transform,"Avanzado","transformAdvanced",false);
    auto *shapeButtons=new QHBoxLayout;for(int i:{1,2}){auto *b=button(mapper_i18n::text(i==1?"Polígono":"Editar malla"),("tool"+std::to_string(i)).c_str(),shapeButtons);b->setCheckable(true);connect(b,&QPushButton::clicked,this,[this,i]{activateTool(i,true);});}geometry->addRow(shapeButtons);
    connect(tabs,&QTabWidget::currentChanged,this,[this](int index){if(!loading)activateTool(index==0?0:index==1?shapeTool:index==2?5:6);});
    const char *transformLabels[]={"Posición X","Posición Y","Profundidad Z","Rotación X","Rotación Y","Rotación Z","Escala","Pivote X","Pivote Y"};
    for(size_t i=0;i<9;++i){const auto f=mapping::transform_fields[i];const double factor=i>=3&&i<=5?1:100;auto *spin=number(f.key,f.min*factor,f.max*factor);if(factor==1)spin->setSuffix(QString::fromUtf8("°"));if(i==2||i==3||i==4||i==7||i==8)(i<7?perspective:advanced)->addRow(mapper_i18n::text(transformLabels[i]),spin);connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,f,factor](double value){if(selected())change([&]{selected()->transform.*(f.member)=float(value/factor);});});}
    perspective->addRow(check("cornerPin",mapper_i18n::text("Ajuste por cuatro esquinas")));connect(checks["cornerPin"],&QCheckBox::toggled,this,[this](bool value){if(selected())change([&]{selected()->transform.pinned=value;});if(!loading)activateTool(value?4:0);});
    auto *cornerButton=new QPushButton(mapper_i18n::text("Cuatro esquinas"));cornerButton->setObjectName("tool4");cornerButton->setCheckable(true);perspective->addRow(cornerButton);connect(cornerButton,&QPushButton::clicked,this,[this]{activateTool(4,true);});
    cornerList=new QComboBox;cornerList->setObjectName("cornerIndex");for(const char *label:{"Superior izquierda","Superior derecha","Inferior derecha","Inferior izquierda"})cornerList->addItem(mapper_i18n::text(label));advanced->addRow(mapper_i18n::text("Esquina"),cornerList);connect(cornerList,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){if(!loading&&index>=0){cornerIndex=size_t(index);refresh();}});
    advanced->addRow(mapper_i18n::text("Esquina X"),number("cornerX",-400,400));advanced->addRow(mapper_i18n::text("Esquina Y"),number("cornerY",-400,400));
    for(const char *key:{"cornerX","cornerY"})connect(numbers[key],qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{if(selected())change([&]{selected()->transform.corners[cornerIndex]={float(numbers["cornerX"]->value()/100),float(numbers["cornerY"]->value()/100)};});});
    auto *resetTransform=new QPushButton(mapper_i18n::text("Restablecer transformación"));resetTransform->setObjectName("resetTransform");connect(resetTransform,&QPushButton::clicked,this,[this]{if(selected())change([&]{selected()->transform=mapping::Transform{};});});
    geometryForm=geometry;maskForm=mask;
    geometry->addRow(check("polygon",mapper_i18n::text("Polygon")));geometry->addRow(check("mesh",mapper_i18n::text("Smooth Mesh")));
    connect(checks["polygon"],&QCheckBox::toggled,this,[this](bool enabled){if(selected())change([&]{selected()->polygon.enabled=enabled;});});
    connect(checks["mesh"],&QCheckBox::toggled,this,[this](bool enabled){if(selected())change([&]{selected()->mesh.enabled=enabled;});});
    for(const auto &field:{std::pair<const char *,const char *>{"dstX",mapper_i18n::text("Destino X")},{"dstY",mapper_i18n::text("Destino Y")},{"dstW",mapper_i18n::text("Ancho")},{"dstH",mapper_i18n::text("Alto")}}){auto *spin=number(field.first,0,100);if(std::string(field.first)=="dstX"||std::string(field.first)=="dstY")advanced->addRow(field.second,spin);}
    auto updateDestination=[this]{if(loading||!selected())return;change([&]{layout.automatic=false;selected()->destination=mapping::sanitize_destination({float(numbers["dstX"]->value()/100),float(numbers["dstY"]->value()/100),float(numbers["dstW"]->value()/100),float(numbers["dstH"]->value()/100)});});};
    for(const char *key:{"dstX","dstY","dstW","dstH"})connect(numbers[key],qOverload<double>(&QDoubleSpinBox::valueChanged),this,updateDestination);
    vertices=new QComboBox;vertices->setObjectName("polygonVertex");geometry->addRow(mapper_i18n::text("Vértice"),vertices);connect(vertices,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){if(!loading&&index>=0){vertexIndex=size_t(index);refresh();}});
    auto *vertexActions=new QHBoxLayout;vertexActionRow=vertexActions;connect(button("＋ Punto","addVertex",vertexActions),&QPushButton::clicked,this,[this]{if(!selected())return;change([&]{auto &p=selected()->polygon.vertices;if(p.empty()){p=mapping::Polygon{}.vertices;return;}auto a=p[vertexIndex],b=p[(vertexIndex+1)%p.size()];p.insert(p.begin()+vertexIndex+1,{(a.x+b.x)/2,(a.y+b.y)/2,(a.u+b.u)/2,(a.v+b.v)/2});++vertexIndex;});});
    connect(button("− Punto","deleteVertex",vertexActions),&QPushButton::clicked,this,[this]{if(selected()&&selected()->polygon.vertices.size()>3)change([&]{auto &p=selected()->polygon.vertices;p.erase(p.begin()+vertexIndex);vertexIndex=std::min(vertexIndex,p.size()-1);});});geometry->addRow(vertexActions);
    polygonNumbersForm=fold(geometry,"Coordenadas de puntos","polygonAdvanced");
    for(const auto &field:{std::pair<const char *,const char *>{"vertexX",mapper_i18n::text("Punto X")},{"vertexY",mapper_i18n::text("Punto Y")},{"vertexU",mapper_i18n::text("Punto U")},{"vertexV",mapper_i18n::text("Punto V")}})polygonNumbersForm->addRow(field.second,number(field.first,0,100));
    for(const char *key:{"vertexX","vertexY","vertexU","vertexV"})connect(numbers[key],qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{if(loading||!selected()||selected()->polygon.vertices.empty())return;change([&]{selected()->polygon.vertices[vertexIndex]={float(numbers["vertexX"]->value()/100),float(numbers["vertexY"]->value()/100),float(numbers["vertexU"]->value()/100),float(numbers["vertexV"]->value()/100)};});});
    columns=new QSpinBox;columns->setObjectName("meshColumns");columns->setRange(2,16);rows=new QSpinBox;rows->setObjectName("meshRows");rows->setRange(2,16);geometry->addRow(mapper_i18n::text("Columnas"),columns);geometry->addRow(mapper_i18n::text("Filas"),rows);
    for(auto *spin:{columns,rows})connect(spin,qOverload<int>(&QSpinBox::valueChanged),this,[this]{if(selected())change([&]{mapping::resize_mesh(selected()->mesh,columns->value(),rows->value());});});
    geometry->addRow(check("smooth",mapper_i18n::text("Suavizado de malla")));geometry->addRow(check("wave",mapper_i18n::text("Perfil de onda")));
    auto sliderRow=[&](const char *key,const QString &label,int min,int max) {
        auto *body=new QWidget;auto *row=new QHBoxLayout(body);row->setContentsMargins(0,0,0,0);auto *slider=new QSlider(Qt::Horizontal);slider->setRange(min,max);slider->setObjectName(QString(key)+"Slider");
        auto *spin=number(key,min,max);spin->setMaximumWidth(92);row->addWidget(slider,1);row->addWidget(spin);geometry->addRow(label,body);
        connect(slider,&QSlider::valueChanged,spin,[spin](int value){spin->setValue(value);});connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),slider,[slider](double value){slider->setValue(int(value));});
        connect(slider,&QSlider::sliderPressed,this,[this]{beginGesture();});connect(slider,&QSlider::sliderReleased,this,[this]{endGesture();});
    };
    sliderRow("curveH",mapper_i18n::text("Curvatura horizontal"),-50,50);sliderRow("curveV",mapper_i18n::text("Curvatura vertical"),-50,50);
    geometry->addRow(check("soft",mapper_i18n::text("Selección suave")));sliderRow("radius",mapper_i18n::text("Radio"),0,150);sliderRow("strength",mapper_i18n::text("Fuerza"),0,100);
    meshNumbersForm=fold(geometry,"Coordenadas de malla","meshAdvanced");
    meshPoints=new QComboBox;meshPoints->setObjectName("meshPoint");meshNumbersForm->addRow(mapper_i18n::text("Punto de malla"),meshPoints);connect(meshPoints,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){if(!loading&&index>=0){meshIndex=size_t(index);refresh();}});
    meshNumbersForm->addRow(mapper_i18n::text("Desplazamiento X"),number("meshX",-100,100));meshNumbersForm->addRow(mapper_i18n::text("Desplazamiento Y"),number("meshY",-100,100));
    for(const char *key:{"smooth","wave","soft"})connect(checks[key],&QCheckBox::toggled,this,[this,key](bool value){if(selected())change([&]{auto &m=selected()->mesh;if(std::string(key)=="smooth")m.smooth=value;else if(std::string(key)=="wave")m.wave=value;else m.soft=value;});});
    for(const char *key:{"curveH","curveV","radius","strength"})connect(numbers[key],qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,key](double value){if(selected())change([&]{auto &m=selected()->mesh;const std::string name=key;if(name=="curveH")m.horizontal=float(value/100);else if(name=="curveV")m.vertical=float(value/100);else if(name=="radius")m.radius=float(value/100);else m.strength=float(value/100);});});
    for(const char *key:{"meshX","meshY"})connect(numbers[key],qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{if(loading||!selected()||selected()->mesh.points.empty())return;change([&]{auto &m=selected()->mesh;mapping::move_control(m,meshIndex,float(numbers["meshX"]->value()/100)-m.points[meshIndex].x,float(numbers["meshY"]->value()/100)-m.points[meshIndex].y);});});
    auto *resetMesh=new QPushButton(mapper_i18n::text("Restablecer malla"));resetMesh->setObjectName("resetMesh");geometry->addRow(resetMesh);connect(resetMesh,&QPushButton::clicked,this,[this]{if(selected())change([&]{const bool enabled=selected()->mesh.enabled;selected()->mesh=mapping::Mesh{};selected()->mesh.enabled=enabled;});});
    maskKind=new QComboBox;maskKind->setObjectName("maskType");maskKind->addItems({mapper_i18n::text("Sin máscara"),mapper_i18n::text("Elipse"),mapper_i18n::text("Rectángulo"),mapper_i18n::text("Imagen PNG / SVG")});mask->addRow(mapper_i18n::text("Máscara"),maskKind);connect(maskKind,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int value){if(selected())change([&]{selected()->mask.kind=value;});});
    const char *shapeLabels[]={"Centro X de máscara","Centro Y de máscara","Ancho de máscara","Alto de máscara","Radio de círculo / elipse"};
    for(size_t i=0;i<5;++i){const auto field=mapping::mask_dimensions[i];mask->addRow(mapper_i18n::text(shapeLabels[i]),number(field.key,0,100));connect(numbers[field.key],qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,field](double value){if(selected())change([&]{selected()->mask.*(field.member)=float(value/100);});});}
    auto *fitMesh=new QPushButton(mapper_i18n::text("Adaptar malla al recorte"));fitMesh->setObjectName("fitMeshToMask");mask->addRow(fitMesh);
    fitMesh->setToolTip(mapper_i18n::text("Distribuye los controles sobre el recorte conservando el vídeo. Después puedes deformarlo moviendo los puntos."));
    connect(fitMesh,&QPushButton::clicked,this,[this]{if(!selected())return;auto candidate=selected()->mesh;const auto &m=selected()->mask;
        if((m.kind!=1&&m.kind!=2)||m.invert||m.strength<1){status->setText(mapper_i18n::text("Usa un rectángulo o una elipse sin invertir, con intensidad del 100 %."));return;}
        if((!candidate.enabled&&(candidate.horizontal!=0||candidate.vertical!=0))||std::any_of(candidate.points.begin(),candidate.points.end(),[](const mapping::Offset &p){return p.x!=0||p.y!=0;})){status->setText(mapper_i18n::text("La malla ya tiene puntos desplazados. Deshaz esos cambios o restablece la malla antes de adaptarla."));return;}
        candidate.domain=m.kind;candidate.center_x=m.center_x;candidate.center_y=m.center_y;candidate.axis_x=m.width*(m.kind==1?m.radius:.5f);candidate.axis_y=m.height*(m.kind==1?m.radius:.5f);candidate.enabled=true;
        if(candidate.axis_x<.000001f||candidate.axis_y<.000001f||!mapping::validate_mesh(candidate)){status->setText(mapper_i18n::text("La máscara debe tener un tamaño mayor que cero."));return;}
        change([&]{selected()->mesh=candidate;});activateTool(2);status->setText(mapper_i18n::text("Malla adaptada. El vídeo conserva su encuadre; mueve los puntos para deformarlo."));
    });
    auto *resetShape=new QPushButton(mapper_i18n::text("Restablecer forma"));resetShape->setObjectName("resetMaskShape");mask->addRow(resetShape);connect(resetShape,&QPushButton::clicked,this,[this]{if(selected())change([&]{mapping::reset_mask_shape(selected()->mask);});});
    maskPath=new QLineEdit;maskPath->setObjectName("maskFile");mask->addRow(mapper_i18n::text("Archivo"),maskPath);connect(maskPath,&QLineEdit::editingFinished,this,[this]{if(selected()&&!loading){auto path=utf8(maskPath->text());change([&]{selected()->mask.path=path;});}});
    auto *browse=new QPushButton(mapper_i18n::text("Elegir PNG / SVG…"));browse->setObjectName("browseMask");mask->addRow(browse);connect(browse,&QPushButton::clicked,this,[this]{auto path=QFileDialog::getOpenFileName(this,mapper_i18n::text("Máscara de zona"),{},mapper_i18n::text("Máscaras (*.png *.svg)"));if(!path.isEmpty()&&selected())change([&]{selected()->mask.path=utf8(path);selected()->mask.kind=3;});});
    mask->addRow(check("luminance","Luminancia × alfa"));mask->addRow(check("invert",mapper_i18n::text("Invertir")));mask->addRow(mapper_i18n::text("Intensidad"),number("maskStrength",0,100));mask->addRow(mapper_i18n::text("Suavizado de borde"),number("feather",0,25));
    for(const char *key:{"luminance","invert"})connect(checks[key],&QCheckBox::toggled,this,[this,key](bool value){if(selected())change([&]{if(std::string(key)=="invert")selected()->mask.invert=value;else selected()->mask.luminance=value;});});
    for(const char *key:{"maskStrength","feather"})connect(numbers[key],qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,key](double value){if(selected())change([&]{if(std::string(key)=="feather")selected()->mask.feather=float(value/100);else selected()->mask.strength=float(value/100);});});
    auto *reloadMask=new QPushButton(mapper_i18n::text("Recargar archivo"));reloadMask->setObjectName("reloadMask");mask->addRow(reloadMask);connect(reloadMask,&QPushButton::clicked,this,[this]{if(selected())change([&]{auto &revision=selected()->mask.revision;revision=revision<9223372036854775807LL?revision+1:0;});});
    for(const auto &field:{std::pair<const char *,const char *>{"uvX",mapper_i18n::text("Región X")},{"uvY",mapper_i18n::text("Región Y")},{"uvW",mapper_i18n::text("Ancho UV")},{"uvH",mapper_i18n::text("Alto UV")}})uv->addRow(field.second,number(field.first,0,100));
    for(const char *key:{"uvX","uvY","uvW","uvH"})connect(numbers[key],qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{if(loading||!selected())return;change([&]{layout.mode=mapping::Mode::Custom;obs_data_set_string(settings,"distribution","custom");selected()->uv=mapping::sanitize({float(numbers["uvX"]->value()/100),float(numbers["uvY"]->value()/100),float(numbers["uvW"]->value()/100),float(numbers["uvH"]->value()/100)});});});
    auto *maskHint=new QLabel(mapper_i18n::text("Arrastra el centro para mover; el punto exterior cambia el tamaño."));maskHint->setWordWrap(true);mask->insertRow(0,maskHint);
    auto *uvHint=new QLabel(mapper_i18n::text("Arrastra el centro para encuadrar; el punto exterior cambia la región UV."));uvHint->setWordWrap(true);uv->insertRow(0,uvHint);
    const char *basicKeys[]={"transform_x","transform_y","dstW","dstH","transform_rz","transform_scale"};
    const char *basicLabels[]={"Posición X","Posición Y","Ancho","Alto","Rotación Z","Escala"};
    for(int i=0;i<6;++i)transform->addRow(mapper_i18n::text(basicLabels[i]),numbers[basicKeys[i]]);
    for(const char *name:{"perspectiveSection","transformAdvanced"}){transform->addRow(findChild<QPushButton*>(name));transform->addRow(findChild<QWidget*>(QString(name)+"Body"));}
    transform->addRow(resetTransform);
    auto *resetZone=new QPushButton(mapper_i18n::text("Restablecer zona"));resetZone->setObjectName("resetZone");rightLayout->addWidget(resetZone);connect(resetZone,&QPushButton::clicked,this,[this]{if(selected())change([&]{auto old=*selected();*selected()=mapping::Zone{};selected()->id=old.id;selected()->name=old.name;selected()->destination=old.destination;});});split->addWidget(right);split->setSizes({280,820,340});
    auto *bottom=new QHBoxLayout;bottom->addWidget(new QLabel(mapper_i18n::text("4. LIENZO Y SALIDA")));canvasWidth=new QSpinBox;canvasHeight=new QSpinBox;canvasWidth->setObjectName("canvasWidth");canvasHeight->setObjectName("canvasHeight");for(auto *spin:{canvasWidth,canvasHeight}){spin->setRange(16,16384);spin->setKeyboardTracking(false);bottom->addWidget(spin);}bottom->addWidget(new QLabel(mapper_i18n::text("Fondo transparente · preview en vivo")));bottom->addStretch();
    connect(canvasWidth,qOverload<int>(&QSpinBox::valueChanged),this,[this](int value){change([&]{obs_data_set_int(settings,"canvas_width",value);});});connect(canvasHeight,qOverload<int>(&QSpinBox::valueChanged),this,[this](int value){change([&]{obs_data_set_int(settings,"canvas_height",value);});});
    connect(button(mapper_i18n::text("Cancelar"),"cancelEditor",bottom),&QPushButton::clicked,this,&QDialog::reject);connect(button(mapper_i18n::text("Aplicar"),"applyEditor",bottom),&QPushButton::clicked,this,&MapperEditor::applyEdit);connect(button(mapper_i18n::text("Aceptar"),"acceptEditor",bottom),&QPushButton::clicked,this,&MapperEditor::acceptEdit);root->addLayout(bottom);
    auto *footer=new QHBoxLayout;status=new QLabel;status->setObjectName("editorStatus");status->setWordWrap(true);footer->addWidget(status,1);auto *credit=new QLabel("a plugin from <a style='color:#1ebcff' href='https://discord.gg/dhcqZPDYh'>DjEmiMas</a>");credit->setOpenExternalLinks(true);footer->addWidget(credit);root->addLayout(footer);
    for(auto *b:findChildren<QPushButton*>()) b->setAutoDefault(false);
}
void MapperEditor::keyPressEvent(QKeyEvent *event) {
    if(event->matches(QKeySequence::Undo)){undoEdit();event->accept();return;}if(event->matches(QKeySequence::Redo)){redoEdit();event->accept();return;}
    if(event->key()==Qt::Key_Delete && (focusWidget()==preview || focusWidget()==zones)){zoneAction("delete");event->accept();return;}
    QDialog::keyPressEvent(event);
}
extern "C" __declspec(dllexport) void mapper_open_editor_v2(obs_source_t *source,const char *directory) {
    if(!QApplication::instance() || !source) return;
    MapperEditor dialog(source,QApplication::activeWindow(),QString::fromUtf8(directory?directory:"")); dialog.exec();
}



















