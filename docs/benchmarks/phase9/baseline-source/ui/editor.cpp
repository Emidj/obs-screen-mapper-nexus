// SPDX-License-Identifier: GPL-2.0-or-later
#include "editor.hpp"
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
    setObjectName("mapperEditor"); setWindowTitle("OBS Screen Mapper Editor"); setMinimumSize(1100,720); resize(1480,930);
    auto *original=obs_source_get_settings(target);
    settings=obs_data_create_from_json(obs_data_get_json(original));
    obs_data_set_int(settings,"canvas_width",obs_source_get_width(target)); obs_data_set_int(settings,"canvas_height",obs_source_get_height(target));
    obs_data_release(original);
    layout=mapping::read(settings); selectedIndex=mapping::selection(settings,layout);
    const auto overrideDirectory=qEnvironmentVariable("OBS_MAPPER_PRESET_DIR");if(!overrideDirectory.isEmpty())presetDirectory=overrideDirectory;
    if(presetDirectory.isEmpty())presetDirectory=QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)+"/obs-screen-mapper/presets";
    presetStore=std::make_unique<MapperPresets>(presetDirectory);
    draft=obs_source_create_private("obs_screen_mapper","Mapper Editor Preview",settings);
    build();restoreWorkspace();refreshPresets(); refreshSources(); refresh(); publish();
    connect(&timer,&QTimer::timeout,this,[this]{ if(dirty) publish(); }); timer.start(33);
}
MapperEditor::~MapperEditor() { timer.stop(); obs_source_release(draft); obs_data_release(settings); obs_source_release(target); }
mapping::Zone *MapperEditor::selected() { return selectedIndex<layout.zones.size()?&layout.zones[selectedIndex]:nullptr; }
void MapperEditor::sync() {
    mapping::write(settings,layout);
    if(auto *zone=selected()) obs_data_set_string(settings,"selected_zone",zone->id.c_str());
    else obs_data_set_string(settings,"selected_zone","");
    if(preview) {
        const auto view=preview->viewState();const auto reference=preview->referencePath().isEmpty()?workspaceReference:preview->referencePath();
        QJsonObject editor{{"version",1},{"grid",checks["showGrid"]->isChecked()},{"zones",checks["showZones"]->isChecked()},
            {"zoom",view.zoom},{"pan_x",view.pan.x()},{"pan_y",view.pan.y()},{"tool",tool},{"vertex",double(vertexIndex)},{"control",double(meshIndex)},
            {"reference",reference},{"last_preset",activePreset}};
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
    tool=std::clamp(editor.value("tool").toInt(0),0,3);vertexIndex=size_t(std::clamp(editor.value("vertex").toInt(0),0,511));meshIndex=size_t(std::clamp(editor.value("control").toInt(0),0,255));
    if(auto *button=findChild<QPushButton*>(QString("tool%1").arg(tool)))button->setChecked(true);
    auto boundedView=[](double value,double min,double max,double fallback){return std::isfinite(value)?std::clamp(value,min,max):fallback;};
    preview->restoreView(float(boundedView(editor.value("zoom").toDouble(1),.2,8,1)),{boundedView(editor.value("pan_x").toDouble(),-1000000,1000000,0),boundedView(editor.value("pan_y").toDouble(),-1000000,1000000,0)});
    workspaceReference=editor.value("reference").toString();workspaceWarning.clear();
    if(!preview->setReference(workspaceReference)){preview->setReference({});workspaceWarning="Falta el fondo de referencia; vuelve a elegirlo o quítalo.";}
    if(restorePreset)activePreset=editor.value("last_preset").toString();loading=previous;
}
void MapperEditor::refreshPresets() {
    QString error;presetEntries=presetStore->list(error);presetWarning=error;const bool previous=loading;loading=true;presetList->clear();presetList->addItem("Sin preset",QString());
    for(const auto &entry:presetEntries)presetList->addItem(entry.name,entry.id);
    presetList->setCurrentIndex(std::max(0,presetList->findData(activePreset)));loading=previous;if(!error.isEmpty()&&status)status->setText(error);
}
QString MapperEditor::savePresetAs(const QString &name,QString &error) {
    sync();const auto bytes=MapperPresets::encode(settings,name,error);if(bytes.isEmpty())return {};
    const auto id=presetStore->save(bytes,{},error);if(id.isEmpty())return {};activePreset=id;sync();refreshPresets();return id;
}
bool MapperEditor::savePreset(QString &error) {
    auto found=std::find_if(presetEntries.begin(),presetEntries.end(),[&](const PresetEntry &entry){return entry.id==activePreset;});
    if(found==presetEntries.end()){error="Elige Guardar como para crear un preset.";return false;}
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
    sync(); obs_source_update(draft,settings); dirty=false;
    PreviewState state; state.layout=layout; state.selected=selectedIndex; state.tool=tool;state.point=tool==1?vertexIndex:meshIndex;
    state.width=uint32_t(obs_data_get_int(settings,"canvas_width")); state.height=uint32_t(obs_data_get_int(settings,"canvas_height"));
    state.grid=checks["showGrid"]->isChecked(); state.zones=checks["showZones"]->isChecked();
    preview->setState(std::move(state));
}
void MapperEditor::applyEdit() {
    if(obs_source_removed(target)) { status->setText("La fuente original se ha eliminado. Cierra el editor sin aplicar."); return; }
    if(!validInput(obs_data_get_string(settings,"input_uuid"))) { status->setText("La entrada no está disponible o crea una referencia circular."); return; }
    sync(); obs_source_update(target,settings); accept();
}
QDoubleSpinBox *MapperEditor::number(const char *key,double min,double max,double step) {
    auto *spin=new QDoubleSpinBox; spin->setObjectName(key); spin->setRange(min,max); spin->setDecimals(2); spin->setSingleStep(step); spin->setKeyboardTracking(false); spin->setSuffix(" %"); numbers[key]=spin; return spin;
}
QCheckBox *MapperEditor::check(const char *key,const QString &label) { auto *box=new QCheckBox(label); box->setObjectName(key); checks[key]=box; return box; }
void MapperEditor::refreshSources() {
    const bool previous=loading; loading=true; sources->clear(); sources->addItem("Ninguna · transparente",QString());
    struct Context { QComboBox *combo; obs_source_t *target; } context{sources,target};
    auto enumerate=[](void *opaque,obs_source_t *source) {
        auto *context=static_cast<Context*>(opaque);
        if(source!=context->target && !obs_source_removed(source) && (obs_source_get_output_flags(source)&OBS_SOURCE_VIDEO) &&
            context->combo->findData(QString::fromUtf8(obs_source_get_uuid(source)))<0)
            context->combo->addItem(QString::fromUtf8(obs_source_get_name(source)),QString::fromUtf8(obs_source_get_uuid(source)));
        return true;
    };obs_enum_sources(enumerate,&context);obs_enum_scenes(enumerate,&context);
    const auto uuid=QString::fromUtf8(obs_data_get_string(settings,"input_uuid")); int index=sources->findData(uuid);
    if(index<0) { sources->addItem("Entrada no disponible",uuid); index=sources->count()-1; }
    sources->setCurrentIndex(index); loading=previous;
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
        zoneName->setText(text(zone->name)); checks["visible"]->setChecked(zone->visible);
        const auto d=zone->destination,u=zone->uv;
        numbers["dstX"]->setValue(d.x*100);numbers["dstY"]->setValue(d.y*100);numbers["dstW"]->setValue(d.w*100);numbers["dstH"]->setValue(d.h*100);
        numbers["uvX"]->setValue(u.x*100);numbers["uvY"]->setValue(u.y*100);numbers["uvW"]->setValue(u.w*100);numbers["uvH"]->setValue(u.h*100);
        checks["polygon"]->setChecked(zone->polygon.enabled); checks["mesh"]->setChecked(zone->mesh.enabled);
        vertices->clear(); for(size_t i=0;i<zone->polygon.vertices.size();++i) vertices->addItem("Vértice "+QString::number(i+1));
        vertexIndex=zone->polygon.vertices.empty()?0:std::min(vertexIndex,zone->polygon.vertices.size()-1); vertices->setCurrentIndex(int(vertexIndex));
        if(!zone->polygon.vertices.empty()) {
            auto p=zone->polygon.vertices[vertexIndex]; numbers["vertexX"]->setValue(p.x*100);numbers["vertexY"]->setValue(p.y*100);numbers["vertexU"]->setValue(p.u*100);numbers["vertexV"]->setValue(p.v*100);
        }
        columns->setValue(zone->mesh.columns); rows->setValue(zone->mesh.rows); checks["smooth"]->setChecked(zone->mesh.smooth);
        checks["soft"]->setChecked(zone->mesh.soft); checks["wave"]->setChecked(zone->mesh.wave);
        numbers["radius"]->setValue(zone->mesh.radius*100);numbers["strength"]->setValue(zone->mesh.strength*100);
        numbers["curveH"]->setValue(zone->mesh.horizontal*100);numbers["curveV"]->setValue(zone->mesh.vertical*100);
        meshPoints->clear(); for(size_t i=0;i<zone->mesh.points.size();++i) meshPoints->addItem(QString("Fila %1 · columna %2").arg(i/zone->mesh.columns+1).arg(i%zone->mesh.columns+1));
        meshIndex=zone->mesh.points.empty()?0:std::min(meshIndex,zone->mesh.points.size()-1); meshPoints->setCurrentIndex(int(meshIndex));
        if(!zone->mesh.points.empty()) { numbers["meshX"]->setValue(zone->mesh.points[meshIndex].x*100); numbers["meshY"]->setValue(zone->mesh.points[meshIndex].y*100); }
        maskKind->setCurrentIndex(std::clamp(zone->mask.kind,0,3)); maskPath->setText(text(zone->mask.path));
        checks["luminance"]->setChecked(zone->mask.luminance); checks["invert"]->setChecked(zone->mask.invert);
        numbers["maskStrength"]->setValue(zone->mask.strength*100);numbers["feather"]->setValue(zone->mask.feather*100);
        maskPath->setEnabled(zone->mask.kind==3); checks["luminance"]->setEnabled(zone->mask.kind==3);
        numbers["feather"]->setEnabled(zone->mask.kind==1||zone->mask.kind==2);
        for(const char *key:{"vertexX","vertexY","vertexU","vertexV"}) numbers[key]->setEnabled(zone->polygon.enabled);
        for(const char *key:{"meshX","meshY","curveH","curveV","radius","strength"}) numbers[key]->setEnabled(zone->mesh.enabled);
        const bool showPolygon=zone->polygon.enabled&&tool==1,showMesh=zone->mesh.enabled;
        geometryForm->setRowVisible(vertices,showPolygon);geometryForm->setRowVisible(vertexActionRow,showPolygon);
        for(const char *key:{"vertexX","vertexY","vertexU","vertexV"})geometryForm->setRowVisible(numbers[key],showPolygon);
        for(auto *widget:{static_cast<QWidget*>(columns),static_cast<QWidget*>(rows),static_cast<QWidget*>(checks["smooth"]),static_cast<QWidget*>(checks["wave"]),static_cast<QWidget*>(checks["soft"])})geometryForm->setRowVisible(widget,showMesh);
        geometryForm->setRowVisible(meshPoints,showMesh&&tool==2);geometryForm->setRowVisible(numbers["meshX"],showMesh&&tool==2);geometryForm->setRowVisible(numbers["meshY"],showMesh&&tool==2);
        for(const char *key:{"curveH","curveV","radius","strength"}) { auto *row=numbers[key]->parentWidget();geometryForm->setRowVisible(row,showMesh); }
        auto polygon=zone->polygon; mapping::compile_polygon(polygon); auto mesh=zone->mesh; auto surface=mapping::compile_surface(mesh,polygon);
        QString message=QString("%1 zonas · %2 × %3 · salida transparente").arg(layout.zones.size()).arg(canvasWidth->value()).arg(canvasHeight->value());
        if(!polygon.error.empty()) message=text(polygon.error)+" La zona queda transparente.";
        else if(!surface.error.empty()) message=text(surface.error)+" La zona queda transparente.";
        else if(zone->mask.kind==3 && !QFileInfo::exists(text(zone->mask.path))) message="Falta el archivo de máscara; la zona queda transparente.";
        status->setText(!workspaceWarning.isEmpty()?workspaceWarning:!presetWarning.isEmpty()?presetWarning:message);
    } else { zoneName->clear(); status->setText("Sin zonas · añade una para comenzar."); }
    undoButton->setEnabled(!undo.empty()); redoButton->setEnabled(!redo.empty()); loading=false;
    PreviewState state; state.layout=layout; state.selected=selectedIndex;state.tool=tool;state.point=tool==1?vertexIndex:meshIndex;state.width=uint32_t(canvasWidth->value());state.height=uint32_t(canvasHeight->value());
    state.grid=checks["showGrid"]->isChecked();state.zones=checks["showZones"]->isChecked(); preview->setState(std::move(state));
}
void MapperEditor::build() {
    setStyleSheet(R"CSS(
QDialog,QWidget{background:#09131d;color:#e5f0fa;font-family:'Segoe UI';font-size:12px;}
QGroupBox{background:#0c1a26;border:1px solid #243c50;border-radius:7px;margin-top:10px;padding:14px 8px 8px;font-weight:600;}
QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 4px;}
QPushButton{background:#172a3b;border:1px solid #304d66;border-radius:5px;padding:6px 9px;}
QPushButton:hover{background:#203e56;border-color:#23a6ff;} QPushButton:checked,QPushButton#applyEditor{background:#007cdf;border:1px solid #1ebcff;color:white;}
QPushButton:disabled{color:#647b8b;background:#10202d;}
QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox{background:#07121d;border:1px solid #2b455b;border-radius:4px;padding:5px;min-height:22px;}
QListWidget{background:#0a1723;border:1px solid #263f55;border-radius:5px;outline:none;}
QListWidget::item{padding:6px;border-bottom:1px solid #1b3041;} QListWidget::item:selected{background:#075a9d;color:white;}
QTabWidget::pane{border:1px solid #243e54;} QTabBar::tab{background:#152735;padding:10px 15px;}QTabBar::tab:selected{background:#075a9d;}
QScrollBar:vertical{background:#0b1721;width:9px;} QScrollBar::handle:vertical{background:#36546c;min-height:30px;border-radius:4px;}
QLabel#muted{color:#a1bed2;} QLabel#title{font-size:25px;font-weight:700;} QLabel#brand{color:#16b8ff;border:1px solid #7a44e7;border-radius:4px;padding:3px 7px;}
)CSS");
    auto *root=new QVBoxLayout(this); root->setContentsMargins(14,12,14,12); root->setSpacing(10);
    auto *header=new QHBoxLayout; auto *title=new QLabel("OBS Screen Mapper");title->setObjectName("title");header->addWidget(title);
    auto *brand=new QLabel("NEXUS");brand->setObjectName("brand");header->addWidget(brand); header->addStretch();
    header->addWidget(new QLabel("Preset:"));presetList=new QComboBox;presetList->setObjectName("presetList");presetList->setMinimumWidth(170);presetList->setMaximumWidth(240);header->addWidget(presetList);
    auto *save=button("Guardar","savePreset",header),*load=button("Cargar","loadPreset",header);
    auto *options=new QToolButton;options->setText("Presets ⋯");options->setObjectName("presetOptions");options->setPopupMode(QToolButton::InstantPopup);auto *menu=new QMenu(options);options->setMenu(menu);header->addWidget(options);
    auto *saveAs=menu->addAction("Guardar como…"),*importAction=menu->addAction("Importar…"),*exportAction=menu->addAction("Exportar…");
    auto promptSave=[this]{bool ok=false;auto name=QInputDialog::getText(this,"Guardar preset","Nombre del preset",QLineEdit::Normal,"Mi preset",&ok);if(!ok)return;QString error;if(savePresetAs(name,error).isEmpty())status->setText(error);else status->setText("Preset guardado en la biblioteca local.");};
    connect(saveAs,&QAction::triggered,this,promptSave);
    connect(save,&QPushButton::clicked,this,[this,promptSave]{const auto id=presetList->currentData().toString();if(id.isEmpty()){promptSave();return;}const auto previous=activePreset;activePreset=id;QString error;if(!savePreset(error)){activePreset=previous;status->setText(error);}else status->setText("Preset guardado.");});
    connect(load,&QPushButton::clicked,this,[this]{QString error;const auto id=presetList->currentData().toString();if(id.isEmpty()){status->setText("Elige un preset de la biblioteca.");return;}if(!loadPreset(id,error))status->setText(error);else status->setText("Preset cargado en la copia de trabajo. La fuente de entrada se mantiene; usa Aplicar para confirmar.");});
    connect(importAction,&QAction::triggered,this,[this]{auto path=QFileDialog::getOpenFileName(this,"Importar preset",{},"Preset Mapper (*.mapper.json *.json)");if(path.isEmpty())return;QString error;if(importPreset(path,error).isEmpty())status->setText(error);else status->setText("Preset importado y cargado en la copia de trabajo.");});
    connect(exportAction,&QAction::triggered,this,[this]{auto path=QFileDialog::getSaveFileName(this,"Exportar preset","mi-preset.mapper.json","Preset Mapper (*.mapper.json)");if(path.isEmpty())return;QString error;const QString name=presetList->currentData().toString().isEmpty()?QFileInfo(path).completeBaseName():presetList->currentText();if(!exportPreset(path,name,error))status->setText(error);else status->setText("Preset exportado con sus imágenes incluidas.");});
    undoButton=button("↶ Deshacer","undoEdit",header);redoButton=button("↷ Rehacer","redoEdit",header);
    connect(undoButton,&QPushButton::clicked,this,&MapperEditor::undoEdit);connect(redoButton,&QPushButton::clicked,this,&MapperEditor::redoEdit);root->addLayout(header);
    auto *subtitle=new QLabel("Mapea, divide y deforma tu vídeo en múltiples superficies");subtitle->setObjectName("muted");root->addWidget(subtitle);
    auto *split=new QSplitter;root->addWidget(split,1);
    auto *left=new QWidget;left->setMinimumWidth(240);left->setMaximumWidth(340);auto *leftLayout=new QVBoxLayout(left);leftLayout->setContentsMargins(0,0,5,0);
    auto *sourceGroup=group("1. FUENTE Y DISTRIBUCIÓN",leftLayout); auto *sourceLayout=new QVBoxLayout(sourceGroup);
    sourceLayout->addWidget(new QLabel("Fuente OBS existente"));sources=new QComboBox;sources->setObjectName("inputSource");sourceLayout->addWidget(sources);
    auto *reload=new QPushButton("Actualizar fuentes");sourceLayout->addWidget(reload);connect(reload,&QPushButton::clicked,this,&MapperEditor::refreshSources);
    distribution=new QComboBox;distribution->setObjectName("distribution");distribution->addItems({"Split","Repeat","Custom UV"});sourceLayout->addWidget(distribution);
    connect(sources,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){if(loading||index<0)return;const auto uuid=utf8(sources->itemData(index).toString());if(!validInput(uuid)){refresh();status->setText("Esta entrada crea un ciclo o no está disponible.");return;}change([&]{obs_data_set_string(settings,"input_uuid",uuid.c_str());});});
    connect(distribution,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){change([&]{layout.mode=index==1?mapping::Mode::Repeat:index==2?mapping::Mode::Custom:mapping::Mode::Split;obs_data_set_string(settings,"distribution",index==1?"repeat":index==2?"custom":"split");});});
    auto *zoneGroup=group("2. ZONAS",leftLayout);auto *zoneLayout=new QVBoxLayout(zoneGroup);auto *actions=new QHBoxLayout;
    connect(button("＋ Añadir","addZone",actions),&QPushButton::clicked,this,[this]{zoneAction("add");});connect(button("Duplicar","duplicateZone",actions),&QPushButton::clicked,this,[this]{zoneAction("duplicate");});connect(button("Eliminar","deleteZone",actions),&QPushButton::clicked,this,[this]{zoneAction("delete");});zoneLayout->addLayout(actions);
    zones=new QListWidget;zones->setObjectName("zoneList");zones->setDragDropMode(QAbstractItemView::InternalMove);zoneLayout->addWidget(zones,1);
    connect(zones,&QListWidget::currentRowChanged,this,[this](int index){if(!loading&&index>=0)QTimer::singleShot(0,this,[this,index]{select(size_t(index));});});
    connect(zones,&QListWidget::itemChanged,this,[this](QListWidgetItem *item){if(loading)return;const auto id=utf8(item->data(Qt::UserRole).toString());const auto label=utf8(item->text());const bool visible=item->checkState()==Qt::Checked;QTimer::singleShot(0,this,[this,id,label,visible]{change([&]{for(auto &z:layout.zones)if(z.id==id){z.name=label;z.visible=visible;}});});});
    connect(zones->model(),&QAbstractItemModel::rowsMoved,this,[this]{if(loading)return;std::vector<std::string> ids;for(int i=0;i<zones->count();++i)ids.push_back(utf8(zones->item(i)->data(Qt::UserRole).toString()));const auto selectedId=selected()?selected()->id:"";QTimer::singleShot(0,this,[this,ids,selectedId]{change([&]{std::vector<mapping::Zone> ordered;for(const auto &id:ids)for(const auto &z:layout.zones)if(z.id==id)ordered.push_back(z);layout.zones=std::move(ordered);for(size_t i=0;i<layout.zones.size();++i)if(layout.zones[i].id==selectedId)selectedIndex=i;});});});
    auto *order=new QHBoxLayout;connect(button("↑","zoneUp",order),&QPushButton::clicked,this,[this]{zoneAction("up");});connect(button("↓","zoneDown",order),&QPushButton::clicked,this,[this]{zoneAction("down");});connect(button("Distribuir","arrangeZones",order),&QPushButton::clicked,this,[this]{zoneAction("arrange");});zoneLayout->addLayout(order);leftLayout->setStretchFactor(zoneGroup,1);
    split->addWidget(left);
    auto *center=new QWidget;auto *central=new QVBoxLayout(center);central->setContentsMargins(6,0,6,0);auto *viewBar=new QHBoxLayout;viewBar->addWidget(new QLabel("OBS Screen Mapper Editor"));viewBar->addStretch();
    connect(button("−","zoomOut",viewBar),&QPushButton::clicked,this,[this]{preview->setZoom(float(zoomLabel->text().remove('%').toDouble()/100/1.2));});zoomLabel=new QLabel("100%");viewBar->addWidget(zoomLabel);
    connect(button("＋","zoomIn",viewBar),&QPushButton::clicked,this,[this]{preview->setZoom(float(zoomLabel->text().remove('%').toDouble()/100*1.2));});connect(button("Ajustar al lienzo","fitCanvas",viewBar),&QPushButton::clicked,this,[this]{preview->fit();});central->addLayout(viewBar);
    preview=new MapperPreview(draft);central->addWidget(preview,1);preview->zoomChanged=[this](float zoom){zoomLabel->setText(QString::number(zoom*100,'f',0)+"%");};
    preview->selectZone=[this](size_t index){select(index);};preview->beginEdit=[this]{beginGesture();};preview->endEdit=[this]{endGesture();};
    preview->selectPoint=[this](size_t index){if(tool==1)vertexIndex=index;else meshIndex=index;refresh();};
    preview->moveZone=[this](size_t index,QPointF delta){change([&]{auto &z=layout.zones[index];layout.automatic=false;z.destination.x=std::clamp(z.destination.x+float(delta.x()),0.0f,1-z.destination.w);z.destination.y=std::clamp(z.destination.y+float(delta.y()),0.0f,1-z.destination.h);},false);};
    preview->moveMesh=[this](size_t index,size_t point,QPointF delta){change([&]{meshIndex=point;mapping::move_control(layout.zones[index].mesh,point,float(delta.x()),float(delta.y()));},false);};
    preview->movePolygon=[this](size_t index,size_t point,QPointF delta){change([&]{auto &z=layout.zones[index];vertexIndex=point;auto &p=z.polygon.vertices[point];
        float dx=float(delta.x()),dy=float(delta.y());
        if(z.mesh.enabled && z.mesh.error.empty()) { const float epsilon=.0001f;auto base=mapping::deform(z.mesh,p),px=p,py=p;px.x+=epsilon;py.y+=epsilon;px=mapping::deform(z.mesh,px);py=mapping::deform(z.mesh,py);
            const float a=(px.x-base.x)/epsilon,b=(py.x-base.x)/epsilon,c=(px.y-base.y)/epsilon,d=(py.y-base.y)/epsilon,det=a*d-b*c;
            if(std::abs(det)>1e-6f){const float x=(d*dx-b*dy)/det;dy=(-c*dx+a*dy)/det;dx=x;}
        }p.x=mapping::bounded(p.x+dx);p.y=mapping::bounded(p.y+dy);mapping::compile_polygon(z.polygon);},false);};
    auto *tools=new QHBoxLayout;auto *toolGroup=new QButtonGroup(this);const char *names[]={"Seleccionar","Polígono","Editar malla","Mover lienzo"};
    for(int i=0;i<4;++i){auto *b=button(names[i],("tool"+std::to_string(i)).c_str(),tools);b->setCheckable(true);toolGroup->addButton(b,i);if(i==0)b->setChecked(true);}
    connect(toolGroup,&QButtonGroup::idClicked,this,[this](int value){tool=value;if(selected()&&((tool==1&&!selected()->polygon.enabled)||(tool==2&&!selected()->mesh.enabled)))change([&]{if(tool==1)selected()->polygon.enabled=true;else selected()->mesh.enabled=true;});else refresh();});central->addLayout(tools);
    auto *toggles=new QHBoxLayout;toggles->addWidget(check("showGrid","Cuadrícula"));toggles->addWidget(check("showZones","Mostrar zonas"));checks["showGrid"]->setChecked(true);checks["showZones"]->setChecked(true);
    for(const char *key:{"showGrid","showZones"}) connect(checks[key],&QCheckBox::toggled,this,[this]{if(!loading)refresh();});
    connect(button("Fondo de referencia…","referenceImage",toggles),&QPushButton::clicked,this,[this]{auto path=QFileDialog::getOpenFileName(this,"Fondo del editor",{},"Imágenes (*.png *.jpg *.jpeg *.bmp)");if(path.isEmpty())return;if(!preview->setReference(path))status->setText("No se pudo abrir la imagen de referencia.");else{workspaceReference=path;workspaceWarning.clear();}});
    connect(button("Quitar fondo","clearReference",toggles),&QPushButton::clicked,this,[this]{preview->setReference({});workspaceReference.clear();workspaceWarning.clear();refresh();});central->addLayout(toggles);split->addWidget(center);
    auto *right=new QWidget;right->setMinimumWidth(300);right->setMaximumWidth(390);auto *rightLayout=new QVBoxLayout(right);rightLayout->setContentsMargins(5,0,0,0);rightLayout->addWidget(new QLabel("3. PROPIEDADES DE LA ZONA"));
    auto *nameRow=new QHBoxLayout;zoneName=new QLineEdit;zoneName->setObjectName("zoneName");nameRow->addWidget(zoneName);nameRow->addWidget(check("visible","Visible"));rightLayout->addLayout(nameRow);
    connect(zoneName,&QLineEdit::editingFinished,this,[this]{if(selected()&&!loading){auto name=utf8(zoneName->text());change([&]{selected()->name=name.empty()?"Zona "+selected()->id:name;});}});
    connect(checks["visible"],&QCheckBox::toggled,this,[this](bool value){if(selected())change([&]{selected()->visible=value;});});
    tabs=new QTabWidget;rightLayout->addWidget(tabs,1);QFormLayout *geometry,*mask,*uv;scrollPage(tabs,"Geometría",geometry);scrollPage(tabs,"Máscara",mask);scrollPage(tabs,"Región UV",uv);
    geometryForm=geometry;
    geometry->addRow(check("polygon","Polygon"));geometry->addRow(check("mesh","Smooth Mesh"));
    connect(checks["polygon"],&QCheckBox::toggled,this,[this](bool enabled){if(selected())change([&]{selected()->polygon.enabled=enabled;});});
    connect(checks["mesh"],&QCheckBox::toggled,this,[this](bool enabled){if(selected())change([&]{selected()->mesh.enabled=enabled;});});
    for(const auto &field:{std::pair<const char *,const char *>{"dstX","Destino X"},{"dstY","Destino Y"},{"dstW","Ancho"},{"dstH","Alto"}})geometry->addRow(field.second,number(field.first,0,100));
    auto updateDestination=[this]{if(loading||!selected())return;change([&]{layout.automatic=false;selected()->destination=mapping::sanitize({float(numbers["dstX"]->value()/100),float(numbers["dstY"]->value()/100),float(numbers["dstW"]->value()/100),float(numbers["dstH"]->value()/100)});});};
    for(const char *key:{"dstX","dstY","dstW","dstH"})connect(numbers[key],qOverload<double>(&QDoubleSpinBox::valueChanged),this,updateDestination);
    vertices=new QComboBox;vertices->setObjectName("polygonVertex");geometry->addRow("Vértice",vertices);connect(vertices,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){if(!loading&&index>=0){vertexIndex=size_t(index);refresh();}});
    auto *vertexActions=new QHBoxLayout;vertexActionRow=vertexActions;connect(button("＋ Punto","addVertex",vertexActions),&QPushButton::clicked,this,[this]{if(!selected())return;change([&]{auto &p=selected()->polygon.vertices;if(p.empty()){p=mapping::Polygon{}.vertices;return;}auto a=p[vertexIndex],b=p[(vertexIndex+1)%p.size()];p.insert(p.begin()+vertexIndex+1,{(a.x+b.x)/2,(a.y+b.y)/2,(a.u+b.u)/2,(a.v+b.v)/2});++vertexIndex;});});
    connect(button("− Punto","deleteVertex",vertexActions),&QPushButton::clicked,this,[this]{if(selected()&&selected()->polygon.vertices.size()>3)change([&]{auto &p=selected()->polygon.vertices;p.erase(p.begin()+vertexIndex);vertexIndex=std::min(vertexIndex,p.size()-1);});});geometry->addRow(vertexActions);
    for(const auto &field:{std::pair<const char *,const char *>{"vertexX","Punto X"},{"vertexY","Punto Y"},{"vertexU","Punto U"},{"vertexV","Punto V"}})geometry->addRow(field.second,number(field.first,0,100));
    for(const char *key:{"vertexX","vertexY","vertexU","vertexV"})connect(numbers[key],qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{if(loading||!selected()||selected()->polygon.vertices.empty())return;change([&]{selected()->polygon.vertices[vertexIndex]={float(numbers["vertexX"]->value()/100),float(numbers["vertexY"]->value()/100),float(numbers["vertexU"]->value()/100),float(numbers["vertexV"]->value()/100)};});});
    columns=new QSpinBox;columns->setObjectName("meshColumns");columns->setRange(2,16);rows=new QSpinBox;rows->setObjectName("meshRows");rows->setRange(2,16);geometry->addRow("Columnas",columns);geometry->addRow("Filas",rows);
    for(auto *spin:{columns,rows})connect(spin,qOverload<int>(&QSpinBox::valueChanged),this,[this]{if(selected())change([&]{mapping::resize_mesh(selected()->mesh,columns->value(),rows->value());});});
    geometry->addRow(check("smooth","Suavizado de malla"));geometry->addRow(check("wave","Perfil de onda"));
    auto sliderRow=[&](const char *key,const QString &label,int min,int max) {
        auto *body=new QWidget;auto *row=new QHBoxLayout(body);row->setContentsMargins(0,0,0,0);auto *slider=new QSlider(Qt::Horizontal);slider->setRange(min,max);slider->setObjectName(QString(key)+"Slider");
        auto *spin=number(key,min,max);spin->setMaximumWidth(92);row->addWidget(slider,1);row->addWidget(spin);geometry->addRow(label,body);
        connect(slider,&QSlider::valueChanged,spin,[spin](int value){spin->setValue(value);});connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),slider,[slider](double value){slider->setValue(int(value));});
        connect(slider,&QSlider::sliderPressed,this,[this]{beginGesture();});connect(slider,&QSlider::sliderReleased,this,[this]{endGesture();});
    };
    sliderRow("curveH","Curvatura horizontal",-50,50);sliderRow("curveV","Curvatura vertical",-50,50);
    geometry->addRow(check("soft","Selección suave"));sliderRow("radius","Radio",0,150);sliderRow("strength","Fuerza",0,100);
    meshPoints=new QComboBox;meshPoints->setObjectName("meshPoint");geometry->addRow("Punto de malla",meshPoints);connect(meshPoints,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){if(!loading&&index>=0){meshIndex=size_t(index);refresh();}});
    geometry->addRow("Desplazamiento X",number("meshX",-100,100));geometry->addRow("Desplazamiento Y",number("meshY",-100,100));
    for(const char *key:{"smooth","wave","soft"})connect(checks[key],&QCheckBox::toggled,this,[this,key](bool value){if(selected())change([&]{auto &m=selected()->mesh;if(std::string(key)=="smooth")m.smooth=value;else if(std::string(key)=="wave")m.wave=value;else m.soft=value;});});
    for(const char *key:{"curveH","curveV","radius","strength"})connect(numbers[key],qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,key](double value){if(selected())change([&]{auto &m=selected()->mesh;const std::string name=key;if(name=="curveH")m.horizontal=float(value/100);else if(name=="curveV")m.vertical=float(value/100);else if(name=="radius")m.radius=float(value/100);else m.strength=float(value/100);});});
    for(const char *key:{"meshX","meshY"})connect(numbers[key],qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{if(loading||!selected()||selected()->mesh.points.empty())return;change([&]{auto &m=selected()->mesh;mapping::move_control(m,meshIndex,float(numbers["meshX"]->value()/100)-m.points[meshIndex].x,float(numbers["meshY"]->value()/100)-m.points[meshIndex].y);});});
    auto *resetMesh=new QPushButton("Restablecer malla");resetMesh->setObjectName("resetMesh");geometry->addRow(resetMesh);connect(resetMesh,&QPushButton::clicked,this,[this]{if(selected())change([&]{const bool enabled=selected()->mesh.enabled;selected()->mesh=mapping::Mesh{};selected()->mesh.enabled=enabled;});});
    maskKind=new QComboBox;maskKind->setObjectName("maskType");maskKind->addItems({"Sin máscara","Elipse","Rectángulo","Imagen PNG / SVG"});mask->addRow("Máscara",maskKind);connect(maskKind,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int value){if(selected())change([&]{selected()->mask.kind=value;});});
    maskPath=new QLineEdit;maskPath->setObjectName("maskFile");mask->addRow("Archivo",maskPath);connect(maskPath,&QLineEdit::editingFinished,this,[this]{if(selected()&&!loading){auto path=utf8(maskPath->text());change([&]{selected()->mask.path=path;});}});
    auto *browse=new QPushButton("Elegir PNG / SVG…");browse->setObjectName("browseMask");mask->addRow(browse);connect(browse,&QPushButton::clicked,this,[this]{auto path=QFileDialog::getOpenFileName(this,"Máscara de zona",{},"Máscaras (*.png *.svg)");if(!path.isEmpty()&&selected())change([&]{selected()->mask.path=utf8(path);selected()->mask.kind=3;});});
    mask->addRow(check("luminance","Luminancia × alfa"));mask->addRow(check("invert","Invertir"));mask->addRow("Intensidad",number("maskStrength",0,100));mask->addRow("Suavizado de borde",number("feather",0,25));
    for(const char *key:{"luminance","invert"})connect(checks[key],&QCheckBox::toggled,this,[this,key](bool value){if(selected())change([&]{if(std::string(key)=="invert")selected()->mask.invert=value;else selected()->mask.luminance=value;});});
    for(const char *key:{"maskStrength","feather"})connect(numbers[key],qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,key](double value){if(selected())change([&]{if(std::string(key)=="feather")selected()->mask.feather=float(value/100);else selected()->mask.strength=float(value/100);});});
    auto *reloadMask=new QPushButton("Recargar archivo");reloadMask->setObjectName("reloadMask");mask->addRow(reloadMask);connect(reloadMask,&QPushButton::clicked,this,[this]{if(selected())change([&]{auto &revision=selected()->mask.revision;revision=revision<9223372036854775807LL?revision+1:0;});});
    for(const auto &field:{std::pair<const char *,const char *>{"uvX","Región X"},{"uvY","Región Y"},{"uvW","Ancho UV"},{"uvH","Alto UV"}})uv->addRow(field.second,number(field.first,0,100));
    for(const char *key:{"uvX","uvY","uvW","uvH"})connect(numbers[key],qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{if(loading||!selected())return;change([&]{layout.mode=mapping::Mode::Custom;obs_data_set_string(settings,"distribution","custom");selected()->uv=mapping::sanitize({float(numbers["uvX"]->value()/100),float(numbers["uvY"]->value()/100),float(numbers["uvW"]->value()/100),float(numbers["uvH"]->value()/100)});});});
    auto *resetZone=new QPushButton("Restablecer zona");resetZone->setObjectName("resetZone");rightLayout->addWidget(resetZone);connect(resetZone,&QPushButton::clicked,this,[this]{if(selected())change([&]{auto old=*selected();*selected()=mapping::Zone{};selected()->id=old.id;selected()->name=old.name;selected()->destination=old.destination;});});split->addWidget(right);split->setSizes({280,820,340});
    auto *bottom=new QHBoxLayout;bottom->addWidget(new QLabel("4. LIENZO Y SALIDA"));canvasWidth=new QSpinBox;canvasHeight=new QSpinBox;canvasWidth->setObjectName("canvasWidth");canvasHeight->setObjectName("canvasHeight");for(auto *spin:{canvasWidth,canvasHeight}){spin->setRange(16,16384);spin->setKeyboardTracking(false);bottom->addWidget(spin);}bottom->addWidget(new QLabel("Fondo transparente · preview en vivo"));bottom->addStretch();
    connect(canvasWidth,qOverload<int>(&QSpinBox::valueChanged),this,[this](int value){change([&]{obs_data_set_int(settings,"canvas_width",value);});});connect(canvasHeight,qOverload<int>(&QSpinBox::valueChanged),this,[this](int value){change([&]{obs_data_set_int(settings,"canvas_height",value);});});
    connect(button("Cancelar","cancelEditor",bottom),&QPushButton::clicked,this,&QDialog::reject);connect(button("Aplicar","applyEditor",bottom),&QPushButton::clicked,this,&MapperEditor::applyEdit);root->addLayout(bottom);
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
