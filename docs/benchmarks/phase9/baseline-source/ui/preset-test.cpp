// SPDX-License-Identifier: GPL-2.0-or-later
#include "presets.hpp"
#include "../src/zones.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <cstdio>
#include <cstdlib>
static void check(bool ok,const char *label) {std::printf("%s: %s\n",ok?"PASS":"FAIL",label);std::fflush(stdout);if(!ok)std::exit(1);}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);check(argc==2,"preset test directory supplied");QDir().mkpath(QString::fromLocal8Bit(argv[1]));
    QTemporaryDir fixture(QString::fromLocal8Bit(argv[1])+"/presets-XXXXXX");check(fixture.isValid(),"isolated preset directory created");
    const QString png=fixture.filePath(QString::fromUtf8("máscara-ñ.png")),svg=fixture.filePath("shape.svg");
    QImage image(8,8,QImage::Format_RGBA8888);image.fill(QColor(255,255,255,128));check(image.save(png),"PNG preset asset created");
    QString error;check(MapperPresets::writeFile(svg,"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 8 8\"><path d=\"M0 0H8V8H0Z\" fill=\"white\"/></svg>",error),"SVG preset asset created");
    auto *settings=obs_data_create();obs_data_set_int(settings,"canvas_width",1920);obs_data_set_int(settings,"canvas_height",1080);obs_data_set_string(settings,"input_uuid","never-export-this-source");obs_data_set_string(settings,"distribution","custom");
    mapping::Layout layout;layout.mode=mapping::Mode::Custom;layout.automatic=false;
    for(int i=0;i<9;++i){mapping::Zone zone;zone.id=std::to_string(i+1);zone.name=i==0?"Zona ñ":"Zona "+zone.id;zone.destination={float(i)/9,0,1.0f/9,1};zone.uv={.1f,.2f,.3f,.4f};zone.visible=i!=2;zone.polygon.enabled=true;zone.mesh.enabled=true;zone.mesh.points[5]={.02f,.03f};zone.mesh.horizontal=.1f;zone.mesh.soft=true;zone.mesh.strength=.7f;if(i<2){zone.mask.kind=3;zone.mask.path=png.toUtf8().toStdString();}if(i==2){zone.mask.kind=3;zone.mask.path=svg.toUtf8().toStdString();}layout.zones.push_back(std::move(zone));}
    mapping::write(settings,layout);obs_data_set_string(settings,"selected_zone","1");
    auto *preferences=obs_data_create_from_json(QJsonDocument(QJsonObject{{"version",1},{"zoom",1.75},{"pan_x",22},{"pan_y",-13},{"grid",false},{"zones",true},{"tool",2},{"vertex",2},{"control",5},{"reference",png},{"last_preset","private-local-id"}}).toJson().constData());obs_data_set_obj(settings,"editor",preferences);obs_data_release(preferences);
    const auto packed=MapperPresets::encode(settings,QString::fromUtf8("Escenario ñ"),error);check(!packed.isEmpty()&&error.isEmpty(),"full configuration encoded as portable preset");
    QJsonObject document;check(MapperPresets::validate(packed,document,error),"portable preset validates");
    check(document.value("assets").toArray().size()==2,"duplicate mask and reference assets are packed once");
    check(!packed.contains("never-export-this-source")&&!packed.contains(png.toUtf8())&&!packed.contains("private-local-id"),"export omits source UUID, original paths and local preset identity");
    MapperPresets store(fixture.filePath("library"));QJsonObject config;QString name;
    check(store.materialize(packed,config,name,error),"portable assets materialize into owned cache");
    auto *restored=obs_data_create_from_json(QJsonDocument(config).toJson().constData());const auto loaded=mapping::read(restored);
    check(loaded.zones.size()==9&&loaded.zones[0].name=="Zona ñ"&&loaded.zones[0].polygon.enabled&&loaded.zones[0].mesh.enabled&&loaded.zones[0].mesh.points[5].x==.02f&&loaded.zones[0].mesh.strength==.7f&&!loaded.zones[2].visible&&loaded.zones[0].uv.x==.1f,"zones, Polygon, mesh, UV, visibility and Soft Selection survive round trip");
    const auto cached=QString::fromUtf8(loaded.zones[0].mask.path.c_str());check(QFileInfo::exists(cached)&&cached.startsWith(store.directory()+"/assets/"),"asset references resolve only inside the preset cache");obs_data_release(restored);
    const auto id=store.save(packed,{},error);check(!id.isEmpty()&&store.list(error).size()==1,"local library saves a preset with a generated identity");
    MapperPresets reopened(store.directory());check(reopened.list(error).size()==1&&!reopened.load(id,error).isEmpty(),"local library survives reopening");
    check(store.save(packed,id,error)==id&&store.list(error).size()==1,"saving existing preset updates it without duplicating library entry");
    check(!store.save(packed,{},error).isEmpty()&&store.list(error).size()==2,"Save As creates an independent preset");
    const auto entries=store.list(error);check(entries[0].name!=entries[1].name,"duplicate preset names receive distinct labels");
    const auto exported=fixture.filePath(QString::fromUtf8("portátil.mapper.json"));check(MapperPresets::writeFile(exported,packed,error)&&MapperPresets::readFile(exported,error)==packed,"portable preset file round trip supports Unicode names");
    const auto original=MapperPresets::readFile(exported,error);check(!MapperPresets::writeFile(fixture.path(),packed,error)&&MapperPresets::readFile(exported,error)==original,"failed atomic save leaves previous preset intact");
    const auto indexPath=QDir(store.directory()).filePath("library.json");const auto committed=store.load(id,error);
    check(QFile::setPermissions(indexPath,QFileDevice::ReadOwner|QFileDevice::ReadUser),"index made read-only for atomic commit failure test");
    auto update=document;update["name"]="Changed after failure";
    check(store.save(QJsonDocument(update).toJson(),id,error).isEmpty()&&store.load(id,error)==committed,"index commit failure keeps previously committed preset revision unchanged");
    check(QFile::setPermissions(indexPath,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ReadUser|QFileDevice::WriteUser),"index permissions restored after fixture test");
    auto reject=[&](QJsonObject bad,const char *label){QJsonObject result;QString failure;check(!MapperPresets::validate(QJsonDocument(bad).toJson(),result,failure)&&!failure.isEmpty(),label);};
    auto bad=document;bad["version"]=99;reject(bad,"future preset format version rejected");
    bad=document;auto changed=bad.value("settings").toObject();changed["mapping_schema"]=999;bad["settings"]=changed;reject(bad,"future mapping schema rejected");
    bad=document;changed=bad.value("settings").toObject();changed["canvas_width"]=17.5;bad["settings"]=changed;reject(bad,"fractional canvas dimensions rejected");
    bad=document;changed=bad.value("settings").toObject();auto prefs=changed.value("editor").toObject();prefs["version"]=2;changed["editor"]=prefs;bad["settings"]=changed;reject(bad,"future editor preferences version rejected");
    bad=document;changed=bad.value("settings").toObject();auto zones=changed.value("zones").toArray();auto zone=zones[1].toObject();zone["id"]=zones[0].toObject().value("id");zones[1]=zone;changed["zones"]=zones;bad["settings"]=changed;reject(bad,"duplicate zone identities rejected");
    bad=document;auto assets=bad.value("assets").toArray();auto asset=assets[0].toObject();asset["data"]="!!!";assets[0]=asset;bad["assets"]=assets;reject(bad,"invalid Base64 image rejected");
    bad=document;assets=bad.value("assets").toArray();asset=assets[0].toObject();asset["sha256"]=QString(64,'0');assets[0]=asset;bad["assets"]=assets;reject(bad,"asset integrity mismatch rejected");
    bad=document;changed=bad.value("settings").toObject();zones=changed.value("zones").toArray();zone=zones[0].toObject();zone["mask_path"]="../escape.png";zones[0]=zone;changed["zones"]=zones;bad["settings"]=changed;reject(bad,"path traversal reference rejected");
    bad=document;changed=bad.value("settings").toObject();zones=changed.value("zones").toArray();QJsonArray vertices,controls;
    for(int i=0;i<512;++i){const double angle=i*6.283185307179586/512;vertices.append(QJsonObject{{"x",.5+.45*std::cos(angle)},{"y",.5+.45*std::sin(angle)},{"u",.5},{"v",.5}});}
    for(int i=0;i<256;++i)controls.append(QJsonObject{{"x",0},{"y",0}});
    for(int i=0;i<2;++i){zone=zones[i].toObject();zone["polygon_vertices"]=vertices;zone["mesh_points"]=controls;zone["mesh_columns"]=16;zone["mesh_rows"]=16;zones[i]=zone;}changed["zones"]=zones;bad["settings"]=changed;reject(bad,"oversized derived geometry budget rejected before tessellation");
    QJsonObject result;check(!MapperPresets::validate("not json",result,error),"corrupt JSON rejected");
    bad=document;changed=bad.value("settings").toObject();changed["input_uuid"]="attempted-source-replacement";bad["settings"]=changed;
    check(MapperPresets::validate(QJsonDocument(bad).toJson(),result,error)&&!result.value("settings").toObject().contains("input_uuid"),"import strips any attempted source binding");
    MapperPresets failed(fixture.filePath("invalid-library"));config=QJsonObject{{"untouched",true}};bad=document;assets=bad.value("assets").toArray();asset=assets[0].toObject();asset["id"]="../../outside.png";assets[0]=asset;bad["assets"]=assets;
    check(!failed.materialize(QJsonDocument(bad).toJson(),config,name,error)&&config.value("untouched").toBool()&&!QFileInfo::exists(failed.directory()),"invalid import performs no extraction and leaves destination configuration unchanged");
    layout.zones[0].mask.path=fixture.filePath("missing.png").toUtf8().toStdString();mapping::write(settings,layout);
    check(MapperPresets::encode(settings,"Missing asset",error).isEmpty(),"missing image prevents incomplete portable export");
    check(MapperPresets::writeFile(QDir(store.directory()).filePath("library.json"),"broken",error),"corrupt-index fixture written");
    check(store.list(error).isEmpty()&&!error.isEmpty(),"corrupt library index reports error");
    check(store.save(packed,{},error).isEmpty()&&MapperPresets::readFile(QDir(store.directory()).filePath("library.json"),error)=="broken","save never overwrites an invalid library index silently");
    check(!QDir(store.directory()).entryList({"*.mapper.json"},QDir::Files).isEmpty(),"preset revision files remain available for manual reimport after index corruption");
    QJsonObject legacyConfig{{"mapping_schema",1},{"canvas_width",1920},{"canvas_height",1080},{"distribution","repeat"},{"zones",QJsonArray{QJsonObject{{"id","legacy"},{"name","Zona antigua"},{"visible",true}}}}};
    QJsonObject legacy{{"format","obs-screen-mapper-preset"},{"version",1},{"name","Legacy"},{"settings",legacyConfig},{"assets",QJsonArray{}}};
    MapperPresets migration(fixture.filePath("migration"));check(migration.materialize(QJsonDocument(legacy).toJson(),config,name,error),"older mapping schema imports with optional geometry fields absent");
    restored=obs_data_create_from_json(QJsonDocument(config).toJson().constData());auto migrated=mapping::read(restored);check(migrated.zones.size()==1&&!migrated.zones[0].mesh.enabled&&!migrated.zones[0].polygon.enabled&&migrated.zones[0].mask.kind==0,"older preset preserves rectangular rendering defaults");obs_data_release(restored);
    legacyConfig["zones"]=QJsonArray{};legacy["settings"]=legacyConfig;check(migration.materialize(QJsonDocument(legacy).toJson(),config,name,error)&&config.value("zones").toArray().isEmpty(),"empty zone layout survives portable preset import");
    obs_data_release(settings);std::puts("PASS: portable preset and persistence validation complete");return 0;
}
