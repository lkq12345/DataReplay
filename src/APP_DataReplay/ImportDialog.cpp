/**
 * @file ImportDialog.cpp
 * @brief 导入想定确认对话框的实现
 */

#include "ImportDialog.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFrame>

ImportDialog::ImportDialog(const ImportPreview &preview, QWidget *parent)
    : QDialog(parent)
{
    setupUi(preview);
}

void ImportDialog::setupUi(const ImportPreview &preview)
{
    setWindowTitle(QStringLiteral("导入想定"));
    setMinimumSize(360, 0);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);

    // 想定名称
    auto *infoGroup = new QGroupBox(QStringLiteral("想定信息"), this);
    auto *formLayout = new QFormLayout(infoGroup);

    QLabel *labelSceName = new QLabel(preview.sceName, this);
    labelSceName->setStyleSheet("font-weight: bold; font-size: 14px;");
    formLayout->addRow(QStringLiteral("想定名称："), labelSceName);

    mainLayout->addWidget(infoGroup);

    // 覆盖警告
    if (preview.targetExists) {
        auto *labelWarning = new QLabel(
            QStringLiteral("目标目录已存在，确认导入将覆盖旧文件"), this);
        labelWarning->setStyleSheet(
            "color: #C0392B; font-weight: bold; padding: 4px;");
        mainLayout->addWidget(labelWarning);
    }

    mainLayout->addStretch();

    auto *separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    mainLayout->addWidget(separator);

    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认导入"));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    mainLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
