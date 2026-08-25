/**
 * @file DescriptionDialog.h
 * @brief 描述编辑对话框
 *
 * 用于编辑想定描述或数据文件描述。
 * 构造时传入对象名称（想定名/文件名）与初始描述，
 * 通过 description() 获取编辑结果。
 */

#ifndef DESCRIPTIONDIALOG_H
#define DESCRIPTIONDIALOG_H

#include <QDialog>

class QPlainTextEdit;

class DescriptionDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief 构造描述编辑对话框
     * @param title       对象名称（想定名或数据文件名），用于提示编辑对象
     * @param description 当前描述（初始值）
     * @param parent      父窗口
     */
    explicit DescriptionDialog(const QString &title, const QString &description,
                               QWidget *parent = nullptr);

    /** @brief 获取编辑后的描述内容（去首尾空白） */
    QString description() const;

private:
    QPlainTextEdit *m_textEdit = nullptr;   //!< 描述编辑区
};

#endif // DESCRIPTIONDIALOG_H
