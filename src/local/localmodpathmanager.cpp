#include "localmodpathmanager.h"

#include "localmodpath.h"
#include "config.hpp"

LocalModPathManager::LocalModPathManager(QObject *parent) : QObject(parent)
{}

LocalModPathManager *LocalModPathManager::manager()
{
    static LocalModPathManager manager;
    return &manager;
}

const QList<LocalModPath *> &LocalModPathManager::pathList()
{
    return manager()->pathList_;
}

void LocalModPathManager::setPathList(const QList<LocalModPath *> &newPathList)
{
    for(const auto &path : newPathList){
        path->setParent(manager());
        connect(path, &LocalModPath::infoUpdated, manager(), &LocalModPathManager::updateList);
    }
    manager()->pathList_ = newPathList;
    manager()->updateList();
}

void LocalModPathManager::addPath(LocalModPath *path)
{
    path->setParent(manager());
    connect(path, &LocalModPath::infoUpdated, manager(), &LocalModPathManager::updateList);
    manager()->pathList_ << path;
    manager()->updateList();
}

void LocalModPathManager::addPaths(const QStringList &paths)
{
    for(auto &&pathStr : paths){
        auto path = new LocalModPath(LocalModPathInfo::deduceFromPath(pathStr), true);
        manager()->pathList_ << path;
        connect(path, &LocalModPath::infoUpdated, manager(), &LocalModPathManager::updateList);
    }
}

void LocalModPathManager::removePath(LocalModPath *path)
{
    path->deleteLater();
    manager()->pathList_.removeOne(path);
    manager()->updateList();
}

void LocalModPathManager::removePathAt(int i)
{
    manager()->pathList_.takeAt(i)->deleteLater();
    manager()->updateList();
}

void LocalModPathManager::insertPath(int i, LocalModPath *path)
{
    path->setParent(manager());
    manager()->pathList_.insert(i, path);
    manager()->updateList();
}

void LocalModPathManager::load()
{
    Config config;
    for(auto &&pathInfo : config.getLocalPathList()){
        auto path = new LocalModPath(LocalModPathInfo::fromVariant(pathInfo));
        manager()->pathList_ << path;
        connect(path, &LocalModPath::infoUpdated, manager(), &LocalModPathManager::updateList);
    }
}

void LocalModPathManager::saveToConfig()
{
    Config config;
    QList<QVariant> list;
    for(const auto &path : qAsConst(manager()->pathList_))
        list << path->info().toVariant();
    config.setLocalPathList(list);
}

void LocalModPathManager::updateList()
{
    emit pathListUpdated();
    saveToConfig();
}
