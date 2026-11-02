// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QVector>
#include <QByteArray>
#include <obs.h>

struct PresetEntry { QString id,name,revision; };
class MapperPresets {
public:
    explicit MapperPresets(QString directory):root(std::move(directory)) {}
    static QByteArray encode(obs_data_t *settings,const QString &name,QString &error);
    static bool validate(const QByteArray &bytes,QJsonObject &document,QString &error);
    static bool writeFile(const QString &path,const QByteArray &bytes,QString &error);
    static QByteArray readFile(const QString &path,QString &error);
    bool materialize(const QByteArray &bytes,QJsonObject &settings,QString &name,QString &error) const;
    QVector<PresetEntry> list(QString &error) const;
    QString save(const QByteArray &bytes,const QString &existingId,QString &error) const;
    QByteArray load(const QString &id,QString &error) const;
    QString directory() const { return root; }
private:
    QString root;
};
