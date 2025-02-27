#include "localmoditemwidget.h"
#include "ui_localmoditemwidget.h"

#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <QGraphicsDropShadowEffect>

#include "curseforge/curseforgemod.h"
#include "modrinth/modrinthmod.h"
#include "ui/curseforge/curseforgemoddialog.h"
#include "ui/modrinth/modrinthmoddialog.h"
#include "ui/local/localmodmenu.h"
#include "util/tutil.hpp"

LocalModItemWidget::LocalModItemWidget(QWidget *parent, LocalMod *mod) :
    QWidget(parent),
    ui(new Ui::LocalModItemWidget),
    mod_(mod)
{
    //init ui
    ui->setupUi(this);
    ui->updateProgress->setVisible(false);
    ui->updateButton->setVisible(false);
    ui->updateButton->setEnabled(false);
    ui->curseforgeButton->setVisible(false);
    ui->modrinthButton->setVisible(false);
    ui->disableButton->setVisible(false);
    ui->featuredButton->setVisible(false);

    this->setAttribute(Qt::WA_Hover, true);

    //init info
    updateInfo();

    //signals / slots
    connect(mod_, &LocalMod::modFileUpdated, this, &LocalModItemWidget::updateInfo);

    connect(mod_, &LocalMod::updateReady, this, &LocalModItemWidget::updateReady);

    connect(mod_, &LocalMod::checkCurseforgeStarted, this, &LocalModItemWidget::startCheckCurseforge);
    connect(mod_, &LocalMod::curseforgeReady, this, &LocalModItemWidget::curseforgeReady);
    connect(mod_, &LocalMod::checkCurseforgeUpdateStarted, this, &LocalModItemWidget::startCheckCurseforgeUpdate);

    connect(mod_, &LocalMod::checkModrinthStarted, this, &LocalModItemWidget::startCheckModrinth);
    connect(mod_, &LocalMod::modrinthReady, this, &LocalModItemWidget::modrinthReady);
    connect(mod_, &LocalMod::checkModrinthUpdateStarted, this, &LocalModItemWidget::startCheckModrinthUpdate);

    connect(mod_, &LocalMod::updateStarted, this, &LocalModItemWidget::startUpdate);
    connect(mod_, &LocalMod::updateProgress, this, &LocalModItemWidget::updateProgress);
    connect(mod_, &LocalMod::updateFinished, this, &LocalModItemWidget::finishUpdate);
}

LocalModItemWidget::~LocalModItemWidget()
{
    delete ui;
}

void LocalModItemWidget::enterEvent(QEvent *event)
{
    ui->disableButton->setVisible(true);
    ui->featuredButton->setVisible(true);
    QWidget::enterEvent(event);
}

void LocalModItemWidget::leaveEvent(QEvent *event)
{
    ui->disableButton->setVisible(ui->disableButton->isChecked());
    ui->featuredButton->setVisible(ui->featuredButton->isChecked());
    QWidget::leaveEvent(event);
}

void LocalModItemWidget::updateInfo()
{
    auto setEffect = [=](QWidget *widget, const QString str){
        if(str.contains("</span>")){
            auto *effect = new QGraphicsDropShadowEffect;
            effect->setBlurRadius(4);
            effect->setColor(Qt::darkGray);
            effect->setOffset(1, 1);
            widget->setGraphicsEffect(effect);
        } else
            widget->setGraphicsEffect(nullptr);
    };

    //mod name
    auto displayName = mod_->displayName();
    setEffect(ui->modName, displayName);
    ui->modName->setText(displayName);
    ui->modVersion->setText(mod_->commonInfo()->version());
    auto description = mod_->commonInfo()->description();
    auto index = description.indexOf(".");
    //description
    //show first statements only
    if (index > 0)
        description = description.left(index + 1);
    setEffect(ui->modDescription, description);
    ui->modDescription->setText(description);
    //authors
    if (!mod_->commonInfo()->authors().isEmpty()){
        auto authors = mod_->commonInfo()->authors().join("</b>, <b>").prepend("by <b>").append("</b>");
        setEffect(ui->modAuthors, authors);
        ui->modAuthors->setText(authors);
    }
    else
        ui->modAuthors->setText("");

    auto setIcon = [=](auto &&image) {
        QPixmap pixelmap;
        if (mod_->modFile()->type() == LocalModFile::Disabled){
            auto alphaChannel = image.convertToFormat(QImage::Format_Alpha8);
            image = image.convertToFormat(QImage::Format_Grayscale8);
            image.setAlphaChannel(alphaChannel);
        }
        pixelmap.convertFromImage(image);
        ui->modIcon->setPixmap(pixelmap.scaled(80, 80, Qt::KeepAspectRatio));
    };

    if(!mod_->commonInfo()->iconBytes().isEmpty()){
        QImage image;
        image.loadFromData(mod_->commonInfo()->iconBytes());
        setIcon(std::move(image));
    }else if(mod_->commonInfo()->id() == "optifine"){
        setIcon(QImage(":/image/optifine.png"));
    }else if(mod_->curseforgeMod()){
        auto setCurseforgeIcon = [=]{
            QImage image;
            image.loadFromData(mod_->curseforgeMod()->modInfo().iconBytes());
            setIcon(std::move(image));
        };
        if(!mod_->curseforgeMod()->modInfo().iconBytes().isEmpty())
            setCurseforgeIcon();
        else{
            connect(mod_->curseforgeMod(), &CurseforgeMod::basicInfoReady, this, [=]{
                connect(mod_->curseforgeMod(), &CurseforgeMod::iconReady, this, setCurseforgeIcon);
                mod_->curseforgeMod()->acquireIcon();
            });
            mod_->curseforgeMod()->acquireBasicInfo();
        }
    } else if(mod_->modrinthMod()){
        auto setModrinthIcon = [=]{
            QImage image;
            image.loadFromData(mod_->modrinthMod()->modInfo().iconBytes());
            setIcon(image);
        };
        if(!mod_->modrinthMod()->modInfo().iconBytes().isEmpty())
            setModrinthIcon();
        else{
            mod_->modrinthMod()->acquireFullInfo();
            connect(mod_->modrinthMod(), &ModrinthMod::fullInfoReady, this, [=]{
                connect(mod_->modrinthMod(), &ModrinthMod::iconReady, this, setModrinthIcon);
                mod_->modrinthMod()->acquireIcon();
            });
        }
    } else
        setIcon(QImage(":/image/modmanager.png"));


    if (mod_->curseforgeMod())
        ui->curseforgeButton->setVisible(true);
    if (mod_->modrinthMod())
        ui->modrinthButton->setVisible(true);

    updateReady(mod_->defaultUpdateType());

    //rollback
    if (mod_->oldFiles().isEmpty())
        ui->rollbackButton->setVisible(false);
    else{
        ui->rollbackButton->setVisible(true);
        ui->rollbackButton->setEnabled(true);
        auto menu = new QMenu(this);
        for (const auto &file : mod_->oldFiles())
            connect(menu->addAction(file->commonInfo()->version()), &QAction::triggered, this, [=]{
                        ui->rollbackButton->setEnabled(false);
                        mod_->rollback(file);
                    });
        ui->rollbackButton->setMenu(menu);
    }

    //warning
    if (!mod_->duplicateFiles().isEmpty()){
        ui->warningButton->setToolTip(tr("Duplicate mod!"));
    } else{
        ui->warningButton->setVisible(false);
    }

    //enabled
    if (mod_->isDisabled()){
        ui->disableButton->setChecked(true);
        ui->disableButton->setVisible(true);
        ui->modName->setStyleSheet("color: #777");
        ui->modAuthors->setStyleSheet("color: #777");
        ui->modDescription->setStyleSheet("color: #777;");
        ui->modVersion->setStyleSheet("color: #777");
        ui->updateButton->setEnabled(false);
    } else{
        ui->disableButton->setChecked(false);
        ui->modName->setStyleSheet("");
        ui->modAuthors->setStyleSheet("");
        ui->modDescription->setStyleSheet("");
        ui->modVersion->setStyleSheet("");
        ui->updateButton->setEnabled(true);
    }

    //featured
    if (mod_->isFeatured())
        ui->featuredButton->setVisible(true);
    ui->featuredButton->setChecked(mod_->isFeatured());

    updateUi();
}

void LocalModItemWidget::updateUi()
{
    //tags
    for(auto widget : qAsConst(tagWidgets_)){
        ui->tagsLayout->removeWidget(widget);
        widget->deleteLater();
    }
    tagWidgets_.clear();
    for(auto &&tag : mod_->tagManager().tags(Config().getShowTagCategories())){
        auto label = new QLabel(tag.name(), this);
        label->setToolTip(tag.name());
        label->setStyleSheet(QString("color: #fff; background-color: %1; border-radius:10px; padding:2px 4px;").arg(tag.tagCategory().color().name()));
        ui->tagsLayout->addWidget(label);
        tagWidgets_ << label;
    }
}

void LocalModItemWidget::on_updateButton_clicked()
{
    mod_->update(mod_->defaultUpdateType());
}

void LocalModItemWidget::updateReady(LocalMod::ModWebsiteType defaultType)
{
    if (defaultType == LocalMod::None){
        ui->updateButton->setVisible(false);
        if(ui->updateButton->menu())
            ui->updateButton->menu()->clear();
        return;
    }
    ui->updateButton->setVisible(true);
    ui->updateButton->setText(tr("Update"));
    ui->updateButton->setEnabled(!mod_->isDisabled());

    auto menu = new QMenu(this);
    auto ignoreMenu = new QMenu(tr("Ignore update"), this);
    for(auto &&type : mod_->updateTypes()){
        if (type == LocalMod::Curseforge){
            auto name = mod_->curseforgeUpdate().updateFileInfo()->displayName();
            auto action = menu->addAction(QIcon(":/image/curseforge.svg"), name);
            action->setToolTip(mod_->updateInfos(type).second);
            connect(action, &QAction::triggered, this, [=]{
                mod_->update(type);
            });
            connect(ignoreMenu->addAction(QIcon(":/image/curseforge.svg"), name), &QAction::triggered, this, [=]{
                mod_->ignoreUpdate(type);
            });
        }
        else if (type == LocalMod::Modrinth){
            auto name = mod_->modrinthUpdate().updateFileInfo()->displayName();
            auto action = menu->addAction(QIcon(":/image/modrinth.svg"), name);
            action->setToolTip(mod_->updateInfos(type).second);
            connect(action, &QAction::triggered, this, [=]{
                mod_->update(type);
            });
            connect(ignoreMenu->addAction(QIcon(":/image/modrinth.svg"), name), &QAction::triggered, this, [=]{
                mod_->ignoreUpdate(type);
            });
        }
    }
    menu->addSeparator();
    menu->addMenu(ignoreMenu);
    ui->updateButton->setMenu(menu);
}

void LocalModItemWidget::startCheckCurseforge()
{
    //TODO
}

void LocalModItemWidget::curseforgeReady(bool bl)
{
    ui->curseforgeButton->setVisible(bl);
}

void LocalModItemWidget::startCheckCurseforgeUpdate()
{
    //TODO
}

void LocalModItemWidget::startCheckModrinth()
{
    //TODO
}

void LocalModItemWidget::modrinthReady(bool bl)
{
    ui->modrinthButton->setVisible(bl);
}

void LocalModItemWidget::startCheckModrinthUpdate()
{
    //TODO
}

void LocalModItemWidget::startUpdate()
{
    ui->updateButton->setText(tr("Updating"));
    ui->updateButton->setEnabled(false);
    ui->updateProgress->setVisible(true);
}

void LocalModItemWidget::updateProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    ui->updateProgress->setMaximum(bytesTotal);
    ui->updateProgress->setValue(bytesReceived);
}

void LocalModItemWidget::finishUpdate(bool success)
{
    ui->updateProgress->setVisible(false);
    if (success)
        ui->updateButton->setVisible(false);
    else
    {
        ui->updateButton->setText(tr("Retry Update"));
        ui->updateButton->setEnabled(true);
    }
}

void LocalModItemWidget::on_curseforgeButton_clicked()
{
    auto curseforgeMod = mod_->curseforgeMod();
    if (!curseforgeMod->modInfo().hasBasicInfo())
        curseforgeMod->acquireBasicInfo();
    auto dialog = new CurseforgeModDialog(this, curseforgeMod, mod_);
    dialog->show();
}

void LocalModItemWidget::on_modrinthButton_clicked()
{
    auto modrinthMod = mod_->modrinthMod();
    if (!modrinthMod->modInfo().hasBasicInfo())
        modrinthMod->acquireFullInfo();
    auto dialog = new ModrinthModDialog(this, modrinthMod, mod_);
    dialog->show();
}

void LocalModItemWidget::on_warningButton_clicked()
{
    QString str = tr("Duplicate version of <b>%1</b> was found:").arg(mod_->commonInfo()->name());
    QStringList list(mod_->commonInfo()->version());
    for (const auto &file : mod_->duplicateFiles())
        list << file->commonInfo()->version();
    str += "<ul><li>" + list.join("</li><li>") + "</li></ul>";
    str += tr("Keep one of them and set the others as old mods?");
    if (QMessageBox::Yes == QMessageBox::question(this, tr("Incompatibility"), str))
        mod_->duplicateToOld();
}

void LocalModItemWidget::on_disableButton_toggled(bool checked)
{
    ui->disableButton->setEnabled(false);
    mod_->setEnabled(!checked);
    ui->disableButton->setEnabled(true);
}

void LocalModItemWidget::on_featuredButton_toggled(bool checked)
{
    ui->featuredButton->setEnabled(false);
    ui->featuredButton->setIcon(QIcon::fromTheme(checked ? "starred-symbolic" : "non-starred-symbolic"));
    mod_->setFeatured(checked);
    ui->featuredButton->setEnabled(true);
}

LocalMod *LocalModItemWidget::mod() const
{
    return mod_;
}

void LocalModItemWidget::on_LocalModItemWidget_customContextMenuRequested(const QPoint &pos)
{
    auto menu = new QMenu(this);
    auto localModMenu = new LocalModMenu(this, mod_);
    connect(menu->addAction(QIcon::fromTheme("entry-edit"), tr("Set Alias")), &QAction::triggered, this, [=]{
        bool ok;
        auto alias = QInputDialog::getText(this, tr("Set mod alias"), tr("Alias of <b>%1</b> mod:").arg(mod_->commonInfo()->name()), QLineEdit::Normal, mod_->alias(), &ok);
        if(ok)
            mod_->setAlias(alias);
    });
    menu->addMenu(localModMenu->addTagMenu());
    if(!mod_->customizableTags().isEmpty())
        menu->addMenu(localModMenu->removeTagmenu());
    menu->addSeparator();
    if(!mod_->curseforgeUpdate().ignores().isEmpty() || !mod_->modrinthUpdate().ignores().isEmpty()){
        connect(menu->addAction(tr("Clear update ignores")), &QAction::triggered, this, [=]{
            mod_->clearIgnores();
        });
        menu->addSeparator();
    }
    auto starAction = menu->addAction(QIcon::fromTheme("non-starred-symbolic"), tr("Star"));
    starAction->setCheckable(true);
    starAction->setChecked(mod_->isFeatured());
    connect(starAction, &QAction::toggled, this, [=](bool bl){
        mod_->setFeatured(bl);
    });
    auto disableAction = menu->addAction(QIcon::fromTheme("action-unavailable-symbolic"), tr("Disable"));
    disableAction->setCheckable(true);
    disableAction->setChecked(mod_->isDisabled());
    connect(disableAction, &QAction::toggled, this, [=](bool bl){
        mod_->setEnabled(!bl);
    });
    if(!menu->actions().isEmpty())
        menu->exec(mapToGlobal(pos));
}

