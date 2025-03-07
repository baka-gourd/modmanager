#include "localmod.h"

#include <QDir>
#include <QJsonObject>
#include <QJsonArray>
#include <iterator>
#include <algorithm>
#include <tuple>

#include "local/localmodpath.h"
#include "local/idmapper.h"
#include "local/knownfile.h"
#include "curseforge/curseforgeapi.h"
#include "curseforge/curseforgemod.h"
#include "modrinth/modrinthapi.h"
#include "modrinth/modrinthmod.h"
#include "download/downloadmanager.h"
#include "util/tutil.hpp"
#include "config.hpp"
#include "util/mmlogger.h"

LocalMod::LocalMod(QObject *parent, LocalModFile *file) :
    QObject(parent),
    curseforgeAPI_(CurseforgeAPI::api()),
    modrinthAPI_(ModrinthAPI::api())
{
    setModFile(file);
}

LocalMod::LocalMod(LocalModPath *parent, LocalModFile *file) :
    QObject(parent),
    curseforgeAPI_(parent->curseforgeAPI()),
    modrinthAPI_(parent->modrinthAPI()),
    path_(parent),
    targetVersion_(parent->info().gameVersion()),
    targetLoaderType_(parent->info().loaderType())
{
    setModFile(file);
    connect(parent, &LocalModPath::infoUpdated, this, [=]{
        targetVersion_ = parent->info().gameVersion();
        targetLoaderType_ = parent->info().loaderType();
    });
}

LocalMod::~LocalMod()
{
    MMLogger::dtor(this) << commonInfo()->id();
}

CurseforgeMod *LocalMod::curseforgeMod() const
{
    return curseforgeMod_;
}

void LocalMod::setCurseforgeMod(CurseforgeMod *newCurseforgeMod)
{
    curseforgeMod_ = newCurseforgeMod;
}

void LocalMod::searchOnWebsite()
{
    auto count = std::make_shared<int>(2);
    auto hasWeb = std::make_shared<bool>(false);
    auto foo = [=](bool bl){
        if(bl) *hasWeb = true;
        if(--(*count) == 0) emit websiteReady(*hasWeb);
    };
    emit checkWebsiteStarted();
    connect(this, &LocalMod::searchOnCurseforgeFinished, this, foo);
    searchOnCurseforge();
    connect(this, &LocalMod::searchOnModrinthFinished, this, foo);
    searchOnModrinth();
}

void LocalMod::searchOnCurseforge()
{
    emit checkCurseforgeStarted();
    //id cache
    if(!curseforgeMod_ && IdMapper::idMap().contains(commonInfo()->id()))
        if(auto id = IdMapper::get(commonInfo()->id()).curseforgeId())
            setCurseforgeId(id, false);
    auto &&murmurhash = modFile_->murmurhash();
    //file cache
    if(!curseforgeUpdate_.currentFileInfo() && KnownFile::curseforgeFiles().keys().contains(murmurhash))
        setCurrentCurseforgeFileInfo(KnownFile::curseforgeFiles().value(murmurhash), false);
    //do i need web search?
    if(curseforgeMod_ && curseforgeUpdate_.currentFileInfo())
        emit searchOnCurseforgeFinished(true);
    else if(KnownFile::unmatchedCurseforgeFiles().contains(murmurhash)){
        emit searchOnCurseforgeFinished(false);
    } else
        curseforgeAPI_->getIdByFingerprint(murmurhash, [=](int id, auto fileInfo, const auto &fileList){
            IdMapper::addCurseforge(commonInfo()->id(), id);
            KnownFile::addCurseforge(murmurhash, fileInfo);
            CurseforgeModInfo modInfo(id);
            modInfo.setLatestFiles(fileList);
            curseforgeMod_ = new CurseforgeMod(this, modInfo);
            setCurrentCurseforgeFileInfo(fileInfo);
            emit modCacheUpdated();
            emit curseforgeReady(true);
            emit searchOnCurseforgeFinished(true);
        }, [=]{
            KnownFile::addUnmatchedCurseforge(murmurhash);
            emit searchOnCurseforgeFinished(false);
    });
}

void LocalMod::searchOnModrinth()
{
    emit checkModrinthStarted();
    //id cache
    if(!modrinthMod_ && IdMapper::idMap().contains(commonInfo()->id()))
        if(auto id = IdMapper::get(commonInfo()->id()).modrinthId(); !id.isEmpty())
            setModrinthId(id, false);
    auto &&sha1 = modFile_->sha1();
    //file cache
    if(!modrinthUpdate_.currentFileInfo() && KnownFile::modrinthFiles().keys().contains(sha1))
        setCurrentModrinthFileInfo(KnownFile::modrinthFiles().value(sha1), false);
    //do i need web search?
    if(modrinthMod_ && modrinthUpdate_.currentFileInfo())
        emit searchOnModrinthFinished(true);
    else if(KnownFile::unmatchedModrinthFiles().contains(sha1)){
        emit searchOnModrinthFinished(false);
    } else
        modrinthAPI_->getVersionFileBySha1(sha1, [=](const auto &fileInfo){
            qDebug() << sha1;
            IdMapper::addModrinth(commonInfo()->id(), fileInfo.modId());
            KnownFile::addModrinth(sha1, fileInfo);
            ModrinthModInfo modInfo(fileInfo.modId());
            modrinthMod_ = new ModrinthMod(this, modInfo);
            setCurrentModrinthFileInfo(fileInfo);
            emit modCacheUpdated();
            emit modrinthReady(true);
            emit searchOnModrinthFinished(true);
        }, [=]{
            KnownFile::addUnmatchedModrinth(sha1);
            emit searchOnModrinthFinished(false);
        });
}

void LocalMod::checkUpdates(bool force)
{
    //clear update cache
    modrinthUpdate_.reset();
    curseforgeUpdate_.reset();

    emit checkUpdatesStarted();

    Config config;
    auto count = std::make_shared<int>(0);
    auto foo = [=](bool){
        if(--(*count) == 0){
            emit updateReady(defaultUpdateType());
        }
    };
    bool noSource = true;
    if(config.getUseCurseforgeUpdate() && curseforgeUpdate_.currentFileInfo()){
        (*count)++;
        connect(this, &LocalMod::curseforgeUpdateReady, foo);
        checkCurseforgeUpdate(force);
        noSource = false;
    }
    if(config.getUseModrinthUpdate() && modrinthUpdate_.currentFileInfo()){
        (*count)++;
        connect(this, &LocalMod::modrinthUpdateReady, foo);
        checkModrinthUpdate(force);
        noSource = false;
    }
    if(noSource) emit updateReady(ModWebsiteType::None);
}

void LocalMod::checkCurseforgeUpdate(bool force)
{
    if(!curseforgeMod_){
        emit curseforgeUpdateReady(false);
        return;
    }

    emit checkCurseforgeUpdateStarted();

    //update file list
    auto updateFileList = [=]{
         bool bl = curseforgeUpdate_.findUpdate(curseforgeMod_->modInfo().allFileList(), targetVersion_, targetLoaderType_);
         emit curseforgeUpdateReady(bl);
    };

    if(force || curseforgeMod_->modInfo().allFileList().isEmpty()){
        curseforgeMod_->acquireAllFileList();
        connect(curseforgeMod_, &CurseforgeMod::allFileListReady, this, updateFileList);
    }else
        updateFileList();
}

void LocalMod::checkModrinthUpdate(bool force)
{
    if(!modrinthMod_){
        emit modrinthUpdateReady(false);
        return;
    }

    emit checkModrinthUpdateStarted();

    //update file list
    auto updateFileList = [=]{
         bool bl = modrinthUpdate_.findUpdate(modrinthMod_->modInfo().fileList(), targetVersion_, targetLoaderType_);
         emit modrinthUpdateReady(bl);
    };

    auto updateFullInfo = [=]{
        if(!modrinthMod_->modInfo().fileList().isEmpty())
            updateFileList();
        else {
            modrinthMod_->acquireFileList();
            connect(modrinthMod_, &ModrinthMod::fileListReady, this, updateFileList);
        }
    };

    //always acquire
//    if(modrinthMod_->modInfo().hasFullInfo())
//        updateFullInfo();
//    else {
    if(force || modrinthMod_->modInfo().fileList().isEmpty()){
        if(modrinthMod_->modInfo().hasFullInfo())
            updateFullInfo();
        else{
            modrinthMod_->acquireFullInfo();
            connect(modrinthMod_, &ModrinthMod::fullInfoReady, this, updateFullInfo);
        }
    }
    //    }
}

QAria2Downloader *LocalMod::downloadOldMod(DownloadFileInfo &info)
{
    //TODO: something wrong
    info.setPath(path_->info().path());
    auto downloader = DownloadManager::manager()->download(info);
    connect(downloader, &AbstractDownloader::finished, this, [=]{
        QFileInfo fileInfo(path_->info().path(), info.fileName());
        if(!LocalModFile::availableSuffix.contains(fileInfo.suffix())) return;
        auto file = new LocalModFile(this, fileInfo.absoluteFilePath());
        file->loadInfo();
        qDebug() << file->addOld();
        addOldFile(file);
        emit modFileUpdated();
    });
    return downloader;
}

LocalMod::ModWebsiteType LocalMod::defaultUpdateType() const
{
    auto list = updateTypes();
    return list.isEmpty()? ModWebsiteType::None : list.first();
}

QList<LocalMod::ModWebsiteType> LocalMod::updateTypes() const
{
    bool bl1(curseforgeUpdate_.updateFileInfo());
    bool bl2(modrinthUpdate_.updateFileInfo());

    if(bl1 && !bl2)
        return { ModWebsiteType::Curseforge };
    else if(!bl1 && bl2)
        return { ModWebsiteType::Modrinth };
    else if(bl1 && bl2){
        if(curseforgeUpdate_.fileDate() > modrinthUpdate_.fileDate())
            return { ModWebsiteType::Curseforge, ModWebsiteType::Modrinth };
        else
            return { ModWebsiteType::Modrinth, ModWebsiteType::Curseforge};
    } else
        return {};
}

QPair<QString, QString> LocalMod::updateNames(ModWebsiteType type) const
{
    if(type == Curseforge)
        return curseforgeUpdate_.updateNames();
    else if(type == Modrinth)
        return modrinthUpdate_.updateNames();
    else
        return QPair("", "");
}

QPair<QString, QString> LocalMod::updateInfos(ModWebsiteType type) const
{
    if(type == Curseforge)
        return curseforgeUpdate_.updateInfos();
    else if(type == Modrinth)
        return modrinthUpdate_.updateInfos();
    else
        return QPair("", "");
}

QAria2Downloader *LocalMod::update(ModWebsiteType type)
{
    if(type == None) return nullptr;
    emit updateStarted();

    auto path = QFileInfo(modFile_->path()).absolutePath();
    auto callback1 = [=](const auto &newInfo){
        auto newPath = QDir(path).absoluteFilePath(newInfo.fileName());

        auto file = new LocalModFile(this, newPath);
        file->loadInfo();
        //loader type mismatch
        if(!file->loaderTypes().contains(modFile_->loaderType())){
            emit updateFinished(false);
            return false;
        }

        //deal with old mod file
        auto postUpdate = Config().getPostUpdate();
        if(postUpdate == Config::Delete){
            //remove old file
            if(!modFile_->remove())
                return false;
            modFile_->deleteLater();
        } else if(postUpdate == Config::Keep){
            if(!modFile_->addOld())
                return false;
            oldFiles_ << modFile_;
        }

        //TODO: if version updated from curseforge also in modrinth, etc...
        if(type == Curseforge)
            modrinthUpdate_.reset(true);
        if(type == Modrinth)
            curseforgeUpdate_.reset(true);

        setModFile(file);
        emit modFileUpdated();

        return true;
    };
    auto callback2 = [=]{
        emit modCacheUpdated();
        emit updateFinished(true);
    };

    QAria2Downloader *downloader;

    if(type == ModWebsiteType::Curseforge){
        if(curseforgeUpdate_.updateFileInfo())
            downloader = curseforgeUpdate_.update(path, modFile_->commonInfo()->iconBytes(), callback1, callback2);
        else
            downloader = curseforgeUpdate_.update(path, modFile_->commonInfo()->iconBytes(), callback1, callback2);
    }
    else /*if(type == ModWebsiteType::Modrinth)*/{
        if(modrinthUpdate_.updateFileInfo())
            downloader = modrinthUpdate_.update(path, modFile_->commonInfo()->iconBytes(), callback1, callback2);
        else
            downloader = modrinthUpdate_.update(path, modFile_->commonInfo()->iconBytes(), callback1, callback2);
    }

    connect(downloader, &AbstractDownloader::downloadProgress, this, &LocalMod::updateProgress);
    return downloader;
}

qint64 LocalMod::updateSize(ModWebsiteType type) const
{
    if(type == Curseforge)
        return curseforgeUpdate_.size();
    if(type == Modrinth)
        return modrinthUpdate_.size();

    return 0;
}

CurseforgeAPI *LocalMod::curseforgeAPI() const
{
    return curseforgeAPI_;
}

ModrinthAPI *LocalMod::modrinthAPI() const
{
    return modrinthAPI_;
}

void LocalMod::addOldFile(LocalModFile *oldFile)
{
    oldFile->setParent(this);
    oldFiles_ << oldFile;
    emit modFileUpdated();
}

void LocalMod::addDuplicateFile(LocalModFile *duplicateFile)
{
    duplicateFile->setParent(this);
    duplicateFiles_ << duplicateFile;
}

void LocalMod::duplicateToOld()
{
    for(auto &file : qAsConst(duplicateFiles_)){
        file->addOld();
        oldFiles_ << file;
    }

    duplicateFiles_.clear();
    emit modFileUpdated();
}

void LocalMod::rollback(LocalModFile *file)
{
    auto disabled = isDisabled();
    if(disabled) setEnabled(true);
    file->removeOld();
    oldFiles_.removeAll(file);
    modFile_->addOld();
    oldFiles_ << modFile_;
    setModFile(file);
    if(disabled) setEnabled(false);
    emit modFileUpdated();

    //reset update info
    curseforgeUpdate_.reset(true);
    modrinthUpdate_.reset(true);
    connect(this, &LocalMod::websiteReady, this, [=]{
        checkUpdates(false);
    });
    searchOnWebsite();
}

void LocalMod::deleteAllOld()
{
    for(auto &oldFile : qAsConst(oldFiles_))
        oldFile->remove();
    clearQObjects(oldFiles_);
    emit modFileUpdated();
}

void LocalMod::deleteOld(LocalModFile *file)
{
    if(file->remove()){
        oldFiles_.removeOne(file);
        file->setParent(nullptr);
        file->deleteLater();
    }
    emit modFileUpdated();
}

bool LocalMod::isDisabled()
{
    return modFile_->type() == LocalModFile::Disabled;
}

bool LocalMod::setEnabled(bool enabled)
{
    auto bl = modFile_->setEnabled(enabled);
    if(bl)
        emit modFileUpdated();
    return bl;
}

void LocalMod::ignoreUpdate(ModWebsiteType type)
{
    if(type == Curseforge)
        curseforgeUpdate_.addIgnore();
    else if(type == Modrinth)
        modrinthUpdate_.addIgnore();
    emit modCacheUpdated();
    emit updateReady(defaultUpdateType());
}

void LocalMod::clearIgnores()
{
    curseforgeUpdate_.clearIgnores();
    modrinthUpdate_.clearIgnores();
    emit modCacheUpdated();
    checkUpdates();
}

void LocalMod::addDepend(std::tuple<QString, QString, std::optional<FabricModInfo> > modDepend)
{
    depends_ << modDepend;
}

void LocalMod::addConflict(std::tuple<QString, QString, FabricModInfo> modConflict)
{
    conflicts_ << modConflict;
}

void LocalMod::addBreak(std::tuple<QString, QString, FabricModInfo> modBreak)
{
    breaks_ << modBreak;
}

LocalModFile *LocalMod::modFile() const
{
    return modFile_;
}

const CommonModInfo *LocalMod::commonInfo() const
{
    return modFile_->commonInfo();
}

QString LocalMod::displayName() const
{
    if(!alias_.isEmpty())
        return alias_;
    if(!commonInfo()->name().isEmpty())
        return commonInfo()->name();
    return modFile_->fileInfo().fileName();
}

const QList<LocalModFile *> &LocalMod::oldFiles() const
{
    return oldFiles_;
}

const QList<LocalModFile *> &LocalMod::duplicateFiles() const
{
    return duplicateFiles_;
}

const Updatable<CurseforgeFileInfo> &LocalMod::curseforgeUpdate() const
{
    return curseforgeUpdate_;
}

const Updatable<ModrinthFileInfo> &LocalMod::modrinthUpdate() const
{
    return modrinthUpdate_;
}

void LocalMod::setCurrentCurseforgeFileInfo(const CurseforgeFileInfo &info, bool cache)
{
    if(info.id() == 0) return;
    curseforgeUpdate_.setCurrentFileInfo(info);
    emit modCacheUpdated();
    if(cache)
        KnownFile::addCurseforge(modFile_->murmurhash(), info);
}

void LocalMod::setCurrentModrinthFileInfo(const ModrinthFileInfo &info, bool cache)
{
    if(info.id().isEmpty()) return;
    modrinthUpdate_.setCurrentFileInfo(info);
    emit modCacheUpdated();
    if(cache)
        KnownFile::addModrinth(modFile_->sha1(), info);
}

const QList<std::tuple<QString, QString, std::optional<FabricModInfo> > > &LocalMod::depends() const
{
    return depends_;
}

const QList<std::tuple<QString, QString, FabricModInfo> > &LocalMod::conflicts() const
{
    return conflicts_;
}

const QList<std::tuple<QString, QString, FabricModInfo> > &LocalMod::breaks() const
{
    return breaks_;
}

void LocalMod::setCurseforgeId(int id, bool cache)
{
    if(id != 0){
        curseforgeMod_ = new CurseforgeMod(this, id);
        emit curseforgeReady(true);
        if(cache)
            IdMapper::addCurseforge(commonInfo()->id(), id);
    } else
        emit curseforgeReady(false);
}

void LocalMod::setModrinthId(const QString &id, bool cache)
{
    if(!id.isEmpty()){
        modrinthMod_ = new ModrinthMod(this, id);
        emit modrinthReady(true);
        if(cache)
            IdMapper::addModrinth(commonInfo()->id(), id);
    } else
        emit modrinthReady(false);
}

const QString &LocalMod::alias() const
{
    return alias_;
}

void LocalMod::setAlias(const QString &newAlias)
{
    if(alias_ == newAlias) return;
    alias_ = newAlias;
    emit modFileUpdated();
    emit modCacheUpdated();
}

QJsonObject LocalMod::toJsonObject() const
{
    QJsonObject object;

    if(!alias_.isEmpty())
        object.insert("alias", alias_);
    if(isFeatured_)
        object.insert("featured", isFeatured_);
    if(!tagManager_.tags(TagCategory::CustomizableCategories).isEmpty()){
        QJsonArray tagArray;
        for(auto &&tag : tagManager_.tags(TagCategory::CustomizableCategories))
            tagArray << tag.toJsonValue();
        object.insert("tags", tagArray);
    }

    if(curseforgeMod_){
        QJsonObject curseforgeObject;
        curseforgeObject.insert("id", curseforgeMod_->modInfo().id());
        if(curseforgeUpdate_.currentFileInfo())
            curseforgeObject.insert("currentFileInfo", curseforgeUpdate_.currentFileInfo()->toJsonObject());
        if(curseforgeUpdate_.updateFileInfo())
            curseforgeObject.insert("updateFileInfo", curseforgeUpdate_.updateFileInfo()->toJsonObject());
        if(!curseforgeUpdate_.ignores().isEmpty()){
            QJsonArray ignoreArray;
            for(auto &&fileId : curseforgeUpdate_.ignores())
                ignoreArray << fileId;
            curseforgeObject.insert("ignores", ignoreArray);
        }
        object.insert("curseforge", curseforgeObject);
    }

    if(modrinthMod_){
        QJsonObject modrinthObject;
        modrinthObject.insert("id", modrinthMod_->modInfo().id());
        if(modrinthUpdate_.currentFileInfo())
            modrinthObject.insert("currentFileInfo", modrinthUpdate_.currentFileInfo()->toJsonObject());
        if(modrinthUpdate_.updateFileInfo())
            modrinthObject.insert("updateFileInfo", modrinthUpdate_.updateFileInfo()->toJsonObject());
        if(!modrinthUpdate_.ignores().isEmpty()){
            QJsonArray ignoreArray;
            for(auto &&fileId : modrinthUpdate_.ignores())
                ignoreArray << fileId;
            modrinthObject.insert("ignores", ignoreArray);
        }
        object.insert("modrinth", modrinthObject);
    }

    return object;
}

void LocalMod::restore(const QVariant &variant)
{
    alias_ = value(variant, "alias").toString();
    isFeatured_ = value(variant, "featured").toBool();
    for(auto &&tag : value(variant, "tags").toList())
        tagManager_ << Tag::fromVariant(tag);
    if(contains(variant, "curseforge")){
        if(contains(value(variant, "curseforge"), "id"))
            setCurseforgeId(value(variant, "curseforge", "id").toInt());
        if(contains(value(variant, "curseforge"), "currentFileInfo"))
            setCurrentCurseforgeFileInfo(CurseforgeFileInfo::fromVariant(value(variant, "curseforge", "currentFileInfo")));
        if(contains(value(variant, "curseforge"), "updateFileInfo"))
            curseforgeUpdate_.setUpdateFileInfo(CurseforgeFileInfo::fromVariant(value(variant, "curseforge", "updateFileInfo")));
        if(contains(value(variant, "curseforge"), "ignores"))
            for(auto &&id : value(variant, "curseforge", "ignores").toList())
                curseforgeUpdate_.addIgnore(id.toInt());
    }
    if(contains(variant, "modrinth")){
        if(contains(value(variant, "modrinth"), "id"))
            setModrinthId(value(variant, "modrinth", "id").toString());
        if(contains(value(variant, "modrinth"), "currentFileInfo"))
            setCurrentModrinthFileInfo(ModrinthFileInfo::fromVariant(value(variant, "modrinth", "currentFileInfo")));
        if(contains(value(variant, "modrinth"), "updateFileInfo"))
            modrinthUpdate_.setUpdateFileInfo(ModrinthFileInfo::fromVariant(value(variant, "modrinth", "updateFileInfo")));
        if(contains(value(variant, "modrinth"), "ignores"))
            for(auto &&id : value(variant, "modrinth", "ignores").toList())
                modrinthUpdate_.addIgnore(id.toString());
    }
}

bool LocalMod::isFeatured() const
{
    return isFeatured_;
}

void LocalMod::setFeatured(bool featured)
{
    isFeatured_ = featured;
    emit modCacheUpdated();
    emit modFileUpdated();
}

LocalModPath *LocalMod::path() const
{
    return path_;
}

const QList<Tag> LocalMod::tags() const
{
    return tagManager_.tags();
}

const QList<Tag> LocalMod::customizableTags() const
{
    return tagManager_.tags(TagCategory::CustomizableCategories);
}

void LocalMod::addTag(const Tag &tag)
{
    tagManager_.addTag(tag);
    emit modCacheUpdated();
    emit modFileUpdated();
}

void LocalMod::removeTag(const Tag &tag)
{
    tagManager_.removeTag(tag);
    emit modCacheUpdated();
    emit modFileUpdated();
}

const LocalModTags &LocalMod::tagManager() const
{
    return tagManager_;
}

void LocalMod::setModFile(LocalModFile *newModFile)
{
    modFile_ = newModFile;
    modFile_->setParent(this);
    tagManager_.removeTags({TagCategory::EnvironmentCategory});
    if(modFile_->loaderType() == ModLoaderType::Fabric){
        if(auto environment = modFile_->fabric().environment(); environment == "*"){
            addTag(Tag::clientTag());
            addTag(Tag::serverTag());
        }else if(environment == "client")
            addTag(Tag::clientTag());
        else if(environment == "server")
            addTag(Tag::serverTag());
    }
}

ModrinthMod *LocalMod::modrinthMod() const
{
    return modrinthMod_;
}
