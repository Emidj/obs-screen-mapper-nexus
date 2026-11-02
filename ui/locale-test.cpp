// SPDX-License-Identifier: GPL-2.0-or-later
#include "editor.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QDir>
#include <QPainter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void check(bool ok,const char *message){std::printf("%s: %s\n",ok?"PASS":"FAIL",message);std::fflush(stdout);if(!ok)std::exit(1);++checks;}
static void pump(){QElapsedTimer timer;timer.start();while(timer.elapsed()<180){QApplication::processEvents();QThread::msleep(2);}}
int main(int argc,char **argv){
    QApplication app(argc,argv);check(argc==5,"locale test paths supplied");
    check(obs_startup("es-ES",nullptr,nullptr),"isolated libobs startup");
    const std::string obs=argv[3],graphics=obs+"/bin/64bit/libobs-d3d11.dll",data=obs+"/data/libobs/";
#pragma warning(push)
#pragma warning(disable:4996)
    obs_add_data_path(data.c_str());
#pragma warning(pop)
    obs_video_info video{};video.graphics_module=graphics.c_str();video.fps_num=60;video.fps_den=1;video.base_width=1920;video.base_height=1080;video.output_width=1920;video.output_height=1080;video.output_format=VIDEO_FORMAT_RGBA;video.colorspace=VIDEO_CS_709;video.range=VIDEO_RANGE_FULL;video.scale_type=OBS_SCALE_BILINEAR;
    check(obs_reset_video(&video)==OBS_VIDEO_SUCCESS,"D3D11 initialized");obs_module_t *module=nullptr;
    check(obs_open_module(&module,argv[1],argv[2])==MODULE_SUCCESS&&obs_init_module(module),"localized core loaded");
    const QString output=QString::fromLocal8Bit(argv[4]);QDir().mkpath(output);QTemporaryDir library(output+"/library-XXXXXX");check(library.isValid(),"isolated preset library");
    const char *locales[]={"es-MX","en-GB","de-AT","fr-CA","it-CH","pt-BR"};
    for(const auto *locale:locales){
        std::printf("LOCALE: %s\n",locale);obs_set_locale(locale);mapper_i18n::set_locale(locale);
        auto *source=obs_source_create_private("obs_screen_mapper","Locale fixture",nullptr);
        check(source!=nullptr,"source created in selected locale");
        auto *properties=obs_source_properties(source);
        check(std::string(obs_property_description(obs_properties_get(properties,"input_uuid")))==mapper_i18n::text("Fuente OBS existente"),"core properties follow OBS locale");
        check(std::string(obs_property_description(obs_properties_get(properties,"open_editor")))==mapper_i18n::text("Abrir Mapper Editor…"),"core editor button follows OBS locale");
        check(std::string(obs_property_description(obs_properties_get(properties,"author_credit")))=="a plugin from <a href=\"https://discord.gg/dhcqZPDYh\">DjEmiMas</a>","exact English author credit preserved");
        obs_properties_destroy(properties);
        auto *settings=obs_source_get_settings(source);const auto original=std::string(obs_data_get_json(settings));
        check(mapping::read(settings).zones[0].name==mapper_i18n::text("Zona 1"),"new default zone name localized");obs_data_release(settings);
        {
            MapperEditor editor(source,nullptr,library.path());editor.show();pump();
            for(const auto &item:{std::pair<const char *,const char *>{"applyEditor","Aplicar"},{"cancelEditor","Cancelar"},{"addZone","＋ Añadir"},{"tool2","Editar malla"},{"tool3","Mover lienzo"},{"savePreset","Guardar"},{"loadPreset","Cargar"}}){
                const auto *button=editor.findChild<QPushButton*>(item.first);check(button&&(std::string(item.first)=="tool3"?button->toolTip()==QString::fromUtf8(mapper_i18n::text("Espacio + arrastrar para mover el lienzo")):button->text()==QString::fromUtf8(mapper_i18n::text(item.second))),"editor action translated");
            }
            const auto *tabs=editor.findChild<QTabWidget*>();check(tabs&&tabs->tabText(1)==QString::fromUtf8(mapper_i18n::text("Forma"))&&tabs->tabText(2)==QString::fromUtf8(mapper_i18n::text("Máscara")),"contextual tabs translated");
            for(const char *label:{"Centro X de máscara","Centro Y de máscara","Ancho de máscara","Alto de máscara","Radio de círculo / elipse"}){bool found=false;for(auto *widget:editor.findChildren<QLabel*>())found|=widget->text()==QString::fromUtf8(mapper_i18n::text(label));check(found,"mask shape label translated");}
            check(editor.findChild<QPushButton*>("resetMaskShape")->text()==QString::fromUtf8(mapper_i18n::text("Restablecer forma")),"mask shape reset translated");
            check(tabs->tabText(0)==QString::fromUtf8(mapper_i18n::text("Posición")),"transform tab translated");
            check(editor.findChild<QPushButton*>("tool4")->text()==QString::fromUtf8(mapper_i18n::text("Cuatro esquinas")),"four-corner tool translated");
            check(editor.findChild<QPushButton*>("acceptEditor")->text()==QString::fromUtf8(mapper_i18n::text("Aceptar")),"accept button translated");
            check(editor.findChild<QPushButton*>("showSceneContext")->text()==QString::fromUtf8(mapper_i18n::text("Ver escena completa")),"scene context action translated");
            check(editor.findChild<QPushButton*>("fitMeshToMask")->text()==QString::fromUtf8(mapper_i18n::text("Adaptar malla al recorte")),"mesh mask view action translated");
            const auto *sources=editor.findChild<QComboBox*>("inputSource");check(sources&&sources->itemText(0)==QString::fromUtf8(mapper_i18n::text("Ninguna · transparente")),"empty input label translated");
            editor.findChild<QPushButton*>("addZone")->click();pump();
            check(editor.workingLayout().zones.size()==2&&editor.workingLayout().zones[1].name.find(mapper_i18n::text("Zona "))==0,"added zone localized");
            editor.findChild<QLineEdit*>("zoneName")->setText(QString::fromUtf8("Mi zona ñ — Benutzer"));
            QMetaObject::invokeMethod(editor.findChild<QLineEdit*>("zoneName"),"editingFinished");pump();
            QString error;const auto path=output+"/"+locale+".mapper.json";
            const bool exported=editor.exportPreset(path,QString::fromUtf8("Mi preset ñ — Benutzer"),error);if(!exported)std::fprintf(stderr,"EXPORT ERROR: %s\n",error.toUtf8().constData());check(exported,"portable preset exports in each language");
            const auto bytes=MapperPresets::readFile(path,error);QJsonObject document;
            check(MapperPresets::validate(bytes,document,error),"portable schema stays language independent");
            check(document["name"].toString()==QString::fromUtf8("Mi preset ñ — Benutzer")&&bytes.contains("Mi zona"),"user zone and preset names are preserved");
            check(document["settings"].toObject()["distribution"]=="split","serialized mode identifier remains stable");
            QJsonObject invalid;check(!MapperPresets::validate("{}",invalid,error)&&error==QString::fromUtf8(mapper_i18n::text("Formato o versión de preset no admitidos.")),"preset validation error translated");
            mapping::Mesh mesh;mesh.columns=1;mapping::validate_mesh(mesh);check(mesh.error==mapper_i18n::text("Dimensiones o puntos de malla no válidos."),"geometry validation error translated");
            auto screenshot=editor.grab();const auto frame=editor.preview->renderFrame();check(!frame.isNull(),"native preview renders in each locale");
            {QPainter painter(&screenshot);painter.drawImage(QRect(editor.preview->mapTo(&editor,QPoint()),editor.preview->size()),frame);}
            check(screenshot.save(output+"/editor-"+locale+".png"),"internal UI render artifact saved");
            editor.reject();
        }
        settings=obs_source_get_settings(source);check(original==obs_data_get_json(settings),"Cancel preserves original settings across languages");obs_data_release(settings);obs_source_release(source);
    }
    mapper_i18n::set_locale("EN_us");check(std::string(mapper_i18n::text("Guardar"))=="Save","case and separator normalization");
    mapper_i18n::set_locale(nullptr);check(std::string(mapper_i18n::text("Guardar"))=="Save","null locale falls back to English");
    check(std::string(mapper_i18n::text("Unmapped user text"))=="Unmapped user text","unknown text preserved");
    obs_shutdown();std::printf("PASS: %u locale checks complete\n",checks);return 0;
}




