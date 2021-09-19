#include "localmodpath.h"

#include <QDir>
#include <QFuture>
#include <QtConcurrent/QtConcurrent>

#include "cpp-semver.hpp"
#include "localmodfile.h"
#include "curseforge/curseforgeapi.h"
#include "modrinth/modrinthapi.h"
#include "util/tutil.hpp"
#include "config.h"

LocalModPath::LocalModPath(QObject *parent, const LocalModPathInfo &info) :
    QObject(parent),
    curseforgeAPI_(new CurseforgeAPI(this)),
    modrinthAPI_(new ModrinthAPI(this)),
    info_(info)
{
    loadMods();
}

void LocalModPath::loadMods()
{
    modFileList_.clear();
    for(const auto &fileInfo : QDir(info_.path()).entryInfoList(QDir::Files))
        if(LocalModFile::availableSuffix.contains(fileInfo.suffix()))
            modFileList_ << new LocalModFile(this, fileInfo.absoluteFilePath());

    auto future = QtConcurrent::run([=]{
        for(auto &file : modFileList_)
            file->loadInfo();
    });

    auto watcher = new QFutureWatcher<void>(this);
    watcher->setFuture(future);
    connect(watcher, &QFutureWatcher<void>::finished, this, [=]{
        modMap_.clear();
        fabricModMap_.clear();
        provideList_.clear();

        //load normal mods (include duplicate)
        for(const auto &file : qAsConst(modFileList_)){
            if(file->type() != LocalModFile::Normal) continue;
            if(file->loaderType() != info_.loaderType()) continue;
            auto id = file->commonInfo()->id();
            //duplicate
            if(modMap_.contains(id)){
                modMap_[id]->addDuplicateFile(file);
                continue;
            }
            else {
                //new mod
                auto mod = new LocalMod(this, file);
                //connect update signal
                connect(mod, &LocalMod::updateFinished, this, [=](bool){
                    emit updatesReady();
                });
                modMap_[id] = mod;
            }
        }

        //load old mods
        for(const auto &file : qAsConst(modFileList_)){
            if(file->type() != LocalModFile::Old) continue;
            if(file->loaderType() != info_.loaderType()) continue;
            auto id = file->commonInfo()->id();
            //old
            if(modMap_.contains(id)){
                modMap_[id]->addOldFile(file);
                continue;
            }
            else
                ;//TODO: deal with homeless old mods
        }

        readFromFile();
        emit modListUpdated();

        connect(this, &LocalModPath::websitesReady, [=]{
            checkModUpdates(false);
            disconnect(SIGNAL(websitesReady()));
        });

        searchOnWebsites();
    });
}

void LocalModPath::checkFabric()
{
    //fabric
    if(info_.loaderType() == ModLoaderType::Fabric){
        //depends
        for(const auto &[fabricMod, modid, version, missingMod] : checkFabricDepends()){
            QString str;
            if(missingMod.has_value())
                str += "Missing:\n" + modid + " " + version;
            else
                str += "MisMatch:\n" + modid + " " + version;

//            auto localMod = modMap_.value(fabricMod.mainId());
//            localMod->addDepend()
        }
    }
}

std::tuple<LocalModPath::FindResultType, std::optional<FabricModInfo> > LocalModPath::findFabricMod(const QString &modid, const QString &range_str) const
{
    //check contains
    if(!fabricModMap_.contains(modid) && !provideList_.contains(modid)) {
        //environment
        if(modid == "minecraft" || modid == "java" || modid == "fabricloader")
            return { Environmant, std::nullopt };
        else
            return { Missing, std::nullopt };
    }

    //current mod version
    auto modInfo = fabricModMap_.value(modid);
    auto version_str = modInfo.version();
    //remove build etc
    version_str = version_str.left(version_str.indexOf('+'));
    version_str = version_str.left(version_str.indexOf('-'));
    if(!semver::valid(version_str.toStdString())){
        return { VersionSemverError, {modInfo} };
    }
    if(!semver::valid(range_str.toStdString())){
        return { RangeSemverError, std::nullopt };
    }
    if (range_str == "*" || semver::satisfies(version_str.toStdString(), range_str.toStdString())) {
        return { Match, {modInfo} };
    } else {
        return { Mismatch, {modInfo} };
    }
}

void LocalModPath::writeToFile()
{
    QJsonObject object;

    object.insert("latestUpdateCheck", latestUpdateCheck_.toString(Qt::DateFormat::ISODate));

    //mods
    QJsonObject modsObject;
    for(auto mod : qAsConst(modMap_))
        modsObject.insert(mod->commonInfo()->id(), mod->toJsonObject());
    object.insert("mods", modsObject);

    QJsonDocument doc(object);
    QDir dir(info_.path());
    QFile file(dir.absoluteFilePath(kFileName));
    if(!file.open(QIODevice::WriteOnly)) return;
    file.write(doc.toJson());
    file.close();
}

void LocalModPath::readFromFile()
{
    QDir dir(info_.path());
    QFile file(dir.absoluteFilePath(kFileName));
    if(!file.open(QIODevice::ReadOnly)) return;
    auto bytes = file.readAll();
    file.close();

    //parse json
    QJsonParseError error;
    QJsonDocument jsonDocument = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError) {
        qDebug("%s", error.errorString().toUtf8().constData());
        return;
    }
    auto result = jsonDocument.toVariant();

    latestUpdateCheck_ = value(result, "latestUpdateCheck").toDateTime();

    //mods
    auto modMap = value(result, "mods").toMap();
    for(auto it = modMap.cbegin(); it != modMap.cend(); it++)
        if(modMap_.contains(it.key()))
            modMap_[it.key()]->restore(*it);
}

QList<std::tuple<FabricModInfo, QString, QString, std::optional<FabricModInfo>>> LocalModPath::checkFabricDepends() const
{
    QList<std::tuple<FabricModInfo, QString, QString, std::optional<FabricModInfo>>> list;
    for(const auto &fabricMod : qAsConst(fabricModMap_)){
        //check depends
        if(fabricMod.isEmbedded()) continue;
        for(auto it = fabricMod.depends().cbegin(); it != fabricMod.depends().cend(); it++){
            auto [result, info] = findFabricMod(it.key(), it.value());
            auto modid = it.key();
            auto range_str = it.value();
            switch (result) {
            case Environmant:
                //nothing to do
                break;
            case Missing:
                list.append({ fabricMod, modid, "", std::nullopt});
                qDebug() << fabricMod.name() << fabricMod.id() << "depends" << modid << "which is missing";
                break;
            case Mismatch:
                list.append({ fabricMod, modid, range_str, info});
                qDebug() << fabricMod.name() << fabricMod.id() << "depends" << modid << "which is mismatch";
                break;
            case Match:
                //nothing to do
                break;
            case RangeSemverError:
                qDebug() << "range does not respect semver:" << modid << range_str << "provided by" << fabricMod.name();
                //nothing to do
                break;
            case VersionSemverError:
                qDebug() << "version does not respect semver:" << modid << info->version() << "provided by" << info->name();
                //nothing to do
                break;
            }
        }
    }
    return list;
}

QList<std::tuple<FabricModInfo, QString, QString, FabricModInfo> > LocalModPath::checkFabricConflicts() const
{
    QList<std::tuple<FabricModInfo, QString, QString, FabricModInfo>> list;
    for(const auto &fabricMod : qAsConst(fabricModMap_)){
        //check depends
        if(fabricMod.isEmbedded()) continue;
        for(auto it = fabricMod.conflicts().cbegin(); it != fabricMod.conflicts().cend(); it++){
            auto [result, info] = findFabricMod(it.key(), it.value());
            auto modid = it.key();
            auto range_str = it.value();
            switch (result) {
            case Environmant:
                //nothing to do
                break;
            case Missing:
                //nothing to do
            case Mismatch:
                //nothing to do
                break;
            case Match:
                list.append({ fabricMod, modid, range_str, *info});
                qDebug() << fabricMod.name() << fabricMod.id() << "conflicts" << modid << "which is present";
                break;
            case RangeSemverError:
                //nothing to do
                break;
            case VersionSemverError:
                //nothing to do
                break;
            }
        }
    }
    return list;
}

QList<std::tuple<FabricModInfo, QString, QString, FabricModInfo> > LocalModPath::checkFabricBreaks() const
{
    QList<std::tuple<FabricModInfo, QString, QString, FabricModInfo>> list;
    for(const auto &fabricMod : qAsConst(fabricModMap_)){
        //check depends
        if(fabricMod.isEmbedded()) continue;
        for(auto it = fabricMod.breaks().cbegin(); it != fabricMod.breaks().cend(); it++){
            auto [result, info] = findFabricMod(it.key(), it.value());
            auto modid = it.key();
            auto range_str = it.value();
            switch (result) {
            case Environmant:
                //nothing to do
                break;
            case Missing:
                //nothing to do
            case Mismatch:
                //nothing to do
                break;
            case Match:
                list.append({ fabricMod, modid, range_str, *info});
                qDebug() << fabricMod.name() << fabricMod.id() << "breaks" << modid << "which is present";
                break;
            case RangeSemverError:
                //nothing to do
                break;
            case VersionSemverError:
                //nothing to do
                break;
            }
        }
    }
    return list;
}

LocalMod *LocalModPath::findLocalMod(const QString &id)
{
    return modMap_.contains(id)? modMap_.value(id) : nullptr;
}

void LocalModPath::searchOnWebsites()
{
    if(modMap_.isEmpty()) return;
    emit checkWebsitesStarted();
    auto count = std::make_shared<int>(0);
    bool isAllCached = true;
    for(const auto &mod : qAsConst(modMap_)){
        //has cache
        if(!mod->curseforgeMod() && !mod->modrinthMod()){
            isAllCached = false;
            (*count)++;
            connect(mod, &LocalMod::websiteReady, this, [=]{
                writeToFile();
                emit websiteCheckedCountUpdated(modMap_.size() - *count);
                if(--(*count) == 0)
                    emit websitesReady();
            });
            mod->searchOnWebsite();
        }
    }
    if(isAllCached) emit websitesReady();
}

void LocalModPath::checkModUpdates(bool force)
{
    auto interval = Config().getUpdateCheckInterval();
    if(force || interval == Config::Always ||
      (interval == Config::EveryDay && latestUpdateCheck_.daysTo(QDateTime::currentDateTime()) >= 1)){
        if(modMap_.isEmpty()) return;
        emit checkUpdatesStarted();
        auto count = std::make_shared<int>(0);
        auto updateCount = std::make_shared<int>(0);
        for(const auto &mod : qAsConst(modMap_)){
            connect(mod, &LocalMod::updateReady, this, [=](bool bl){
                (*count)++;
                if(bl) (*updateCount)++;
                emit updateCheckedCountUpdated(*updateCount, *count);
                //done
                if(*count == modMap_.size()){
                    latestUpdateCheck_ = QDateTime::currentDateTime();
                    writeToFile();
                    emit updatesReady();
                }
            });
            mod->checkUpdates(info_.gameVersion(), info_.loaderType());
        }
    } else if(interval != Config::Never)
        emit updatesReady();
}

void LocalModPath::updateMods(QList<QPair<LocalMod *, LocalMod::ModWebsiteType> > modUpdateList)
{
    emit updatesStarted();
    auto count = std::make_shared<int>(0);
    auto successCount = std::make_shared<int>(0);
    auto failCount = std::make_shared<int>(0);
    auto bytesReceivedList = std::make_shared<QVector<qint64>>(modUpdateList.size());

    qint64 totalSize = 0;
    for(const auto &pair : modUpdateList)
        totalSize += pair.first->updateSize(pair.second);

    for(int i = 0; i < modUpdateList.size(); i++){
        auto mod = modUpdateList.at(i).first;
        auto updateSource = modUpdateList.at(i).second;

        mod->update(updateSource);

        connect(mod, &LocalMod::updateProgress, this, [=](qint64 bytesReceived, qint64){
            (*bytesReceivedList)[i] = bytesReceived;
            auto sumReceived = std::accumulate(bytesReceivedList->cbegin(), bytesReceivedList->cend(), 0);
            emit updatesProgress(sumReceived, totalSize);
        });
        connect(mod, &LocalMod::updateFinished, this, [=](bool success){
            (*count)++;
            if(success)
                (*successCount)++;
            else
                (*failCount)++;
            emit updatesDoneCountUpdated(*count, modUpdateList.size());
            if(*count == modUpdateList.size())
                emit updatesDone(*successCount, *failCount);
        });
    }
}

const LocalModPathInfo &LocalModPath::info() const
{
    return info_;
}

void LocalModPath::setInfo(const LocalModPathInfo &newInfo)
{
    if(info_ == newInfo) return;

    info_ = newInfo;
    emit infoUpdated();

    //path, game version or loader type change will trigger mod reload
    if(info_.path() != newInfo.path() || info_.gameVersion() != newInfo.gameVersion() || info_.loaderType() != newInfo.loaderType())
        loadMods();
}

CurseforgeAPI *LocalModPath::curseforgeAPI() const
{
    return curseforgeAPI_;
}

ModrinthAPI *LocalModPath::modrinthAPI() const
{
    return modrinthAPI_;
}

int LocalModPath::updatableCount() const
{
    auto count = std::count_if(modMap_.cbegin(), modMap_.cend(), [=](const auto &mod){
        return !mod->updateTypes().isEmpty();
    });
    return count;
}

const QMap<QString, LocalMod *> &LocalModPath::modMap() const
{
    return modMap_;
}

void LocalModPath::deleteAllOld() const
{
    for(auto mod : modMap_)
        mod->deleteAllOld();
    //TODO: inform finished
}
