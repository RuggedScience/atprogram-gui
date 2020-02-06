#ifndef ATPACKMANAGER_H
#define ATPACKMANAGER_H

#include <QString>
#include <QFile>
#include <QList>
#include <QPair>

class AtPackManager
{
public:
    AtPackManager(const QString &path = QString());

    bool isNull() const;
    bool isValid() const;

    void setPath(const QString &path);
    QString path() const;

    QStringList getAllTargets() const;

    struct FuseInfo
    {
        QString name;
        QString description;
        QStringList bitFields;

        FuseInfo(QString name = QString(), QString description = QString()) :
            name(name),
            description(description)
        {}
    };

    QStringList getMemories(const QString &target) const;
    QList<FuseInfo> getFuseInfo(const QString &target) const;
    QStringList getInterfaces(const QString &target) const;

private:
    bool m_valid{false};
    QString m_path;

    QString getTargetFile(const QString &target) const;
    QString getFuseModuleName(const QString &targetFile) const;
};

#endif // ATPACKMANAGER_H
