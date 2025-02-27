#include "curseforgemoditemwidget.h"
#include "ui_curseforgemoditemwidget.h"

#include <QDebug>
#include <QMenu>

#include "local/localmodpath.h"
#include "curseforge/curseforgemod.h"
#include "download/downloadmanager.h"
#include "download/qaria2downloader.h"
#include "util/funcutil.h"
#include "util/youdaotranslator.h"

CurseforgeModItemWidget::CurseforgeModItemWidget(QWidget *parent, CurseforgeMod *mod, const std::optional<CurseforgeFileInfo> &defaultDownload) :
    QWidget(parent),
    ui(new Ui::CurseforgeModItemWidget),
    mod_(mod),
    defaultFileInfo_(defaultDownload)
{
    ui->setupUi(this);
    ui->downloadProgress->setVisible(false);
    connect(mod_, &CurseforgeMod::iconReady, this, &CurseforgeModItemWidget::updateIcon);

    auto menu = new QMenu(this);

    if(defaultFileInfo_){
        auto name = defaultFileInfo_.value().displayName() + " ("+ numberConvert(defaultFileInfo_.value().size(), "B") + ")";
        connect(menu->addAction(QIcon::fromTheme("starred-symbolic"), name), &QAction::triggered, this, [=]{
            downloadFile(*defaultFileInfo_);
        });

        if(!mod->modInfo().latestFileList().isEmpty())
            menu->addSeparator();
    }

    for(const auto &fileInfo : mod->modInfo().latestFileList()){
        auto name = fileInfo.displayName() + " ("+ numberConvert(fileInfo.size(), "B") + ")";
        connect(menu->addAction(name), &QAction::triggered, this, [=]{
            downloadFile(fileInfo);
        });
    }

    ui->downloadButton->setMenu(menu);

    ui->modName->setText(mod->modInfo().name());
    ui->modSummary->setText(mod->modInfo().summary());
//    YoudaoTranslator::translator()->translate(mod->modInfo().summary(), [=](const auto &translted){
//        if(!translted.isEmpty())
//            ui->modSummary->setText(translted);
//    });
    ui->modAuthors->setText(mod->modInfo().authors().join("</b>, <b>").prepend("by <b>").append("</b>"));
    ui->modUpdateDate->setText(tr("%1 ago").arg(timesTo(mod->modInfo().dateModified())));
    ui->modUpdateDate->setToolTip(mod->modInfo().dateModified().toString());
    ui->modCreateDate->setText(tr("%1 ago").arg(timesTo(mod->modInfo().dateCreated())));
    ui->modCreateDate->setToolTip(mod->modInfo().dateCreated().toString());

    //tags
    for(auto &&tag : mod_->tags()){
        auto label = new QLabel(this);
        label->setSizePolicy(QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred));
        if(!tag.iconName().isEmpty())
            label->setText(QString(R"(<img src="%1" height="22" width="22"/>)").arg(tag.iconName()));
        else
            label->setText(tag.name());
        label->setToolTip(tag.name());
        if(tag.tagCategory() != TagCategory::CurseforgeCategory)
            label->setStyleSheet(QString("color: #fff; background-color: %1; border-radius:10px; padding:2px 4px;").arg(tag.tagCategory().color().name()));
        ui->tagsLayout->addWidget(label);
    }

    //loader type
    for(auto &&loaderType : mod_->modInfo().loaderTypes()){
        auto label = new QLabel(this);
        label->setSizePolicy(QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred));
        if(loaderType == ModLoaderType::Fabric)
            label->setText(QString(R"(<img src=":/image/fabric.png" height="22" width="22"/>)"));
        else if(loaderType == ModLoaderType::Forge)
            label->setText(QString(R"(<img src=":/image/forge.svg" height="22" width="22"/>)"));
        else
            label->setText(ModLoaderType::toString(loaderType));
        label->setToolTip(ModLoaderType::toString(loaderType));
        ui->loadersLayout->addWidget(label);
    }

    if(defaultFileInfo_.has_value())
        ui->downloadSpeedText->setText(numberConvert(defaultDownload.value().size(), "B") + "\n"
                                       + numberConvert(mod->modInfo().downloadCount(), "", 3, 1000) + tr(" Downloads"));
    else
        ui->downloadSpeedText->setText(numberConvert(mod->modInfo().downloadCount(), "", 3, 1000) + tr(" Downloads"));

    updateUi();
}

CurseforgeModItemWidget::~CurseforgeModItemWidget()
{
    delete ui;
}

void CurseforgeModItemWidget::updateIcon()
{
    QPixmap pixelmap;
    pixelmap.loadFromData(mod_->modInfo().iconBytes());
    ui->modIcon->setPixmap(pixelmap.scaled(80, 80, Qt::KeepAspectRatio));
}

void CurseforgeModItemWidget::downloadFile(const CurseforgeFileInfo &fileInfo)
{
    ui->downloadButton->setText(tr("Downloading"));
    ui->downloadButton->setEnabled(false);
    ui->downloadProgress->setVisible(true);

    QAria2Downloader *downloader;
    DownloadFileInfo info(fileInfo);
    QPixmap pixelmap;
    pixelmap.loadFromData(mod_->modInfo().iconBytes());
    info.setIcon(pixelmap);
    info.setTitle(mod_->modInfo().name());
    if(downloadPath_)
        downloader = downloadPath_->downloadNewMod(info);
    else{
        info.setPath(Config().getDownloadPath());
        downloader = DownloadManager::manager()->download(info);
    }

    ui->downloadProgress->setMaximum(fileInfo.size());

    connect(downloader, &AbstractDownloader::downloadProgress, this, [=](qint64 bytesReceived, qint64 /*bytesTotal*/){
        ui->downloadProgress->setValue(bytesReceived);
    });
    connect(downloader, &AbstractDownloader::downloadSpeed, this, [=](qint64 bytesPerSec){
        ui->downloadSpeedText->setText(numberConvert(bytesPerSec, "B/s"));
    });
    connect(downloader, &AbstractDownloader::finished, this, [=]{
        ui->downloadProgress->setVisible(false);
        ui->downloadSpeedText->setText(numberConvert(fileInfo.size(), "B"));
        ui->downloadButton->setText(tr("Downloaded"));
    });
}

CurseforgeMod *CurseforgeModItemWidget::mod() const
{
    return mod_;
}

void CurseforgeModItemWidget::setDownloadPath(LocalModPath *newDownloadPath)
{
    downloadPath_ = newDownloadPath;

    bool bl = false;
    if(downloadPath_)
        bl = hasFile(downloadPath_, mod_);
    else{
        if(defaultFileInfo_)
            bl = hasFile(Config().getDownloadPath(), defaultFileInfo_->fileName());
        if(!mod_->modInfo().latestFileList().isEmpty())
            for(const auto &fileInfo : mod_->modInfo().latestFileList()){
                if(hasFile(Config().getDownloadPath(), fileInfo.fileName())){
                    bl = true;
                    break;
                }
            }
    }
    if(bl){
        ui->downloadButton->setEnabled(false);
        ui->downloadButton->setText(tr("Downloaded"));
    } else{
        ui->downloadButton->setEnabled(true);
        ui->downloadButton->setText(tr("Download"));
    }
}

void CurseforgeModItemWidget::updateUi()
{
    Config config;
    ui->modDateTime->setVisible(config.getShowModDateTime());
    ui->tags->setVisible(config.getShowModCategory());
    ui->loaderTypes->setVisible(config.getShowModLoaderType());
}
