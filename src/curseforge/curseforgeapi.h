#ifndef CURSEFORGEAPI_H
#define CURSEFORGEAPI_H

#include <QObject>
#include <QNetworkAccessManager>
#include <functional>

#include "gameversion.h"
#include "curseforgemodinfo.h"

class CurseforgeAPI : public QObject
{
    Q_OBJECT
    static const QString PREFIX;

public:
    explicit CurseforgeAPI(QObject *parent = nullptr);
    static CurseforgeAPI *api();

    void searchMods(const GameVersion &version, int index, const QString &searchFilter, int category, int sort, std::function<void (QList<CurseforgeModInfo>)> callback);
    void getIdByFingerprint(const QString &fingerprint, std::function<void (int, CurseforgeFileInfo, QList<CurseforgeFileInfo>)> callback, std::function<void()> noMatch);
    void getDescription(int id, std::function<void(QString)> callback);
    void getChangelog(int id, int FileID, std::function<void (QString)> callback);
    void getDownloadUrl(int id, int FileID, std::function<void (QString)> callback);
    void getFileInfo(int id, int FileID, std::function<void(CurseforgeFileInfo)> callback);
    void getFiles(int id, std::function<void (QList<CurseforgeFileInfo>)> callback);
    void getInfo(int id, std::function<void (CurseforgeModInfo)> callback);
    void getTimestamp(std::function<void (QString)> callback);
    void getMinecraftVersionList(std::function<void (QList<GameVersion>)> callback);

    static const QList<std::tuple<int, QString, QString, int> > &getCategories();

private:
    QNetworkAccessManager accessManager_;
};

#endif // CURSEFORGEAPI_H
