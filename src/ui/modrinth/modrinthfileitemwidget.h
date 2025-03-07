#ifndef MODRINTHFILEITEMWIDGET_H
#define MODRINTHFILEITEMWIDGET_H

#include <QWidget>

#include "modrinth/modrinthfileinfo.h"

class ModrinthMod;
class LocalMod;
class LocalModPath;

namespace Ui {
class ModrinthFileItemWidget;
}

class ModrinthFileItemWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ModrinthFileItemWidget(QWidget *parent, ModrinthMod *mod, const ModrinthFileInfo &info, LocalMod* localMod = nullptr);
    ~ModrinthFileItemWidget();

public slots:
    void setDownloadPath(LocalModPath *newDownloadPath);

private slots:
    void updateLocalInfo();

    void on_downloadButton_clicked();

    void on_ModrinthFileItemWidget_customContextMenuRequested(const QPoint &pos);

private:
    Ui::ModrinthFileItemWidget *ui;
    ModrinthMod *mod_;
    LocalMod *localMod_ = nullptr;
    ModrinthFileInfo fileInfo_;

    LocalModPath *downloadPath_ = nullptr;
};

#endif // MODRINTHFILEITEMWIDGET_H
