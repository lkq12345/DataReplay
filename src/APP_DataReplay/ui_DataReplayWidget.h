/********************************************************************************
** Form generated from reading UI file 'DataReplayWidget.ui'
**
** Created by: Qt User Interface Compiler version 5.12.6
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_DATAREPLAYWIDGET_H
#define UI_DATAREPLAYWIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QTableView>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QTreeView>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_DataReplayWidget
{
public:
    QVBoxLayout *verticalLayout_Main;
    QLabel *label_Title;
    QHBoxLayout *horizontalLayout_Main;
    QGroupBox *groupBox_FileMgr;
    QVBoxLayout *verticalLayout;
    QHBoxLayout *horizontalLayout;
    QLineEdit *edit_SearchFile;
    QPushButton *Btn_SearchFile;
    QPushButton *Btn_Import;
    QTreeView *treeView_Scenario;
    QVBoxLayout *verticalLayout_Right;
    QGroupBox *groupBox_Entities;
    QVBoxLayout *verticalLayout_Entities;
    QTableView *tableView_Entities;
    QHBoxLayout *horizontalLayout_SaveMapping;
    QSpacerItem *horizontalSpacer_SaveMapping;
    QLineEdit *lineEdit_SearchEntity;
    QPushButton *Btn_SearchEntity;
    QPushButton *btn_SaveMapping;
    QGroupBox *groupBox_Control;
    QVBoxLayout *verticalLayout_Control;
    QHBoxLayout *horizontalLayout_Buttons;
    QPushButton *btn_Init;
    QPushButton *btn_Start;
    QPushButton *btn_Pause;
    QPushButton *btn_Resume;
    QPushButton *btn_Stop;
    QSpacerItem *horizontalSpacer_Buttons;
    QHBoxLayout *horizontalLayout_SpeedProgress;
    QLabel *label_SpeedText;
    QLineEdit *edit_Speed;
    QLabel *label_SpeedUnit;
    QLabel *label_Separator;
    QLabel *label_SimTimeTitle;
    QLabel *label_SimTime;
    QSpacerItem *horizontalSpacer_Speed;
    QProgressBar *progressBar;
    QGroupBox *groupBox_Log;
    QVBoxLayout *verticalLayout_Log;
    QTextEdit *textEdit_Log;
    QFrame *frame_StatusBar;
    QHBoxLayout *horizontalLayout_Status;
    QLabel *label_StatusNATS;
    QLabel *label_StatusScenario;
    QLabel *label_StatusProgress;
    QLabel *label_StatusState;
    QSpacerItem *horizontalSpacer_Status;

    void setupUi(QWidget *DataReplayWidget)
    {
        if (DataReplayWidget->objectName().isEmpty())
            DataReplayWidget->setObjectName(QString::fromUtf8("DataReplayWidget"));
        DataReplayWidget->resize(1200, 800);
        DataReplayWidget->setMinimumSize(QSize(900, 600));
        verticalLayout_Main = new QVBoxLayout(DataReplayWidget);
        verticalLayout_Main->setSpacing(4);
        verticalLayout_Main->setObjectName(QString::fromUtf8("verticalLayout_Main"));
        verticalLayout_Main->setContentsMargins(6, 6, 6, 4);
        label_Title = new QLabel(DataReplayWidget);
        label_Title->setObjectName(QString::fromUtf8("label_Title"));
        label_Title->setAlignment(Qt::AlignCenter);

        verticalLayout_Main->addWidget(label_Title);

        horizontalLayout_Main = new QHBoxLayout();
        horizontalLayout_Main->setSpacing(6);
        horizontalLayout_Main->setObjectName(QString::fromUtf8("horizontalLayout_Main"));
        groupBox_FileMgr = new QGroupBox(DataReplayWidget);
        groupBox_FileMgr->setObjectName(QString::fromUtf8("groupBox_FileMgr"));
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(groupBox_FileMgr->sizePolicy().hasHeightForWidth());
        groupBox_FileMgr->setSizePolicy(sizePolicy);
        groupBox_FileMgr->setMinimumSize(QSize(280, 0));
        groupBox_FileMgr->setMaximumSize(QSize(400, 16777215));
        verticalLayout = new QVBoxLayout(groupBox_FileMgr);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        edit_SearchFile = new QLineEdit(groupBox_FileMgr);
        edit_SearchFile->setObjectName(QString::fromUtf8("edit_SearchFile"));
        edit_SearchFile->setClearButtonEnabled(true);

        horizontalLayout->addWidget(edit_SearchFile);

        Btn_SearchFile = new QPushButton(groupBox_FileMgr);
        Btn_SearchFile->setObjectName(QString::fromUtf8("Btn_SearchFile"));

        horizontalLayout->addWidget(Btn_SearchFile);

        Btn_Import = new QPushButton(groupBox_FileMgr);
        Btn_Import->setObjectName(QString::fromUtf8("Btn_Import"));

        horizontalLayout->addWidget(Btn_Import);


        verticalLayout->addLayout(horizontalLayout);

        treeView_Scenario = new QTreeView(groupBox_FileMgr);
        treeView_Scenario->setObjectName(QString::fromUtf8("treeView_Scenario"));
        treeView_Scenario->setEditTriggers(QAbstractItemView::NoEditTriggers);
        treeView_Scenario->setSelectionMode(QAbstractItemView::SingleSelection);
        treeView_Scenario->setSelectionBehavior(QAbstractItemView::SelectRows);
        treeView_Scenario->setUniformRowHeights(true);
        treeView_Scenario->setHeaderHidden(true);

        verticalLayout->addWidget(treeView_Scenario);


        horizontalLayout_Main->addWidget(groupBox_FileMgr);

        verticalLayout_Right = new QVBoxLayout();
        verticalLayout_Right->setSpacing(4);
        verticalLayout_Right->setObjectName(QString::fromUtf8("verticalLayout_Right"));
        groupBox_Entities = new QGroupBox(DataReplayWidget);
        groupBox_Entities->setObjectName(QString::fromUtf8("groupBox_Entities"));
        QSizePolicy sizePolicy1(QSizePolicy::Expanding, QSizePolicy::Expanding);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(groupBox_Entities->sizePolicy().hasHeightForWidth());
        groupBox_Entities->setSizePolicy(sizePolicy1);
        verticalLayout_Entities = new QVBoxLayout(groupBox_Entities);
        verticalLayout_Entities->setSpacing(4);
        verticalLayout_Entities->setObjectName(QString::fromUtf8("verticalLayout_Entities"));
        tableView_Entities = new QTableView(groupBox_Entities);
        tableView_Entities->setObjectName(QString::fromUtf8("tableView_Entities"));
        tableView_Entities->setEditTriggers(QAbstractItemView::DoubleClicked);
        tableView_Entities->setAlternatingRowColors(true);
        tableView_Entities->setSelectionMode(QAbstractItemView::SingleSelection);
        tableView_Entities->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableView_Entities->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        tableView_Entities->horizontalHeader()->setStretchLastSection(true);

        verticalLayout_Entities->addWidget(tableView_Entities);

        horizontalLayout_SaveMapping = new QHBoxLayout();
        horizontalLayout_SaveMapping->setObjectName(QString::fromUtf8("horizontalLayout_SaveMapping"));
        horizontalSpacer_SaveMapping = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_SaveMapping->addItem(horizontalSpacer_SaveMapping);

        lineEdit_SearchEntity = new QLineEdit(groupBox_Entities);
        lineEdit_SearchEntity->setObjectName(QString::fromUtf8("lineEdit_SearchEntity"));

        horizontalLayout_SaveMapping->addWidget(lineEdit_SearchEntity);

        Btn_SearchEntity = new QPushButton(groupBox_Entities);
        Btn_SearchEntity->setObjectName(QString::fromUtf8("Btn_SearchEntity"));

        horizontalLayout_SaveMapping->addWidget(Btn_SearchEntity);

        btn_SaveMapping = new QPushButton(groupBox_Entities);
        btn_SaveMapping->setObjectName(QString::fromUtf8("btn_SaveMapping"));

        horizontalLayout_SaveMapping->addWidget(btn_SaveMapping);


        verticalLayout_Entities->addLayout(horizontalLayout_SaveMapping);


        verticalLayout_Right->addWidget(groupBox_Entities);

        groupBox_Control = new QGroupBox(DataReplayWidget);
        groupBox_Control->setObjectName(QString::fromUtf8("groupBox_Control"));
        QSizePolicy sizePolicy2(QSizePolicy::Expanding, QSizePolicy::Maximum);
        sizePolicy2.setHorizontalStretch(0);
        sizePolicy2.setVerticalStretch(0);
        sizePolicy2.setHeightForWidth(groupBox_Control->sizePolicy().hasHeightForWidth());
        groupBox_Control->setSizePolicy(sizePolicy2);
        verticalLayout_Control = new QVBoxLayout(groupBox_Control);
        verticalLayout_Control->setSpacing(6);
        verticalLayout_Control->setObjectName(QString::fromUtf8("verticalLayout_Control"));
        horizontalLayout_Buttons = new QHBoxLayout();
        horizontalLayout_Buttons->setSpacing(8);
        horizontalLayout_Buttons->setObjectName(QString::fromUtf8("horizontalLayout_Buttons"));
        btn_Init = new QPushButton(groupBox_Control);
        btn_Init->setObjectName(QString::fromUtf8("btn_Init"));

        horizontalLayout_Buttons->addWidget(btn_Init);

        btn_Start = new QPushButton(groupBox_Control);
        btn_Start->setObjectName(QString::fromUtf8("btn_Start"));

        horizontalLayout_Buttons->addWidget(btn_Start);

        btn_Pause = new QPushButton(groupBox_Control);
        btn_Pause->setObjectName(QString::fromUtf8("btn_Pause"));

        horizontalLayout_Buttons->addWidget(btn_Pause);

        btn_Resume = new QPushButton(groupBox_Control);
        btn_Resume->setObjectName(QString::fromUtf8("btn_Resume"));

        horizontalLayout_Buttons->addWidget(btn_Resume);

        btn_Stop = new QPushButton(groupBox_Control);
        btn_Stop->setObjectName(QString::fromUtf8("btn_Stop"));

        horizontalLayout_Buttons->addWidget(btn_Stop);

        horizontalSpacer_Buttons = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_Buttons->addItem(horizontalSpacer_Buttons);


        verticalLayout_Control->addLayout(horizontalLayout_Buttons);

        horizontalLayout_SpeedProgress = new QHBoxLayout();
        horizontalLayout_SpeedProgress->setSpacing(8);
        horizontalLayout_SpeedProgress->setObjectName(QString::fromUtf8("horizontalLayout_SpeedProgress"));
        label_SpeedText = new QLabel(groupBox_Control);
        label_SpeedText->setObjectName(QString::fromUtf8("label_SpeedText"));

        horizontalLayout_SpeedProgress->addWidget(label_SpeedText);

        edit_Speed = new QLineEdit(groupBox_Control);
        edit_Speed->setObjectName(QString::fromUtf8("edit_Speed"));
        edit_Speed->setMaximumSize(QSize(60, 16777215));
        edit_Speed->setAlignment(Qt::AlignRight);

        horizontalLayout_SpeedProgress->addWidget(edit_Speed);

        label_SpeedUnit = new QLabel(groupBox_Control);
        label_SpeedUnit->setObjectName(QString::fromUtf8("label_SpeedUnit"));

        horizontalLayout_SpeedProgress->addWidget(label_SpeedUnit);

        label_Separator = new QLabel(groupBox_Control);
        label_Separator->setObjectName(QString::fromUtf8("label_Separator"));

        horizontalLayout_SpeedProgress->addWidget(label_Separator);

        label_SimTimeTitle = new QLabel(groupBox_Control);
        label_SimTimeTitle->setObjectName(QString::fromUtf8("label_SimTimeTitle"));

        horizontalLayout_SpeedProgress->addWidget(label_SimTimeTitle);

        label_SimTime = new QLabel(groupBox_Control);
        label_SimTime->setObjectName(QString::fromUtf8("label_SimTime"));
        label_SimTime->setMinimumSize(QSize(160, 0));

        horizontalLayout_SpeedProgress->addWidget(label_SimTime);

        horizontalSpacer_Speed = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_SpeedProgress->addItem(horizontalSpacer_Speed);


        verticalLayout_Control->addLayout(horizontalLayout_SpeedProgress);

        progressBar = new QProgressBar(groupBox_Control);
        progressBar->setObjectName(QString::fromUtf8("progressBar"));
        progressBar->setValue(0);

        verticalLayout_Control->addWidget(progressBar);


        verticalLayout_Right->addWidget(groupBox_Control);

        groupBox_Log = new QGroupBox(DataReplayWidget);
        groupBox_Log->setObjectName(QString::fromUtf8("groupBox_Log"));
        sizePolicy1.setHeightForWidth(groupBox_Log->sizePolicy().hasHeightForWidth());
        groupBox_Log->setSizePolicy(sizePolicy1);
        groupBox_Log->setMinimumSize(QSize(0, 140));
        groupBox_Log->setMaximumSize(QSize(16777215, 280));
        verticalLayout_Log = new QVBoxLayout(groupBox_Log);
        verticalLayout_Log->setSpacing(2);
        verticalLayout_Log->setObjectName(QString::fromUtf8("verticalLayout_Log"));
        textEdit_Log = new QTextEdit(groupBox_Log);
        textEdit_Log->setObjectName(QString::fromUtf8("textEdit_Log"));
        textEdit_Log->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        textEdit_Log->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        textEdit_Log->setLineWrapMode(QTextEdit::NoWrap);
        textEdit_Log->setReadOnly(true);

        verticalLayout_Log->addWidget(textEdit_Log);


        verticalLayout_Right->addWidget(groupBox_Log);


        horizontalLayout_Main->addLayout(verticalLayout_Right);


        verticalLayout_Main->addLayout(horizontalLayout_Main);

        frame_StatusBar = new QFrame(DataReplayWidget);
        frame_StatusBar->setObjectName(QString::fromUtf8("frame_StatusBar"));
        QSizePolicy sizePolicy3(QSizePolicy::Expanding, QSizePolicy::Preferred);
        sizePolicy3.setHorizontalStretch(0);
        sizePolicy3.setVerticalStretch(0);
        sizePolicy3.setHeightForWidth(frame_StatusBar->sizePolicy().hasHeightForWidth());
        frame_StatusBar->setSizePolicy(sizePolicy3);
        frame_StatusBar->setMinimumSize(QSize(0, 24));
        frame_StatusBar->setFrameShape(QFrame::StyledPanel);
        frame_StatusBar->setFrameShadow(QFrame::Sunken);
        horizontalLayout_Status = new QHBoxLayout(frame_StatusBar);
        horizontalLayout_Status->setSpacing(16);
        horizontalLayout_Status->setObjectName(QString::fromUtf8("horizontalLayout_Status"));
        horizontalLayout_Status->setContentsMargins(6, 2, 6, 2);
        label_StatusNATS = new QLabel(frame_StatusBar);
        label_StatusNATS->setObjectName(QString::fromUtf8("label_StatusNATS"));

        horizontalLayout_Status->addWidget(label_StatusNATS);

        label_StatusScenario = new QLabel(frame_StatusBar);
        label_StatusScenario->setObjectName(QString::fromUtf8("label_StatusScenario"));

        horizontalLayout_Status->addWidget(label_StatusScenario);

        label_StatusProgress = new QLabel(frame_StatusBar);
        label_StatusProgress->setObjectName(QString::fromUtf8("label_StatusProgress"));

        horizontalLayout_Status->addWidget(label_StatusProgress);

        label_StatusState = new QLabel(frame_StatusBar);
        label_StatusState->setObjectName(QString::fromUtf8("label_StatusState"));

        horizontalLayout_Status->addWidget(label_StatusState);

        horizontalSpacer_Status = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_Status->addItem(horizontalSpacer_Status);


        verticalLayout_Main->addWidget(frame_StatusBar);


        retranslateUi(DataReplayWidget);

        QMetaObject::connectSlotsByName(DataReplayWidget);
    } // setupUi

    void retranslateUi(QWidget *DataReplayWidget)
    {
        DataReplayWidget->setWindowTitle(QApplication::translate("DataReplayWidget", "\346\225\260\346\215\256\345\233\236\346\224\276\350\275\257\344\273\266", nullptr));
        label_Title->setText(QApplication::translate("DataReplayWidget", "<h2>\346\225\260\346\215\256\345\233\236\346\224\276\350\275\257\344\273\266</h2>", nullptr));
        groupBox_FileMgr->setTitle(QApplication::translate("DataReplayWidget", "\346\226\207\344\273\266\347\256\241\347\220\206", nullptr));
        edit_SearchFile->setPlaceholderText(QApplication::translate("DataReplayWidget", "\346\220\234\347\264\242\346\226\207\344\273\266...", nullptr));
        Btn_SearchFile->setText(QApplication::translate("DataReplayWidget", "\346\220\234\347\264\242", nullptr));
        Btn_Import->setText(QApplication::translate("DataReplayWidget", "\345\257\274\345\205\245", nullptr));
        groupBox_Entities->setTitle(QApplication::translate("DataReplayWidget", "\345\256\236\344\275\223\351\205\215\347\275\256", nullptr));
        Btn_SearchEntity->setText(QApplication::translate("DataReplayWidget", "\346\220\234\347\264\242", nullptr));
        btn_SaveMapping->setText(QApplication::translate("DataReplayWidget", "\344\277\235\345\255\230\346\230\240\345\260\204", nullptr));
        groupBox_Control->setTitle(QApplication::translate("DataReplayWidget", "\345\257\274\350\260\203\346\216\247\345\210\266", nullptr));
        btn_Init->setText(QApplication::translate("DataReplayWidget", "\345\210\235\345\247\213\345\214\226", nullptr));
        btn_Start->setText(QApplication::translate("DataReplayWidget", "\345\274\200\345\247\213", nullptr));
        btn_Pause->setText(QApplication::translate("DataReplayWidget", "\346\232\202\345\201\234", nullptr));
        btn_Resume->setText(QApplication::translate("DataReplayWidget", "\347\273\247\347\273\255", nullptr));
        btn_Stop->setText(QApplication::translate("DataReplayWidget", "\345\201\234\346\255\242", nullptr));
        label_SpeedText->setText(QApplication::translate("DataReplayWidget", "\345\200\215\351\200\237\357\274\232", nullptr));
        edit_Speed->setText(QApplication::translate("DataReplayWidget", "1", nullptr));
        label_SpeedUnit->setText(QApplication::translate("DataReplayWidget", "x  (\350\214\203\345\233\264:1-100)", nullptr));
        label_Separator->setText(QApplication::translate("DataReplayWidget", "  |  ", nullptr));
        label_SimTimeTitle->setText(QApplication::translate("DataReplayWidget", "\345\275\223\345\211\215\357\274\232", nullptr));
        label_SimTime->setText(QApplication::translate("DataReplayWidget", "--", nullptr));
        progressBar->setFormat(QApplication::translate("DataReplayWidget", "%p%", nullptr));
        groupBox_Log->setTitle(QApplication::translate("DataReplayWidget", "\346\227\245\345\277\227\344\277\241\346\201\257", nullptr));
        label_StatusNATS->setText(QApplication::translate("DataReplayWidget", "NATS: \346\234\252\350\277\236\346\216\245", nullptr));
        label_StatusScenario->setText(QApplication::translate("DataReplayWidget", "\346\203\263\345\256\232: --", nullptr));
        label_StatusProgress->setText(QApplication::translate("DataReplayWidget", "\350\277\233\345\272\246: --", nullptr));
        label_StatusState->setText(QApplication::translate("DataReplayWidget", "\347\212\266\346\200\201: Idle", nullptr));
    } // retranslateUi

};

namespace Ui {
    class DataReplayWidget: public Ui_DataReplayWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_DATAREPLAYWIDGET_H
