// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QDialog>
#include <QTimer>
#include <QListWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QFormLayout>
#include <map>
#include "preview.hpp"
#include "presets.hpp"

class MapperEditor:public QDialog {
public:
    explicit MapperEditor(obs_source_t *target,QWidget *parent=nullptr,QString presetDirectory={});
    ~MapperEditor() override;
    mapping::Layout workingLayout() const { return layout; }
    obs_source_t *previewSource() const { return draft; }
    void select(size_t index);
    void zoneAction(const std::string &operation);
    void undoEdit();
    void redoEdit();
    void applyEdit();
    void acceptEdit();
    QString savePresetAs(const QString &name,QString &error);
    bool savePreset(QString &error);
    bool loadPreset(const QString &id,QString &error);
    bool exportPreset(const QString &path,const QString &name,QString &error);
    QString importPreset(const QString &path,QString &error);
    QString currentPreset() const { return activePreset; }
    MapperPreview *preview=nullptr;
protected:
    void keyPressEvent(QKeyEvent *) override;
private:
    bool commitEdit();
    void build();
    void activateTool(int value,bool enable=false);
    void refresh();
    void refreshSources();
    void refreshSceneChoices();
    void updateSceneContext();
    void sync();
    void publish();
    void change(const std::function<void()> &fn,bool history=true);
    void remember();
    void beginGesture();
    void endGesture();
    std::string json();
    void restore(const std::string &snapshot);
    mapping::Zone *selected();
    bool validInput(const std::string &uuid);
    void refreshPresets();
    void restoreWorkspace(bool restorePreset=true);
    bool applyPreset(const QByteArray &bytes,const QString &id,QString &error);
    QDoubleSpinBox *number(const char *key,double min,double max,double step=1);
    QCheckBox *check(const char *key,const QString &label);
    obs_source_t *target=nullptr,*draft=nullptr;
    obs_data_t *settings=nullptr;
    mapping::Layout layout;
    size_t selectedIndex=0,vertexIndex=0,meshIndex=0,cornerIndex=0;
    std::vector<std::string> undo,redo;
    std::map<std::string,QDoubleSpinBox*> numbers;
    std::map<std::string,QCheckBox*> checks;
    bool loading=false,dirty=false,gesture=false;
    std::string gestureBefore;
    QTimer timer;
    QComboBox *sources=nullptr,*distribution=nullptr,*vertices=nullptr,*meshPoints=nullptr,*maskKind=nullptr,*cornerList=nullptr;
    QListWidget *zones=nullptr;
    QLineEdit *zoneName=nullptr,*maskPath=nullptr;
    QSpinBox *columns=nullptr,*rows=nullptr,*canvasWidth=nullptr,*canvasHeight=nullptr;
    QLabel *status=nullptr,*zoomLabel=nullptr;
    QPushButton *undoButton=nullptr,*redoButton=nullptr;
    QTabWidget *tabs=nullptr;
    int tool=0;
    int shapeTool=1;
    QPushButton *sceneButton=nullptr,*refreshSceneButton=nullptr;
    QComboBox *scenePicker=nullptr;
    MapperPreview::ViewState isolatedView{1,{}};
    bool refreshingScene=false;
    QFormLayout *geometryForm=nullptr,*maskForm=nullptr,*polygonNumbersForm=nullptr,*meshNumbersForm=nullptr;
    QHBoxLayout *vertexActionRow=nullptr;
    std::unique_ptr<MapperPresets> presetStore;
    QComboBox *presetList=nullptr;
    QVector<PresetEntry> presetEntries;
    QString activePreset,workspaceReference,workspaceWarning,presetWarning;
};

