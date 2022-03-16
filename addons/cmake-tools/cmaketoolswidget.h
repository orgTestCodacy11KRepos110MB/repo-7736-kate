#ifndef KATE_CMAKE_TOOLS_WIDGET_H
#define KATE_CMAKE_TOOLS_WIDGET_H

#include <ktexteditor/view.h>
#include <KTextEditor/MainWindow>

#include <QWidget>

#include "ui_cmaketoolswidget.h"

class CMakeToolsWidget : public QWidget, public Ui::CMakeToolsWidget
{
    Q_OBJECT

public:
    CMakeToolsWidget(KTextEditor::MainWindow *mainwindow, QWidget *parent);

    ~CMakeToolsWidget() override;

private Q_SLOTS:
    void checkCMakeListsFolder(KTextEditor::View *v);
    void cmakeToolsBuildDir();
    void cmakeToolsSourceDir();
    void cmakeToolsGenLink();

private:
    KTextEditor::MainWindow *m_mainWindow;
    bool cmakeToolsCheckifConfigured(QString sourceCompile_Commands_json_path, QString buildCompile_Commands_json_path);
    bool cmakeToolsVerifyAndCreateCommands_Compilejson(QString buildCompile_Commands_json_path);
    bool cmakeToolsCreateLink(QString sourceCompile_Commands_json_path, QString buildCompile_Commands_json_path, bool createReturn);
};

#endif