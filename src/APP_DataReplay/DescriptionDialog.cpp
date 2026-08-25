/**
 * @file DescriptionDialog.cpp
 * @brief 描述编辑对话框的实现
 *
 * 布局：标题标签（对象名称）+ 多行描述编辑区 + 确定/取消按钮。
 * 信号连接使用具名槽/函数指针，不使用 lambda 表达式。
 */

#include "DescriptionDialog.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QVBoxLayout>

DescriptionDialog::DescriptionDialog(const QString &title, const QString &description,
                                     QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("编辑描述"));

    auto *titleLabel = new QLabel(title, this);
    titleLabel->setWordWrap(true);

    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setPlainText(description);
    m_textEdit->setPlaceholderText(QStringLiteral("请输入描述内容..."));
    m_textEdit->setTabChangesFocus(true);

    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(titleLabel);
    layout->addWidget(m_textEdit);
    layout->addWidget(buttonBox);

    resize(400, 260);
}

QString DescriptionDialog::description() const
{
    return m_textEdit->toPlainText().trimmed();
}
