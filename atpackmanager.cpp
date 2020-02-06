#include "atpackmanager.h"
#include "tinyxml2.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStringList>
#include <QXmlStreamReader>
#include <QPair>

#include <QDebug>

AtPackManager::AtPackManager(const QString &path)
{
    setPath(path);
}

bool AtPackManager::isNull() const
{
    return m_path.isNull();
}

bool AtPackManager::isValid() const
{
    return m_valid;
}

void AtPackManager::setPath(const QString &path)
{
    m_path = QString(path);

    const QFileInfo pathInfo(path);
    if (pathInfo.exists() && pathInfo.isDir() && pathInfo.isReadable())
        m_valid = true;
    else
        m_valid = false;
}

QString AtPackManager::path() const
{
    return m_path;
}

QStringList AtPackManager::getAllTargets() const
{
    QStringList targets;
    if (m_path.isNull()) return targets;

    // Each "packgae" added to Atmel Pack Manager has a "package.content" file which contains all the supported MCUs.
    // Parse all of these files and add the MCUs to the "Target" combo box.
    QDirIterator it(m_path, QStringList() << "package.content", QDir::NoFilter, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        // QXmlStreamReader didn't like the ASCII encoding used in the package.content files
        using namespace tinyxml2;
        XMLDocument doc;
        std::string fileName = it.next().toStdString();
        if (doc.LoadFile(fileName.c_str()) == XML_SUCCESS)
        {
            XMLElement *e = nullptr;
            if ((e = doc.FirstChildElement("package")))
            {
                if ((e = e->FirstChildElement("content")))
                {
                    for (e = e->FirstChildElement("resources"); e; e = e->NextSiblingElement("resources"))
                    {
                        QString attr(e->Attribute("target"));
                        if (!attr.isEmpty())
                            targets.append(attr);
                    }
                }
            }
        }
    }

    return targets;
}

QStringList AtPackManager::getMemories(const QString &target) const
{
    QStringList memories;
    QString targetFile = getTargetFile(target);

    using namespace tinyxml2;
    XMLDocument doc;
    std::string fileName = targetFile.toStdString();
    if (doc.LoadFile(fileName.c_str()) == XML_SUCCESS)
    {
        XMLElement *e = nullptr;
        if ((e = doc.FirstChildElement("avr-tools-device-file")))
        {
            if ((e = e->FirstChildElement("devices")))
            {
                if ((e = e->FirstChildElement("device")))
                {
                    QString nameAttr(e->Attribute("name"));
                    if (nameAttr == target)
                    {
                        if ((e = e->FirstChildElement("address-spaces")))
                        {
                            XMLElement *as = e->FirstChildElement("address-space");
                            for (; as; as = as->NextSiblingElement("address-space"))
                            {
                                memories.append(QString(as->Attribute("name")));
                            }
                        }
                    }
                }
            }
        }
    }

    return memories;
}

QList<AtPackManager::FuseInfo> AtPackManager::getFuseInfo(const QString &target) const
{
    QList<FuseInfo> fuseInfoList;
    QString targetFile = getTargetFile(target);
    QString moduleName = getFuseModuleName(targetFile);

    using namespace tinyxml2;
    XMLDocument doc;
    std::string fileName = targetFile.toStdString();
    if (doc.LoadFile(fileName.c_str()) == XML_SUCCESS)
    {
        XMLElement *e = nullptr;
        if ((e = doc.FirstChildElement("avr-tools-device-file")))
        {
            if ((e = e->FirstChildElement("modules")))
            {
                for (e = e->FirstChildElement("module"); e; e = e->NextSiblingElement("module"))
                {
                    XMLElement *rg = e->FirstChildElement("register-group");
                    for (; rg; rg = rg->NextSiblingElement("register-group"))
                    {
                        QString nameAttr(rg->Attribute("name"));
                        if (nameAttr == moduleName)
                        {
                            XMLElement *r = rg->FirstChildElement("register");
                            for (; r; r = r->NextSiblingElement("register"))
                            {
                                FuseInfo info;
                                info.name = QString(r->Attribute("name"));
                                info.description = QString(r->Attribute("caption"));
                                fuseInfoList.append(info);
                            }
                            return fuseInfoList;
                        }
                    }
                }
            }
        }
    }

    return fuseInfoList;
}

QStringList AtPackManager::getInterfaces(const QString &target) const
{
    QStringList interfaces;
    QString targetFile = getTargetFile(target);

    using namespace tinyxml2;
    XMLDocument doc;
    std::string fileName = targetFile.toStdString();
    if (doc.LoadFile(fileName.c_str()) == XML_SUCCESS)
    {
        XMLElement *e = nullptr;
        if ((e = doc.FirstChildElement("avr-tools-device-file")))
        {
            if ((e = e->FirstChildElement("devices")))
            {
                if ((e = e->FirstChildElement("device")))
                {
                    if ((e = e->FirstChildElement("interfaces")))
                    {
                        XMLElement *i = e->FirstChildElement("interface");
                        for (; i; i = i->NextSiblingElement("interface"))
                        {
                            interfaces.append(i->Attribute("name"));
                        }
                    }
                }
            }
        }
    }

    return interfaces;
}

QString AtPackManager::getTargetFile(const QString &target) const
{
    QString filename = QString("%1.atdf").arg(target);
    QDirIterator it(m_path, QStringList() << filename, QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext()) return it.next();

    return QString();
}

QString AtPackManager::getFuseModuleName(const QString &targetFile) const
{
    using namespace tinyxml2;
    XMLDocument doc;
    std::string fileName = targetFile.toStdString();
    if (doc.LoadFile(fileName.c_str()) == XML_SUCCESS)
    {
        XMLElement *e = nullptr;
        if ((e = doc.FirstChildElement("avr-tools-device-file")))
        {
            if ((e = e->FirstChildElement("devices")))
            {
                if ((e = e->FirstChildElement("device")))
                {
                    if ((e = e->FirstChildElement("peripherals")))
                    {
                        XMLElement *m = e->FirstChildElement("module");
                        for (; m; m = m->NextSiblingElement("module"))
                        {
                            XMLElement *i = m->FirstChildElement("instance");
                            for (; i; i = i->NextSiblingElement("instance"))
                            {
                                QString attr(i->Attribute("name"));
                                if (attr == "FUSE")
                                {
                                    if ((i = i->FirstChildElement("register-group")))
                                        return QString(i->Attribute("name-in-module"));
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return QString();
}
