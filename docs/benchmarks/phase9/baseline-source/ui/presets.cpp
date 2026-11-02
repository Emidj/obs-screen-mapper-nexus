// SPDX-License-Identifier: GPL-2.0-or-later
#include "presets.hpp"
#include "../src/zones.hpp"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QUuid>
#include <QBuffer>
#include <QImageReader>
#include <QXmlStreamReader>
#include <QSet>
#include <QMap>
#include <functional>

namespace {
constexpr qint64 fileLimit=96*1024*1024,assetLimit=32*1024*1024,totalAssetLimit=64*1024*1024;
bool fail(QString &error,const QString &message) {error=message;return false;}
QString hash(const QByteArray &bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()); }
bool number(const QJsonObject &object,const QString &key,double min,double max,bool integer=false,bool required=false) {
    if(!object.contains(key)) return !required;
    auto value=object.value(key);const double n=value.toDouble();return value.isDouble()&&std::isfinite(n)&&n>=min&&n<=max&&(!integer||n==std::floor(n));
}
bool boolean(const QJsonObject &object,const QString &key) {return !object.contains(key)||object.value(key).isBool();}
bool text(const QJsonObject &object,const QString &key,int limit,bool required=false) {
    if(!object.contains(key))return !required;
    auto value=object.value(key);return value.isString()&&value.toString().size()<=limit&&!value.toString().contains(QChar(0));
}
bool imageData(const QByteArray &bytes,const QString &extension,QString &error) {
    if(bytes.isEmpty()||bytes.size()>assetLimit)return fail(error,"Una imagen está vacía o supera 32 MiB.");
    if(extension=="svg") {
        if(bytes.size()>16*1024*1024)return fail(error,"El SVG supera 16 MiB.");
        QXmlStreamReader xml(bytes);bool svg=false;
        while(!xml.atEnd()) {xml.readNext();if(xml.tokenType()==QXmlStreamReader::DTD)return fail(error,"El SVG contiene una declaración DTD no admitida.");
            if(xml.isStartElement()&&!svg){if(xml.name()!=QStringLiteral("svg"))return fail(error,"La imagen no contiene un documento SVG.");svg=true;}}
        return svg&&!xml.hasError()?true:fail(error,"El SVG no es un XML válido.");
    }
    if(extension!="png"&&extension!="jpg"&&extension!="bmp")return fail(error,"Formato de imagen no admitido.");
    QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer,extension.toLatin1());
    auto size=reader.size();if(!size.isValid()||size.width()>4096||size.height()>4096)return fail(error,"La imagen debe medir entre 1 y 4096 píxeles por dimensión.");
    if(reader.read().isNull())return fail(error,"No se puede decodificar la imagen incluida.");return true;
}
bool assetId(const QString &id) {static QRegularExpression pattern("^[0-9a-f]{64}\\.(png|svg|jpg|bmp)$");return pattern.match(id).hasMatch();}
bool presetId(const QString &id) {return !QUuid(id).isNull()&&QUuid(id).toString(QUuid::WithoutBraces)==id;}
bool validateSettings(const QJsonObject &settings,QString &error) {
    if(!number(settings,"mapping_schema",1,4,true)||!number(settings,"canvas_width",16,16384,true,true)||!number(settings,"canvas_height",16,16384,true,true))return fail(error,"Versión o tamaño de canvas no admitidos.");
    const auto mode=settings.value("distribution").toString();if(mode!="split"&&mode!="repeat"&&mode!="custom")return fail(error,"Distribución desconocida.");
    if(!boolean(settings,"automatic_layout")||!text(settings,"selected_zone",128)||!settings.value("zones").isArray())return fail(error,"Estructura de zonas no válida.");
    QSet<QString> ids;qint64 surfaceVertices=0,pointsTotal=0;
    for(auto value:settings.value("zones").toArray()) {
        if(!value.isObject())return fail(error,"Una zona no es un objeto válido.");const auto zone=value.toObject();
        const auto id=zone.value("id").toString();if(id.isEmpty()||!text(zone,"id",128,true)||ids.contains(id)||!text(zone,"name",256,true))return fail(error,"Identificadores o nombres de zona no válidos.");ids.insert(id);
        for(const auto &key:{"visible","polygon_enabled","mesh_enabled","mesh_smooth","mesh_soft","mesh_wave","mask_luminance","mask_invert"})if(!boolean(zone,key))return fail(error,"Una opción de zona tiene un tipo incorrecto.");
        for(const QString prefix:{QString("dst_"),QString("uv_")}) {
            for(const auto &suffix:{"x","y","w","h"})if(!number(zone,prefix+suffix,0,1))return fail(error,"Destino o región UV fuera de rango.");
            if(zone.value(prefix+"x").toDouble()+zone.value(prefix+"w").toDouble(1)>1.000001||zone.value(prefix+"y").toDouble()+zone.value(prefix+"h").toDouble(1)>1.000001)return fail(error,"Destino o región UV excede sus límites.");
        }
        const int columns=zone.value("mesh_columns").toInt(4),rows=zone.value("mesh_rows").toInt(4);
        if(!number(zone,"mesh_columns",2,16,true)||!number(zone,"mesh_rows",2,16,true))return fail(error,"Resolución de malla no válida.");
        int polygonPoints=4;
        if(zone.contains("polygon_vertices")) {
            if(!zone.value("polygon_vertices").isArray())return fail(error,"Vértices Polygon no válidos.");
            const auto vertices=zone.value("polygon_vertices").toArray();polygonPoints=int(vertices.size());
            if(polygonPoints>512)return fail(error,"El preset supera 512 vértices en un contorno.");
            for(auto vertex:vertices) {if(!vertex.isObject())return fail(error,"Vértice Polygon no válido.");for(const auto &key:{"x","y","u","v"})if(!number(vertex.toObject(),key,0,1,false,true))return fail(error,"Coordenadas Polygon fuera de rango.");}
        }
        if(zone.contains("mesh_points")) {
            if(!zone.value("mesh_points").isArray()||zone.value("mesh_points").toArray().size()!=columns*rows)return fail(error,"La cantidad de puntos no coincide con la malla.");
            for(auto point:zone.value("mesh_points").toArray())if(!point.isObject()||!number(point.toObject(),"x",-1,1,false,true)||!number(point.toObject(),"y",-1,1,false,true))return fail(error,"Desplazamientos de malla fuera de rango.");
        }
        for(const auto &key:{"mesh_horizontal","mesh_vertical"})if(!number(zone,key,-.5,.5))return fail(error,"Curvatura fuera de rango.");
        if(!number(zone,"mesh_radius",0,1.5)||!number(zone,"mesh_strength",0,1)||!number(zone,"mask_kind",0,3,true)||!number(zone,"mask_strength",0,1)||!number(zone,"mask_feather",0,.25)||!text(zone,"mask_path",256))return fail(error,"Parámetros de máscara o selección suave no válidos.");
        pointsTotal+=columns*rows+polygonPoints;
        if(zone.value("mesh_enabled").toBool()) {const int subdivisions=std::clamp(std::max(columns-1,rows-1)*4,8,32);surfaceVertices+=qint64(zone.value("polygon_enabled").toBool()?std::max(0,polygonPoints-2):2)*subdivisions*subdivisions*3;}
        if(pointsTotal>1000000||surfaceVertices>2000000)return fail(error,"El preset supera el presupuesto de geometría de importación.");
    }
    if(settings.contains("editor")) {
        if(!settings.value("editor").isObject())return fail(error,"Preferencias de editor no válidas.");const auto editor=settings.value("editor").toObject();
        for(const auto &key:{"grid","zones"})if(!boolean(editor,key))return fail(error,"Preferencias de editor no válidas.");
        if(!number(editor,"version",1,1,true)||!number(editor,"zoom",.2,8)||!number(editor,"pan_x",-1000000,1000000)||!number(editor,"pan_y",-1000000,1000000)||!number(editor,"tool",0,3,true)||!number(editor,"vertex",0,511,true)||!number(editor,"control",0,255,true)||!text(editor,"reference",256))return fail(error,"Preferencias de vista fuera de rango o versión no admitida.");
    }
    return true;
}
bool references(QJsonObject &settings,const std::function<bool(QString&,bool)> &visit) {
    QJsonArray zones=settings.value("zones").toArray();
    for(qsizetype i=0;i<zones.size();++i){auto zone=zones[i].toObject();QString path=zone.value("mask_path").toString();if(!path.isEmpty()&&!visit(path,true))return false;zone["mask_path"]=path;zone["mask_revision"]=0;zones[i]=zone;}
    settings["zones"]=zones;
    if(settings.contains("editor")){auto editor=settings.value("editor").toObject();QString path=editor.value("reference").toString();if(!path.isEmpty()&&!visit(path,false))return false;editor["reference"]=path;editor.remove("last_preset");settings["editor"]=editor;}
    return true;
}
}
QByteArray MapperPresets::readFile(const QString &path,QString &error) {
    error.clear();QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>fileLimit){error="No se puede leer el preset o supera 96 MiB.";return {};}
    return file.readAll();
}
bool MapperPresets::writeFile(const QString &path,const QByteArray &bytes,QString &error) {
    error.clear();QSaveFile file(path);file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())return fail(error,"No se pudo guardar el archivo de forma atómica: "+file.errorString());return true;
}
QByteArray MapperPresets::encode(obs_data_t *settings,const QString &name,QString &error) {
    error.clear();if(name.trimmed().isEmpty()||name.size()>128||name.contains(QChar(0))){error="Usa un nombre de preset entre 1 y 128 caracteres.";return {};}
    auto input=QJsonDocument::fromJson(QByteArray(obs_data_get_json(settings))).object();QJsonObject config;
    for(const auto &key:{"mapping_schema","canvas_width","canvas_height","distribution","automatic_layout","selected_zone","zones","editor"})if(input.contains(key))config[key]=input.value(key);
    if(!config.contains("canvas_width"))config["canvas_width"]=double(obs_data_get_int(settings,"canvas_width"));
    if(!config.contains("canvas_height"))config["canvas_height"]=double(obs_data_get_int(settings,"canvas_height"));
    if(!config.contains("distribution"))config["distribution"]=QString::fromUtf8(obs_data_get_string(settings,"distribution"));
    if(!validateSettings(config,error))return {};
    QMap<QString,QJsonObject> assets;qint64 total=0;
    if(!references(config,[&](QString &path,bool mask){
        QFile file(path);QString extension=QFileInfo(path).suffix().toLower();if(extension=="jpeg")extension="jpg";
        if(mask&&extension!="png"&&extension!="svg")return fail(error,"Las máscaras portables deben ser PNG o SVG.");
        if(!mask&&extension=="svg")return fail(error,"El fondo de referencia debe ser PNG, JPG o BMP.");
        if(!file.open(QIODevice::ReadOnly)||file.size()>assetLimit)return fail(error,"Falta un archivo de imagen o supera 32 MiB: "+QFileInfo(path).fileName());
        auto bytes=file.readAll();if(!imageData(bytes,extension,error))return false;const QString id=hash(bytes)+"."+extension;
        if(!assets.contains(id)){total+=bytes.size();if(total>totalAssetLimit)return fail(error,"Las imágenes del preset superan 64 MiB en total.");assets[id]=QJsonObject{{"id",id},{"sha256",hash(bytes)},{"data",QString::fromLatin1(bytes.toBase64())}};}
        path="asset:"+id;return true;
    }))return {};
    QJsonArray packed;for(const auto &asset:assets)packed.append(asset);
    QJsonObject root{{"format","obs-screen-mapper-preset"},{"version",1},{"name",name.trimmed()},{"settings",config},{"assets",packed}};
    auto bytes=QJsonDocument(root).toJson(QJsonDocument::Compact);if(bytes.size()>fileLimit){error="El preset supera 96 MiB.";return {};}return bytes;
}
bool MapperPresets::validate(const QByteArray &bytes,QJsonObject &document,QString &error) {
    error.clear();if(bytes.isEmpty()||bytes.size()>fileLimit)return fail(error,"Tamaño de preset no válido.");QJsonParseError parse;
    auto json=QJsonDocument::fromJson(bytes,&parse);if(parse.error!=QJsonParseError::NoError||!json.isObject())return fail(error,"El archivo no contiene JSON válido.");
    auto root=json.object();if(root.value("format")!="obs-screen-mapper-preset"||!number(root,"version",1,1,true,true))return fail(error,"Formato o versión de preset no admitidos.");
    if(!text(root,"name",128,true)||root.value("name").toString().trimmed().isEmpty()||!root.value("settings").isObject()||!root.value("assets").isArray())return fail(error,"Estructura de preset no válida.");
    auto config=root.value("settings").toObject();if(!validateSettings(config,error))return false;
    // Imported data never supplies a source binding or runtime/UI implementation fields.
    const QSet<QString> allowed{"mapping_schema","canvas_width","canvas_height","distribution","automatic_layout","selected_zone","zones","editor"};
    for(auto it=config.begin();it!=config.end();){if(!allowed.contains(it.key()))it=config.erase(it);else ++it;}
    QMap<QString,QByteArray> assets;qint64 total=0;
    for(auto value:root.value("assets").toArray()) {
        if(!value.isObject())return fail(error,"Recurso de preset no válido.");auto asset=value.toObject();auto id=asset.value("id").toString();
        if(!assetId(id)||assets.contains(id)||!asset.value("data").isString()||!asset.value("sha256").isString())return fail(error,"Identificador de imagen no válido o duplicado.");
        const auto decoded=QByteArray::fromBase64Encoding(asset.value("data").toString().toLatin1(),QByteArray::AbortOnBase64DecodingErrors);
        if(decoded.decodingStatus!=QByteArray::Base64DecodingStatus::Ok)return fail(error,"Imagen codificada incorrectamente.");
        const auto data=decoded.decoded;total+=data.size();if(total>totalAssetLimit)return fail(error,"Las imágenes superan el presupuesto de 64 MiB.");
        if(hash(data)!=asset.value("sha256").toString()||!id.startsWith(hash(data)+"."))return fail(error,"La integridad de una imagen no coincide.");
        if(!imageData(data,QFileInfo(id).suffix(),error))return false;assets[id]=data;
    }
    QSet<QString> used;
    if(!references(config,[&](QString &path,bool mask){if(!path.startsWith("asset:")||!assets.contains(path.mid(6)))return fail(error,"Falta una imagen incluida o su referencia no es válida.");used.insert(path.mid(6));const auto extension=QFileInfo(path.mid(6)).suffix();if(mask&&extension!="png"&&extension!="svg")return fail(error,"Una máscara usa un formato no admitido.");if(!mask&&extension=="svg")return fail(error,"El fondo de referencia debe ser PNG, JPG o BMP.");return true;}))return false;
    if(used.size()!=assets.size())return fail(error,"El preset contiene imágenes sin referencias.");
    root["settings"]=config;document=root;return true;
}
bool MapperPresets::materialize(const QByteArray &bytes,QJsonObject &settings,QString &name,QString &error) const {
    QJsonObject document;if(!validate(bytes,document,error))return false;
    const QString directory=QDir(root).filePath("assets");if(root.isEmpty()||!QDir().mkpath(directory))return fail(error,"No se pudo preparar la carpeta de imágenes del plugin.");
    auto config=document.value("settings").toObject();
    for(auto value:document.value("assets").toArray()){
        const auto asset=value.toObject();const auto id=asset.value("id").toString();const auto data=QByteArray::fromBase64(asset.value("data").toString().toLatin1());const auto path=QDir(directory).filePath(id);
        QFile current(path);bool unchanged=current.open(QIODevice::ReadOnly)&&current.size()<=assetLimit&&hash(current.readAll())==asset.value("sha256").toString();current.close();
        if(!unchanged&&!writeFile(path,data,error))return false;
    }
    if(!references(config,[&](QString &path,bool){path=QDir(directory).absoluteFilePath(path.mid(6));return true;}))return false;
    settings=config;name=document.value("name").toString();return true;
}
QVector<PresetEntry> MapperPresets::list(QString &error) const {
    error.clear();QVector<PresetEntry> result;const auto path=QDir(root).filePath("library.json");if(!QFileInfo::exists(path))return result;
    const auto bytes=readFile(path,error);if(!error.isEmpty())return result;QJsonParseError parse;const auto json=QJsonDocument::fromJson(bytes,&parse);
    if(parse.error!=QJsonParseError::NoError||!json.isObject()||json.object().value("version")!=1||!json.object().value("presets").isArray()){error="El índice de presets no es válido. Se conservan los archivos existentes.";return {};}
    QSet<QString> ids;static QRegularExpression digest("^[0-9a-f]{64}$");
    for(auto value:json.object().value("presets").toArray()) {auto item=value.toObject();auto id=item.value("id").toString();auto name=item.value("name").toString();auto revision=item.value("revision").toString();
        if(!presetId(id)||ids.contains(id)||name.isEmpty()||name.size()>128||(!revision.isEmpty()&&!digest.match(revision).hasMatch())){error="El índice de presets contiene entradas inválidas.";return {};}ids.insert(id);result.push_back({id,name,revision});}
    return result;
}
QString MapperPresets::save(const QByteArray &bytes,const QString &existingId,QString &error) const {
    QJsonObject document;if(!validate(bytes,document,error))return {};auto entries=list(error);if(!error.isEmpty())return {};
    if(root.isEmpty()||!QDir().mkpath(root)){error="No se pudo crear la carpeta de presets.";return {};}
    const QString id=existingId.isEmpty()?QUuid::createUuid().toString(QUuid::WithoutBraces):existingId;
    if(!presetId(id)){error="Identificador de preset no válido.";return {};}
    if(!existingId.isEmpty()&&std::none_of(entries.begin(),entries.end(),[&](const PresetEntry &entry){return entry.id==id;})){error="El preset seleccionado ya no existe en la biblioteca.";return {};}
    QString name=document.value("name").toString();
    if(existingId.isEmpty()) {
        const QString base=name;int suffix=2;
        while(std::any_of(entries.begin(),entries.end(),[&](const PresetEntry &entry){return entry.name.compare(name,Qt::CaseInsensitive)==0;})) {
            const auto tail=" ("+QString::number(suffix++)+")";auto trimmed=base.left(128-tail.size());if(!trimmed.isEmpty()&&trimmed.back().isHighSurrogate())trimmed.chop(1);name=trimmed+tail;
        }
    }
    document["name"]=name;
    const auto canonical=QJsonDocument(document).toJson(QJsonDocument::Compact);const auto revision=hash(canonical);
    // Immutable revision first; the atomic index replacement is the commit point.
    // A failed index commit leaves the previous revision referenced and untouched.
    const auto file=QDir(root).filePath(id+"."+revision+".mapper.json");if(!writeFile(file,canonical,error))return {};
    bool found=false;for(auto &entry:entries)if(entry.id==id){entry.name=name;entry.revision=revision;found=true;}
    if(!found)entries.push_back({id,name,revision});
    QJsonArray index;for(const auto &entry:entries)index.append(QJsonObject{{"id",entry.id},{"name",entry.name},{"revision",entry.revision}});
    if(!writeFile(QDir(root).filePath("library.json"),QJsonDocument(QJsonObject{{"version",1},{"presets",index}}).toJson(),error))return {};
    return id;
}
QByteArray MapperPresets::load(const QString &id,QString &error) const {
    if(!presetId(id)){error="Identificador de preset no válido.";return {};}const auto entries=list(error);if(!error.isEmpty())return {};
    const auto found=std::find_if(entries.begin(),entries.end(),[&](const PresetEntry &entry){return entry.id==id;});if(found==entries.end()){error="El preset no está en el índice de la biblioteca.";return {};}
    const auto bytes=readFile(QDir(root).filePath(id+(found->revision.isEmpty()?QString():"."+found->revision)+".mapper.json"),error);if(!error.isEmpty())return {};
    if(!found->revision.isEmpty()&&hash(bytes)!=found->revision){error="La revisión del preset no coincide con su integridad guardada.";return {};}
    QJsonObject document;if(!validate(bytes,document,error))return {};return bytes;
}
