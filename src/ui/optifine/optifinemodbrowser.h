#ifndef OPTIFINEMODBROWSER_H
#define OPTIFINEMODBROWSER_H

#include "ui/explorebrowser.h"

class OptifineAPI;
class BMCLAPI;
class LocalModPath;

namespace Ui {
class OptifineModBrowser;
}

class OptifineModBrowser : public ExploreBrowser
{
    Q_OBJECT

public:
    explicit OptifineModBrowser(QWidget *parent = nullptr);
    ~OptifineModBrowser();

public slots:
    void refresh() override;
    void searchModByPathInfo(const LocalModPathInfo &info) override;
    void updateUi() override;

signals:
    void downloadPathChanged(LocalModPath *path);

private slots:
    void updateLocalPathList();
    void filterList();

    void on_openFolderButton_clicked();

    void on_downloadPathSelect_currentIndexChanged(int index);

    void on_getOptiFabric_clicked();

    void on_getOptiForge_clicked();

private:
    Ui::OptifineModBrowser *ui;
    OptifineAPI *api_;
    BMCLAPI *bmclapi_;
    LocalModPath *downloadPath_ = nullptr;
    bool isUiSet_ = false;

    void getModList();
};

#endif // OPTIFINEMODBROWSER_H
