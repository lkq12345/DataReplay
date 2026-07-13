/**
 * @file DataReplayWidget.cpp
 * @brief 数据回放主界面的实现
 *
 * 负责 UI 展示、用户交互控制，通过调用后端模块（ScenarioMgr、ReplayEngine）
 * 实现想定加载、回放控制和实体映射管理。
 *
 * 界面布局（由 .ui 文件定义）：
 *   - 左侧：文件管理（QTreeView） + 加载按钮 + 文件信息
 *   - 右侧上：实体配置（QTableView） + 保存映射按钮
 *   - 右侧中：导调控制（初始化/开始/暂停/继续/停止 + 倍速 + 进度条）
 *   - 右侧下：日志信息（QTextEdit）
 *   - 底部：状态栏（NATS / 想定 / 进度 / 状态）
 */

#include "DataReplayWidget.h"
#include "ui_DataReplayWidget.h"

#include "ScenarioMgr.h"
#include "ReplayEngine.h"
#include "LogService.h"
#include "Communication/Communication_NATS.h"

#include <QTreeView>
#include <QTableView>
#include <QTextEdit>
#include <QPushButton>
#include <QLineEdit>
#include <QProgressBar>
#include <QMessageBox>
#include <QDateTime>
#include <QDebug>
#include <QCloseEvent>
#include <QScrollBar>
#include <QTextDocument>
#include <QCoreApplication>
#include <QFileInfo>

// ==================== 列索引定义 ====================
enum EntityColumn {
    Col_ID = 0,
    Col_Name,
    Col_Type,
    Col_Status,
    Col_MapID,
    Col_EntityCount
};

DataReplayWidget::DataReplayWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DataReplayWidget)
{
    ui->setupUi(this);

    // 设置窗口标题
    setWindowTitle(QStringLiteral("数据回放软件 v1.0"));

    // ==================== 创建后端模块 ====================
    m_scenarioMgr = new ScenarioMgr(this);
    m_replayEngine = new ReplayEngine(this);

    // ==================== 初始化模型 ====================
    initTreeModel();
    initEntityTable();

    // ==================== 倍速输入校验 ====================
    m_speedValidator = new QIntValidator(1, 100, this);
    ui->edit_Speed->setValidator(m_speedValidator);
    ui->edit_Speed->setText("1");

    // ==================== 日志初始化 ====================
    // 日志控件限制最大行数（通过 QTextDocument 设置）
    ui->textEdit_Log->document()->setMaximumBlockCount(1000);

    LogService::instance().setLogFile(QCoreApplication::applicationDirPath() + "/logs");
    LogService::instance().log("INFO", "数据回放软件启动");

    // ==================== 连接信号/槽 ====================

    // -- UI 控件信号 --
    connect(ui->btn_LoadScenario, &QPushButton::clicked,
            this, &DataReplayWidget::onLoadScenario);

    connect(ui->btn_Init, &QPushButton::clicked,
            this, &DataReplayWidget::onInit);
    connect(ui->btn_Start, &QPushButton::clicked,
            this, &DataReplayWidget::onStart);
    connect(ui->btn_Pause, &QPushButton::clicked,
            this, &DataReplayWidget::onPause);
    connect(ui->btn_Resume, &QPushButton::clicked,
            this, &DataReplayWidget::onResume);
    connect(ui->btn_Stop, &QPushButton::clicked,
            this, &DataReplayWidget::onStop);

    // 树形列表选中（用 lambda 桥接，避免 Qt 5.12 信号参数不匹配）
    connect(ui->treeView_Scenario->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this]() { onTreeSelectionChanged(); });

    // 保存映射按钮
    connect(ui->btn_SaveMapping, &QPushButton::clicked,
            this, &DataReplayWidget::onSaveMapping);

    // 倍速输入
    connect(ui->edit_Speed, &QLineEdit::editingFinished,
            this, &DataReplayWidget::onSpeedChanged);

    // -- 日志信号 --
    connect(&LogService::instance(), &LogService::newLog,
            this, &DataReplayWidget::appendLog);

    // -- 回放引擎信号 --
    connect(m_replayEngine, &ReplayEngine::stateChanged,
            this, &DataReplayWidget::onEngineStateChanged);
    connect(m_replayEngine, &ReplayEngine::simTimeChanged,
            this, &DataReplayWidget::onSimTimeChanged);
    connect(m_replayEngine, &ReplayEngine::progressChanged,
            this, &DataReplayWidget::onProgressChanged);
    connect(m_replayEngine, &ReplayEngine::replayFinished,
            this, &DataReplayWidget::onReplayFinished);
    connect(m_replayEngine, &ReplayEngine::errorOccurred,
            this, &DataReplayWidget::onError);
    connect(m_replayEngine, &ReplayEngine::logMessage,
            this, [this](const QString &level, const QString &msg) {
                LogService::instance().log(level, msg);
            });

    // ==================== 初始界面状态 ====================
    updateButtonStates();
    updateStatusBar();

    // 启动时自动扫描想定
    refreshScenarioTree();

    LogService::instance().log("INFO", "界面初始化完成");
}

DataReplayWidget::~DataReplayWidget()
{
    // 确保停止回放
    if (m_replayEngine) {
        m_replayEngine->stop();
    }
    delete ui;
}

void DataReplayWidget::closeEvent(QCloseEvent *event)
{
    if (m_replayEngine && m_replayEngine->state() == ReplayEngine::Playing) {
        m_replayEngine->stop();
    }
    LogService::instance().log("INFO", "数据回放软件关闭");
    event->accept();
}

// ==================== 模型初始化 ====================

void DataReplayWidget::initTreeModel()
{
    // 左侧文件管理树：单列，隐藏表头
    m_treeModel = new QStandardItemModel(this);
    m_treeModel->setHorizontalHeaderLabels(QStringList() << QStringLiteral("文件管理"));
    ui->treeView_Scenario->setModel(m_treeModel);
}

void DataReplayWidget::initEntityTable()
{
    m_entityModel = new QStandardItemModel(this);
    m_entityModel->setColumnCount(Col_EntityCount);
    m_entityModel->setHorizontalHeaderLabels({
        QStringLiteral("ID"),
        QStringLiteral("名称"),
        QStringLiteral("类型"),
        QStringLiteral("状态"),
        QStringLiteral("映射ID")
    });

    ui->tableView_Entities->setModel(m_entityModel);

    // 设置列宽
    ui->tableView_Entities->setColumnWidth(Col_ID, 80);
    ui->tableView_Entities->setColumnWidth(Col_Name, 120);
    ui->tableView_Entities->setColumnWidth(Col_Type, 80);
    ui->tableView_Entities->setColumnWidth(Col_Status, 80);
    ui->tableView_Entities->setColumnWidth(Col_MapID, 100);
}

// ==================== 想定扫描与加载 ====================

void DataReplayWidget::refreshScenarioTree()
{
    // 启动时自动扫描 dataFiles/ 目录，将找到的想定显示在树形列表中
    m_treeModel->clear();
    m_treeModel->setHorizontalHeaderLabels(QStringList() << QStringLiteral("文件管理"));

    QList<ScenarioSummary> summaries = m_scenarioMgr->scanScenarios();

    if (summaries.isEmpty()) {
        LogService::instance().log("WARN", "未找到任何想定文件");
        return;
    }

    for (const ScenarioSummary &summary : summaries) {
        // 创建想定父节点
        auto *scenarioItem = new QStandardItem(summary.name);
        scenarioItem->setData(summary.filePath, Qt::UserRole);          // 存储文件路径
        scenarioItem->setData("scenario", Qt::UserRole + 1);            // 节点类型
        scenarioItem->setData(summary.entityCount, Qt::UserRole + 2);   // 实体数量

        // 先不加载完整信息，填入摘要信息到 tooltip
        scenarioItem->setToolTip(QStringLiteral("想定路径: %1\n实体数量: %2")
                                 .arg(summary.filePath)
                                 .arg(summary.entityCount));

        m_treeModel->invisibleRootItem()->appendRow(scenarioItem);
    }

    LogService::instance().log("INFO",
        QStringLiteral("扫描到 %1 个想定").arg(summaries.size()));
}

void DataReplayWidget::onLoadScenario()
{
    // 获取当前选中项
    QModelIndexList selected = ui->treeView_Scenario->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先在左侧树形列表中选择一个想定"));
        return;
    }

    QModelIndex index = selected.first();
    QString filePath = index.data(Qt::UserRole).toString();
    QString nodeType = index.data(Qt::UserRole + 1).toString();

    if (nodeType != "scenario") {
        // 如果选中的是数据文件节点，取其父节点
        QModelIndex parentIndex = index.parent();
        if (parentIndex.isValid()) {
            filePath = parentIndex.data(Qt::UserRole).toString();
            nodeType = parentIndex.data(Qt::UserRole + 1).toString();
        }
        if (nodeType != "scenario") {
            QMessageBox::information(this, QStringLiteral("提示"),
                                     QStringLiteral("请选择一个想定节点"));
            return;
        }
    }

    if (filePath.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("错误"),
                             QStringLiteral("想定文件路径无效"));
        return;
    }

    // 加载想定
    if (!m_scenarioMgr->loadScenario(filePath)) {
        LogService::instance().log("ERROR",
            QStringLiteral("想定加载失败: %1").arg(filePath));
        QMessageBox::critical(this, QStringLiteral("错误"),
                              QStringLiteral("想定加载失败，请检查文件格式"));
        return;
    }

    const Scenario *scenario = m_scenarioMgr->currentScenario();
    if (!scenario) {
        return;
    }

    LogService::instance().log("INFO",
        QStringLiteral("想定加载成功 - %1 (实体:%2, 数据文件:%3)")
            .arg(scenario->name)
            .arg(scenario->entities.size())
            .arg(scenario->dataFiles.size()));

    // ---- 更新树形列表 ----
    // 找到对应的树节点，展开并添加数据文件子节点
    for (int i = 0; i < m_treeModel->invisibleRootItem()->rowCount(); ++i) {
        QStandardItem *item = m_treeModel->invisibleRootItem()->child(i);
        if (item->data(Qt::UserRole).toString() == filePath) {
            // 更新想定行的显示信息
            QString displayText = QStringLiteral("%1\n  步长:%2ms  实体:%3个")
                                  .arg(scenario->name)
                                  .arg(scenario->simStepMs)
                                  .arg(scenario->entities.size());
            item->setText(displayText);

            // 添加数据文件子节点
            item->removeRows(0, item->rowCount());
            for (const QString &dataFile : scenario->dataFiles) {
                DataFileInfo info = m_scenarioMgr->getDataFileInfo(dataFile);
                QFileInfo fi(dataFile);

                QString fileSizeStr;
                if (info.fileSize < 1024) {
                    fileSizeStr = QStringLiteral("%1 B").arg(info.fileSize);
                } else if (info.fileSize < 1024 * 1024) {
                    fileSizeStr = QStringLiteral("%1 KB").arg(info.fileSize / 1024);
                } else {
                    fileSizeStr = QStringLiteral("%1 MB").arg(info.fileSize / (1024 * 1024));
                }

                QString fileDisplay = QStringLiteral("%1  (%2, %3条)")
                                      .arg(fi.fileName())
                                      .arg(fileSizeStr)
                                      .arg(info.recordCount);
                auto *fileItem = new QStandardItem(fileDisplay);
                fileItem->setData(dataFile, Qt::UserRole);
                fileItem->setData("datafile", Qt::UserRole + 1);
                fileItem->setToolTip(QStringLiteral("路径: %1\n大小: %2\n数据条数: %3\n时间范围: %4 ~ %5")
                                     .arg(dataFile)
                                     .arg(fileSizeStr)
                                     .arg(info.recordCount)
                                     .arg(info.minTime.toString("yyyy-MM-dd HH:mm:ss"))
                                     .arg(info.maxTime.toString("yyyy-MM-dd HH:mm:ss")));
                item->appendRow(fileItem);
            }

                // 展开树节点（QTreeView::expand 通过 QModelIndex 展开）
                QModelIndex scenarioIdx = item->index();
                ui->treeView_Scenario->expand(scenarioIdx);
                break;
        }
    }

    // ---- 更新实体表格 ----
    m_entityModel->removeRows(0, m_entityModel->rowCount());
    for (const EntityInfo &entity : scenario->entities) {
        int row = m_entityModel->rowCount();
        m_entityModel->insertRow(row);

        // ID/名称/类型/状态 —— 只读不可编辑
        auto *idItem = new QStandardItem(entity.id);
        idItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        m_entityModel->setItem(row, Col_ID, idItem);

        auto *nameItem = new QStandardItem(entity.name);
        nameItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        m_entityModel->setItem(row, Col_Name, nameItem);

        auto *typeItem = new QStandardItem(entity.type);
        typeItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        m_entityModel->setItem(row, Col_Type, typeItem);

        auto *statusItem = new QStandardItem(entity.status);
        statusItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        m_entityModel->setItem(row, Col_Status, statusItem);

        // 映射ID —— 允许双击编辑
        auto *mapItem = new QStandardItem("");
        mapItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        m_entityModel->setItem(row, Col_MapID, mapItem);
    }

    // 加载该想定的映射关系，预填映射值
    loadMappingForCurrentScenario();

    // ---- 更新文件信息面板 ----
    ui->label_FileInfoValue->setText(QStringLiteral("已加载: %1").arg(scenario->name));

    // ---- 更新状态栏 ----
    updateStatusBar();

    // ---- 重置引擎状态 ----
    m_isInitialized = false;
    m_replayEngine->stop();
    updateButtonStates();
}

// ==================== 回放控制 ====================

/**
 * @brief 初始化回放引擎
 *
 * 流程：创建 DataFileReader 实例 → 扫描数据文件时间范围 → 连接 NATS →
 * 传入实体 ID 映射表 → 进入 Ready 状态等待开始
 */
void DataReplayWidget::onInit()
{
    const Scenario *scenario = m_scenarioMgr->currentScenario();
    if (!scenario) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先加载想定"));
        return;
    }

    LogService::instance().log("INFO", "正在初始化回放引擎...");
    ui->btn_Init->setEnabled(false);

    // 根据选中的节点决定回放哪些数据文件
    // 选中数据文件节点 → 只回放该文件
    // 选中的是想定节点或未选择 → 回放所有文件（传空列表）
    if (m_selectedDataFiles.isEmpty()) {
        LogService::instance().log("INFO",
            QStringLiteral("将回放想定下所有 %1 个数据文件").arg(scenario->dataFiles.size()));
    } else {
        LogService::instance().log("INFO",
            QStringLiteral("将回放选中的 %1 个数据文件").arg(m_selectedDataFiles.size()));
    }

    bool success = m_replayEngine->initialize(scenario, m_selectedDataFiles);
    if (success) {
        m_isInitialized = true;
        // 将当前实体ID映射传给引擎，回放发送时自动替换
        m_replayEngine->setEntityIdMapping(m_entityIdMapping);
        LogService::instance().log("INFO", "回放引擎初始化完成");
    } else {
        LogService::instance().log("ERROR", "回放引擎初始化失败");
    }

    updateButtonStates();
    updateStatusBar();
}

void DataReplayWidget::onStart()
{
    if (!m_isInitialized) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先执行初始化"));
        return;
    }

    LogService::instance().log("INFO", "正在开始回放...");
    m_replayEngine->start();
    // 按钮状态由 stateChanged 信号驱动更新
}

void DataReplayWidget::onPause()
{
    m_replayEngine->pause();
}

void DataReplayWidget::onResume()
{
    m_replayEngine->resume();
}

void DataReplayWidget::onStop()
{
    m_replayEngine->stop();
}

// ==================== 树形列表选中 ====================

void DataReplayWidget::onTreeSelectionChanged()
{
    QModelIndexList selected = ui->treeView_Scenario->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        return;
    }

    QModelIndex index = selected.first();
    QString nodeType = index.data(Qt::UserRole + 1).toString();

    if (nodeType == "datafile") {
        QString filePath = index.data(Qt::UserRole).toString();
        m_selectedDataFiles = QStringList() << filePath;

        QFileInfo fi(filePath);
        DataFileInfo info = m_scenarioMgr->getDataFileInfo(filePath);

        QString fileSizeStr;
        if (info.fileSize < 1024) {
            fileSizeStr = QStringLiteral("%1 B").arg(info.fileSize);
        } else if (info.fileSize < 1024 * 1024) {
            fileSizeStr = QStringLiteral("%1 KB").arg(info.fileSize / 1024);
        } else {
            fileSizeStr = QStringLiteral("%1 MB").arg(info.fileSize / (1024 * 1024));
        }

        ui->label_FileInfoValue->setText(
            QStringLiteral("文件: %1\n大小: %2\n数据条数: %3\n时间: %4 ~ %5")
                .arg(fi.fileName())
                .arg(fileSizeStr)
                .arg(info.recordCount)
                .arg(info.minTime.toString("HH:mm:ss"))
                .arg(info.maxTime.toString("HH:mm:ss")));
    } else if (nodeType == "scenario") {
        // 选中的是想定节点 → 使用所有数据文件
        m_selectedDataFiles.clear();
        QString filePath = index.data(Qt::UserRole).toString();
        if (m_scenarioMgr->currentScenario() &&
            m_scenarioMgr->currentScenario()->filePath == filePath) {
            const Scenario *sc = m_scenarioMgr->currentScenario();
            ui->label_FileInfoValue->setText(
                QStringLiteral("想定: %1\n步长: %2ms\n实体: %3个\n数据文件: %4个")
                    .arg(sc->name)
                    .arg(sc->simStepMs)
                    .arg(sc->entities.size())
                    .arg(sc->dataFiles.size()));
        } else {
            ui->label_FileInfoValue->setText(QStringLiteral("(未加载，点击「加载想定」载入)"));
        }
    }
}

// ==================== 倍速控制 ====================

void DataReplayWidget::onSpeedChanged()
{
    QString text = ui->edit_Speed->text();
    bool ok = false;
    int speed = text.toInt(&ok);

    if (!ok || speed < 1 || speed > 100) {
        ui->edit_Speed->setText(QString::number(m_replayEngine->speed()));
        return;
    }

    m_replayEngine->setSpeed(speed);
}

// ==================== 日志 ====================

void DataReplayWidget::appendLog(const QString &message)
{
    QTextEdit *logEdit = ui->textEdit_Log;
    logEdit->append(message);

    // 自动滚动到底部
    QScrollBar *scrollBar = logEdit->verticalScrollBar();
    if (scrollBar) {
        scrollBar->setValue(scrollBar->maximum());
    }
}

// ==================== 回放状态更新 ====================

void DataReplayWidget::onSimTimeChanged(const QDateTime &simTime)
{
    setSimTimeLabel(simTime);
}

void DataReplayWidget::onProgressChanged(double percent)
{
    ui->progressBar->setValue(qRound(percent));
    updateStatusBar();
}

void DataReplayWidget::onEngineStateChanged(ReplayEngine::State state)
{
    updateButtonStates();
    updateStatusBar();

    // 如果是 Stopped 状态，重置初始化标志
    if (state == ReplayEngine::Stopped || state == ReplayEngine::Idle) {
        m_isInitialized = false;
    }

    // 如果从 Playing 变为其他状态，更新 NATS 状态显示
    if (state == ReplayEngine::Ready || state == ReplayEngine::Stopped) {
        ui->label_StatusNATS->setText(QStringLiteral("NATS: 已连接"));
    }
}

void DataReplayWidget::onReplayFinished()
{
    LogService::instance().log("INFO", "回放已全部完成");
    ui->label_StatusNATS->setText(QStringLiteral("NATS: 已连接"));
    m_isInitialized = false;
}

void DataReplayWidget::onError(const QString &error)
{
    LogService::instance().log("ERROR", error);
    QMessageBox::warning(this, QStringLiteral("错误"), error);
}

// ==================== 辅助函数 ====================

void DataReplayWidget::updateButtonStates()
{
    ReplayEngine::State state = m_replayEngine->state();
    const Scenario *scenario = m_scenarioMgr->currentScenario();

    bool hasScenario = (scenario != nullptr);

    // 根据状态表设置按钮
    //          Idle    Ready   Playing  Paused  Stopped
    // 初始化    ✓       ✓       ✗       ✗       ✓
    // 开始      ✗       ✓       ✗       ✗       ✗
    // 暂停      ✗       ✗       ✓       ✗       ✗
    // 继续      ✗       ✗       ✗       ✓       ✗
    // 停止      ✗       ✓       ✓       ✓       ✗

    switch (state) {
    case ReplayEngine::Idle:
        ui->btn_Init->setEnabled(hasScenario);
        ui->btn_Start->setEnabled(false);
        ui->btn_Pause->setEnabled(false);
        ui->btn_Resume->setEnabled(false);
        ui->btn_Stop->setEnabled(false);
        break;

    case ReplayEngine::Ready:
        ui->btn_Init->setEnabled(hasScenario);
        ui->btn_Start->setEnabled(true);
        ui->btn_Pause->setEnabled(false);
        ui->btn_Resume->setEnabled(false);
        ui->btn_Stop->setEnabled(true);
        break;

    case ReplayEngine::Playing:
        ui->btn_Init->setEnabled(false);
        ui->btn_Start->setEnabled(false);
        ui->btn_Pause->setEnabled(true);
        ui->btn_Resume->setEnabled(false);
        ui->btn_Stop->setEnabled(true);
        break;

    case ReplayEngine::Paused:
        ui->btn_Init->setEnabled(false);
        ui->btn_Start->setEnabled(false);
        ui->btn_Pause->setEnabled(false);
        ui->btn_Resume->setEnabled(true);
        ui->btn_Stop->setEnabled(true);
        break;

    case ReplayEngine::Stopped:
        ui->btn_Init->setEnabled(hasScenario);
        ui->btn_Start->setEnabled(false);
        ui->btn_Pause->setEnabled(false);
        ui->btn_Resume->setEnabled(false);
        ui->btn_Stop->setEnabled(false);
        break;
    }
}

void DataReplayWidget::updateStatusBar()
{
    ReplayEngine::State state = m_replayEngine->state();
    const Scenario *scenario = m_scenarioMgr->currentScenario();

    // 想定名称
    if (scenario) {
        ui->label_StatusScenario->setText(
            QStringLiteral("想定: %1").arg(scenario->name));
    } else {
        ui->label_StatusScenario->setText(QStringLiteral("想定: --"));
    }

    // 进度
    double progress = m_replayEngine->overallProgress();
    ui->label_StatusProgress->setText(
        QStringLiteral("进度: %1%").arg(qRound(progress)));

    // 状态名称
    QString stateStr;
    switch (state) {
    case ReplayEngine::Idle:    stateStr = "Idle";    break;
    case ReplayEngine::Ready:   stateStr = "Ready";   break;
    case ReplayEngine::Playing: stateStr = "Playing"; break;
    case ReplayEngine::Paused:  stateStr = "Paused";  break;
    case ReplayEngine::Stopped: stateStr = "Stopped"; break;
    }
    ui->label_StatusState->setText(QStringLiteral("状态: %1").arg(stateStr));
}

void DataReplayWidget::setSimTimeLabel(const QDateTime &time)
{
    if (time.isValid()) {
        ui->label_SimTime->setText(time.toString("yyyy-MM-dd HH:mm:ss.zzz"));
    } else {
        ui->label_SimTime->setText("--");
    }
}

// ==================== 映射配置 ====================

void DataReplayWidget::loadMappingForCurrentScenario()
{
    const Scenario *scenario = m_scenarioMgr->currentScenario();
    if (!scenario)
        return;

    // 从想定文件路径反推想定目录
    QFileInfo fi(scenario->filePath);
    QString scenarioDir = fi.absolutePath();

    // 加载映射表
    m_entityIdMapping = m_scenarioMgr->loadEntityIdMapping(scenarioDir);

    if (m_entityIdMapping.isEmpty())
        return;

    // 遍历表格每一行，根据实体ID匹配映射值
    for (int row = 0; row < m_entityModel->rowCount(); ++row) {
        QStandardItem *idItem = m_entityModel->item(row, Col_ID);
        if (!idItem)
            continue;

        QString entityId = idItem->text();
        if (m_entityIdMapping.contains(entityId)) {
            QStandardItem *mapItem = m_entityModel->item(row, Col_MapID);
            if (mapItem) {
                mapItem->setText(m_entityIdMapping[entityId]);
            }
        }
    }

    LogService::instance().log("INFO",
        QStringLiteral("已加载实体ID映射，共 %1 条").arg(m_entityIdMapping.size()));
}

void DataReplayWidget::onSaveMapping()
{
    const Scenario *scenario = m_scenarioMgr->currentScenario();
    if (!scenario) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先加载想定"));
        return;
    }

    QFileInfo fi(scenario->filePath);
    QString scenarioDir = fi.absolutePath();

    // 遍历表格收集映射值
    QMap<QString, QString> mappings;
    for (int row = 0; row < m_entityModel->rowCount(); ++row) {
        QStandardItem *idItem = m_entityModel->item(row, Col_ID);
        QStandardItem *mapItem = m_entityModel->item(row, Col_MapID);
        if (!idItem || !mapItem)
            continue;

        QString entityId = idItem->text();
        QString mapValue = mapItem->text().trimmed();

        // 只保存映射值非空的条目
        if (!mapValue.isEmpty()) {
            mappings[entityId] = mapValue;
        }
    }

    if (!m_scenarioMgr->saveEntityIdMapping(scenarioDir, mappings)) {
        QMessageBox::warning(this, QStringLiteral("错误"),
                             QStringLiteral("保存映射失败"));
        return;
    }

    // 更新内存中的映射表
    m_entityIdMapping = mappings;

    LogService::instance().log("INFO",
        QStringLiteral("映射已保存，共 %1 条").arg(mappings.size()));

    QMessageBox::information(this, QStringLiteral("提示"),
                             QStringLiteral("映射保存成功，共 %1 条").arg(mappings.size()));
}
