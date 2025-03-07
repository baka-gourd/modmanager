#include "modrinthfileitemwidget.h"
#include "ui_modrinthfileitemwidget.h"

#include <QMenu>
#include <QClipboard>

#include "local/localmod.h"
#include "local/localmodpath.h"
#include "modrinth/modrinthmod.h"
#include "download/downloadmanager.h"
#include "util/funcutil.h"

ModrinthFileItemWidget::ModrinthFileItemWidget(QWidget *parent, ModrinthMod *mod, const ModrinthFileInfo &info, LocalMod *localMod) :
    QWidget(parent),
    ui(new Ui::ModrinthFileItemWidget),
    mod_(mod),
    localMod_(localMod),
    fileInfo_(info)
{
    ui->setupUi(this);
    ui->downloadProgress->setVisible(false);

    updateLocalInfo();
    if(localMod_)
        connect(localMod_, &LocalMod::modCacheUpdated, this, &ModrinthFileItemWidget::updateLocalInfo);

    ui->fileNameText->setText(fileInfo_.fileName());
    ui->fileDateText->setText(tr("%1 ago").arg(timesTo(info.fileDate())));
    ui->fileDateText->setToolTip(info.fileDate().toString());

    //game version
    for(auto &&version : fileInfo_.gameVersions()){
        auto label = new QLabel(this);
        label->setText(version);
        label->setToolTip(version);
        if(version.isDev())
            label->setStyleSheet(QString("color: #fff; background-color: #735e82; border-radius:8px; padding:1px 2px;"));
        else
            label->setStyleSheet(QString("color: #fff; background-color: #827965; border-radius:8px; padding:1px 2px;"));
        auto font = label->font();
        font.setPointSize(9);
        label->setFont(font);
        ui->versionsLayout->addWidget(label);
    }

    //loader type
    for(auto &&loaderType : fileInfo_.loaderTypes()){
        auto label = new QLabel(this);
        if(loaderType == ModLoaderType::Fabric)
            label->setText(QString(R"(<img src=":/image/fabric.png" height="22" width="22"/>)"));
        else if(loaderType == ModLoaderType::Forge)
            label->setText(QString(R"(<img src=":/image/forge.svg" height="22" width="22"/>)"));
        else
            label->setText(ModLoaderType::toString(loaderType));
        label->setToolTip(ModLoaderType::toString(loaderType));
        ui->loadersLayout->addWidget(label);
    }

    //size
    ui->downloadSpeedText->setVisible(false);
}

ModrinthFileItemWidget::~ModrinthFileItemWidget()
{
    delete ui;
}

void ModrinthFileItemWidget::on_downloadButton_clicked()
{
    ui->downloadButton->setText(tr("Downloading"));
    ui->downloadButton->setEnabled(false);
    ui->downloadProgress->setVisible(true);

    ui->downloadProgress->setMaximum(fileInfo_.size());

    QAria2Downloader *downloader;
    DownloadFileInfo info(fileInfo_);
    QPixmap pixelmap;
    pixelmap.loadFromData(mod_->modInfo().iconBytes());
    info.setIcon(pixelmap);
    info.setTitle(mod_->modInfo().name());
    if(localMod_)
        info.setIconBytes(localMod_->commonInfo()->iconBytes());
    else
        info.setIconBytes(mod_->modInfo().iconBytes());
    if(downloadPath_)
        downloader = downloadPath_->downloadNewMod(info);
    else if(localMod_)
        downloader = localMod_->downloadOldMod(info);
    else{
        info.setPath(Config().getDownloadPath());
        downloader = DownloadManager::manager()->download(info);
    }
    connect(downloader, &AbstractDownloader::downloadProgress, this, [=](qint64 bytesReceived, qint64 bytesTotal){
        ui->downloadProgress->setValue(bytesReceived);
        ui->downloadProgress->setMaximum(bytesTotal);
    });
    connect(downloader, &AbstractDownloader::downloadSpeed, this, [=](qint64 bytesPerSec){
        ui->downloadSpeedText->setText(numberConvert(bytesPerSec, "B/s"));
    });
    connect(downloader, &AbstractDownloader::finished, this, [=]{
        ui->downloadProgress->setVisible(false);
        ui->downloadSpeedText->setText(numberConvert(fileInfo_.size(), "B"));
        ui->downloadButton->setText(tr("Downloaded"));
    });
}

void ModrinthFileItemWidget::setDownloadPath(LocalModPath *newDownloadPath)
{
    downloadPath_ = newDownloadPath;

    bool bl;
    if(downloadPath_)
        bl = hasFile(downloadPath_, fileInfo_);
    else
        bl = hasFile(Config().getDownloadPath(), fileInfo_.fileName());

    if(bl){
        ui->downloadButton->setEnabled(false);
        ui->downloadButton->setText(tr("Downloaded"));
    } else{
        ui->downloadButton->setEnabled(true);
        ui->downloadButton->setText(tr("Download"));
    }
}

void ModrinthFileItemWidget::updateLocalInfo()
{
    auto name = fileInfo_.displayName();
    if(localMod_ && localMod_->modrinthUpdate().currentFileInfo() && localMod_->modrinthUpdate().currentFileInfo()->id() == fileInfo_.id())
        name.prepend("<font color=\"#56a\">" + tr("[Current]") + "</font> ");
    ui->displayNameText->setText(name);
    //refresh downloaded infos
    setDownloadPath(downloadPath_);
}

void ModrinthFileItemWidget::on_ModrinthFileItemWidget_customContextMenuRequested(const QPoint &pos)
{
    auto menu = new QMenu(this);
    connect(menu->addAction(tr("Copy download link")), &QAction::triggered, this, [=]{
        QApplication::clipboard()->setText(fileInfo_.url().toString());
    });
    if(localMod_ && !(localMod_->modrinthUpdate().currentFileInfo() && localMod_->modrinthUpdate().currentFileInfo()->id() == fileInfo_.id()))
        connect(menu->addAction(tr("Set as current")), &QAction::triggered, this, [=]{
            localMod_->setCurrentModrinthFileInfo(fileInfo_);
        });
    if(!menu->isEmpty())
        menu->exec(mapToGlobal(pos));
}
