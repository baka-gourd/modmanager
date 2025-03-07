#ifndef LOCALMODPATH_H
#define LOCALMODPATH_H

#include <QObject>

#include "localmod.h"
#include "localmodpathinfo.h"

class LocalModFile;
class LocalMod;
class CurseforgeAPI;
class ModrinthAPI;

class LocalModPath : public QObject
{
    Q_OBJECT
public:
    explicit LocalModPath(const LocalModPathInfo &info, bool deduceLoader = false);
    ~LocalModPath();

    void loadMods(bool autoLoaderType = false);
    void addNormalMod(LocalModFile *file);
    void addOldMod(LocalModFile *file);

    void checkFabric();

    //list of <mod, modid, required version, current mod if present>
    QList<std::tuple<FabricModInfo, QString, QString, std::optional<FabricModInfo>>> checkFabricDepends() const;
    //list of <mod, conflict modid, conflict version, current mod>
    QList<std::tuple<FabricModInfo, QString, QString, FabricModInfo>> checkFabricConflicts() const;
    //list of <mod, break modid, break version, current mod>
    QList<std::tuple<FabricModInfo, QString, QString, FabricModInfo>> checkFabricBreaks() const;

    LocalMod *findLocalMod(const QString &id);

    void searchOnWebsites();
    void checkModUpdates(bool force = true);
    void updateMods(QList<QPair<LocalMod *, LocalMod::ModWebsiteType> > modUpdateList);

    //download new mod
    QAria2Downloader *downloadNewMod(DownloadFileInfo &info);

    const LocalModPathInfo &info() const;
    void setInfo(const LocalModPathInfo &newInfo, bool deduceLoader = false);

    LocalModTags tagManager();

    CurseforgeAPI *curseforgeAPI() const;
    ModrinthAPI *modrinthAPI() const;
    int modCount() const;
    int updatableCount() const;
    const QMap<QString, LocalMod *> &modMap() const;
    void deleteAllOld() const;

    LocalMod *optiFineMod() const;

    bool isLoading() const;

signals:
    void infoUpdated();
    void modListUpdated();
    void loadStarted();
    void loadProgress(int loadedCount, int totalCount);
    void loadFinished();

    void checkWebsitesStarted();
    void websiteCheckedCountUpdated(int checkedCount);
    void websitesReady();

    void checkUpdatesStarted();
    void updateCheckedCountUpdated(int updateCount, int checkedCount);
    void updatesReady();

    void updatesStarted();
    void updatableCountChanged(int count);
    void updatesProgress(qint64 bytesReceived, qint64 bytesTotal);
    void updatesDoneCountUpdated(int doneCount, int totalCount);
    void updatesDone(int successCount, int failCount);
private slots:
    void updateUpdatableCount();
private:
    enum FindResultType{ Environmant, Missing, Mismatch, Match, RangeSemverError, VersionSemverError };
    std::tuple<FindResultType, std::optional<FabricModInfo>> findFabricMod(const QString &modid, const QString &range_str) const;

    void writeToFile();
    void readFromFile();

    static constexpr auto kFileName = "mods.json";
    CurseforgeAPI *curseforgeAPI_;
    ModrinthAPI *modrinthAPI_;
    LocalModPathInfo info_;
    QMap<QString, LocalMod*> modMap_;
    LocalMod *optiFineMod_ = nullptr;
    QMultiMap<QString, FabricModInfo> fabricModMap_;
    QStringList provideList_;
    QDateTime latestUpdateCheck_;
    int updatableCount_ = 0;
    bool initialUpdateChecked_ = false;
    bool isLoading_ = false;
    bool isSearching_ = false;
};

#endif // LOCALMODPATH_H
