#include "downloadfileinfo.h"

DownloadFileInfo::DownloadFileInfo(const QUrl &url) :
    url_(url)
{}

DownloadFileInfo::DownloadFileInfo(const CurseforgeFileInfo &info) :
    DownloadFileInfo(info.url())
{
    displayName_ = info.displayName();
    fileName_ = info.fileName();
    size_ = info.size();
}

DownloadFileInfo::DownloadFileInfo(const ModrinthFileInfo &info) :
    DownloadFileInfo(info.url())
{
    displayName_ = info.displayName();
    fileName_ = info.fileName();
    //modrinth does not provide size info
}

DownloadFileInfo::DownloadFileInfo(const OptifineModInfo &info) :
    DownloadFileInfo(info.downloadUrl())
{
    title_ = "OptiFine";
    icon_.load(":/image/optifine.png");
    displayName_ = info.name();
    fileName_ = info.fileName();
    //optifine does not provide size info
}

DownloadFileInfo::DownloadFileInfo(const ReplayModInfo &info) :
    DownloadFileInfo("https://www.replaymod.com" + info.urlPath())
{
    title_ = "Replay";
    icon_.load(":/image/replay.png");
    displayName_ = info.name();
    //TODO
    //replay does not actually provide filename info
    fileName_ = info.fileName();
    //replay does not provide size info
}

const QString &DownloadFileInfo::fileName() const
{
    return fileName_;
}

const QUrl &DownloadFileInfo::url() const
{
    return url_;
}

qint64 DownloadFileInfo::size() const
{
    return size_;
}

const QString &DownloadFileInfo::path() const
{
    return path_;
}

void DownloadFileInfo::setPath(const QString &newPath)
{
    path_ = newPath;
}

const QPixmap &DownloadFileInfo::icon() const
{
    return icon_;
}

void DownloadFileInfo::setIconBytes(const QByteArray &newIconBytes)
{
    icon_.loadFromData(newIconBytes);
}

const QString &DownloadFileInfo::title() const
{
    return title_;
}

const QString &DownloadFileInfo::displayName() const
{
    return displayName_;
}

void DownloadFileInfo::setTitle(const QString &newTitle)
{
    title_ = newTitle;
}

void DownloadFileInfo::setIcon(const QPixmap &newIcon)
{
    icon_ = newIcon;
}
