#include "replaymoditemwidget.h"
#include "ui_replaymoditemwidget.h"

#include "ui/download/downloadbrowser.h"
#include "download/qaria2downloader.h"
#include "local/localmodpath.h"
#include "replay/replaymod.h"
#include "util/funcutil.h"
#include "config.hpp"

ReplayModItemWidget::ReplayModItemWidget(QWidget *parent, ReplayMod *mod) :
    QWidget(parent),
    ui(new Ui::ReplayModItemWidget),
    mod_(mod)
{
    ui->setupUi(this);
    QPixmap pixmap(":/image/replay.png");
    ui->modIcon->setPixmap(pixmap.scaled(80, 80, Qt::KeepAspectRatio));
    ui->downloadProgress->setVisible(false);
    ui->downloadSpeedText->setText(numberConvert(mod->modInfo().downloadCount(), "", 3, 1000) + tr(" Downloads"));

    ui->displayNameText->setText(mod_->modInfo().name());
    ui->gameVersion->setText(mod_->modInfo().gameVersionString());
    ui->loaderType->setText(ModLoaderType::toString(mod_->modInfo().loaderType()));
}

ReplayModItemWidget::~ReplayModItemWidget()
{
    delete ui;
}

ReplayMod *ReplayModItemWidget::mod() const
{
    return mod_;
}

void ReplayModItemWidget::setDownloadPath(LocalModPath *newDownloadPath)
{
    downloadPath_ = newDownloadPath;

    bool bl;
    if(downloadPath_)
        bl = hasFile(downloadPath_->info().path(), mod_->modInfo().fileName()); //TODO
    else
        bl = hasFile(Config().getDownloadPath(), mod_->modInfo().fileName()); //TODO

    if(bl){
        ui->downloadButton->setEnabled(false);
        ui->downloadButton->setText(tr("Downloaded"));
    } else{
        ui->downloadButton->setEnabled(true);
        ui->downloadButton->setText(tr("Download"));
    }
}

void ReplayModItemWidget::on_downloadButton_clicked()
{
    ui->downloadButton->setEnabled(false);
    ui->downloadProgress->setVisible(true);
    DownloadFileInfo info(mod_->modInfo());
    AbstractDownloader *downloader;
    if(downloadPath_)
        downloader = downloadPath_->downloadNewMod(info);
    else{
        info.setPath(Config().getDownloadPath());
        downloader = DownloadManager::manager()->download(info);
    }
//    connect(downloader, &Downloader::sizeUpdated, this, [=](qint64 size){
//        ui->downloadProgress->setMaximum(size);
//    });
    connect(downloader, &AbstractDownloader::downloadProgress, this, [=](qint64 bytesReceived, qint64 bytesTotal){
        ui->downloadProgress->setValue(bytesReceived);
        ui->downloadProgress->setMaximum(bytesTotal);
    });
    connect(downloader, &AbstractDownloader::downloadSpeed, this, [=](qint64 download, qint64 upload[[maybe_unused]]){
        ui->downloadSpeedText->setText(numberConvert(download, "B/s"));
    });
    connect(downloader, &AbstractDownloader::finished, this, [=]{
        ui->downloadProgress->setVisible(false);
        ui->downloadButton->setText(tr("Downloaded"));
    });
}

