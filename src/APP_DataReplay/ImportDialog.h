/**
 * @file ImportDialog.h
 * @brief 导入想定确认对话框
 *
 * 仅展示想定名称，确认后仅复制 XML 文件并创建空的回放数据/ 目录。
 * 数据文件通过右键菜单「添加数据文件」单独导入。
 */

#ifndef IMPORTDIALOG_H
#define IMPORTDIALOG_H

#include <QDialog>

#include "ScenarioMgr.h"  // for ImportPreview

class ImportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImportDialog(const ImportPreview &preview, QWidget *parent = nullptr);

private:
    void setupUi(const ImportPreview &preview);
};

#endif // IMPORTDIALOG_H
