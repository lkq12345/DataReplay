/**
 * @file DataReplayWidget.cpp
 * @brief 数据回放主界面的实现
 *
 * 负责 UI 展示、用户交互控制，通过 Server_DataReplay（Facade）调用后端模块。
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
#include "EntityFilterProxyModel.h"
#include "ScenarioFilterProxyModel.h"
#include "ImportDialog.h"
#include "DescriptionDialog.h"

#include <QTreeView>
#include <QTableView>
#include <QHeaderView>
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
#include <QDir>
#include <QTimer>
#include <QMenu>
#include <QInputDialog>
#include <QRegularExpression>
#include <QFileDialog>
#include <QJsonObject>
#include <QItemSelectionModel>

// ==================== 列索引定义 ====================
enum EntityColumn {
    Col_ID = 0,
    Col_Name,
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

    // ==================== 创建后端 Facade ====================
    m_server = new Server_DataReplay(this);

    // ==================== 统一初始化后端服务（日志 + NATS） ====================
    m_server->initialize(QCoreApplication::applicationDirPath() + "/../logs");

    // 启用右键菜单
    ui->treeView_Scenario->setContextMenuPolicy(Qt::CustomContextMenu);

    // ==================== 初始化模型 ====================
    initTreeModel();
    initEntityTable();

    // ==================== 统一连接信号/槽 ====================
    initConnects();

    // ==================== 倍速输入校验 ====================
    m_speedValidator = new QIntValidator(1, 100, this);
    ui->edit_Speed->setValidator(m_speedValidator);
    ui->edit_Speed->setText("1");

    // ==================== 日志初始化 ====================
    // 日志控件限制最大行数（通过 QTextDocument 设置）
    ui->textEdit_Log->document()->setMaximumBlockCount(1000);

    // ==================== 初始界面状态 ====================
    ui->label_StatusNATS->setText(QStringLiteral("NATS: 连接中..."));
    updateButtonStates();
    updateStatusBar();

    // 启动时自动扫描想定
    refreshScenarioTree();

    m_server->log("INFO", "界面初始化完成");
}

DataReplayWidget::~DataReplayWidget()
{
    // 确保停止回放
    if (m_server) {
        m_server->stopReplay();
    }
    delete ui;
}

// ==================== 信号/槽连接 ====================

void DataReplayWidget::initConnects()
{
    // -- UI 控件信号 --

    // 导入想定按钮
    connect(ui->Btn_Import, &QPushButton::clicked,
            this, &DataReplayWidget::onImportScenario);

    // 右键菜单
    connect(ui->treeView_Scenario, &QTreeView::customContextMenuRequested,
            this, &DataReplayWidget::onTreeContextMenu);

    // 按钮点击
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

    // 实体搜索：按钮点击 + 输入框回车
    connect(ui->Btn_SearchEntity, &QPushButton::clicked,
            this, &DataReplayWidget::onSearchEntity);
    connect(ui->lineEdit_SearchEntity, &QLineEdit::returnPressed,
            this, &DataReplayWidget::onSearchEntity);

    // 倍速输入
    connect(ui->edit_Speed, &QLineEdit::editingFinished,
            this, &DataReplayWidget::onSpeedChanged);

    // 文件搜索：按钮点击 + 输入框回车
    connect(ui->edit_SearchFile, &QLineEdit::returnPressed,
            this, &DataReplayWidget::onSearchFile);
    connect(ui->Btn_SearchFile, &QPushButton::clicked,
            this, &DataReplayWidget::onSearchFile);

    // -- 日志信号（通过 Facade 转发） --
    connect(m_server, &Server_DataReplay::newLog,
            this, &DataReplayWidget::appendLog);

    // -- 回放引擎信号（通过 Facade 转发） --
    connect(m_server, &Server_DataReplay::stateChanged,
            this, &DataReplayWidget::onEngineStateChanged);
    connect(m_server, &Server_DataReplay::simTimeChanged,
            this, &DataReplayWidget::onSimTimeChanged);
    connect(m_server, &Server_DataReplay::progressChanged,
            this, &DataReplayWidget::onProgressChanged);
    connect(m_server, &Server_DataReplay::replayFinished,
            this, &DataReplayWidget::onReplayFinished);
    connect(m_server, &Server_DataReplay::errorOccurred,
            this, &DataReplayWidget::onError);

    // -- NATS 连接状态信号（通过 Facade 转发） --
    connect(m_server, &Server_DataReplay::natsConnected,
            this, &DataReplayWidget::onNatsConnected);

    // -- 外部 NATS 指令（经 Facade 转发，QueuedConnection 保证槽在主线程执行） --
    connect(m_server, &Server_DataReplay::natsMessageReceived,
            this, &DataReplayWidget::onNATSMessage);
}

void DataReplayWidget::closeEvent(QCloseEvent *event)
{
    if (m_server && m_server->state() == Server_DataReplay::Playing) {
        m_server->stopReplay();
    }
    m_server->log("INFO", "数据回放软件关闭");
    event->accept();
}

// ==================== 模型初始化 ====================

void DataReplayWidget::initTreeModel()
{
    // 左侧文件管理树：单列，隐藏表头
    m_treeModel = new QStandardItemModel(this);
    m_treeModel->setHorizontalHeaderLabels(QStringList() << QStringLiteral("文件管理"));

    // 创建过滤代理模型，插入在源 model 和 view 之间
    m_proxyModel = new ScenarioFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_treeModel);
    ui->treeView_Scenario->setModel(m_proxyModel);
}

void DataReplayWidget::initEntityTable()
{
    m_entityModel = new QStandardItemModel(this);
    m_entityModel->setColumnCount(Col_EntityCount);
    m_entityModel->setHorizontalHeaderLabels({
        QStringLiteral("ID"),
        QStringLiteral("名称"),
        QStringLiteral("映射ID")
    });

    // 过滤代理模型，支持按 ID/名称/映射ID 模糊搜索
    m_entityProxyModel = new EntityFilterProxyModel(this);
    m_entityProxyModel->setSourceModel(m_entityModel);
    ui->tableView_Entities->setModel(m_entityProxyModel);

    // 列宽均分
    ui->tableView_Entities->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

// ==================== 想定扫描与加载 ====================

void DataReplayWidget::refreshScenarioTree()
{
    // 通过 Facade 调用后端预扫描，不手动遍历文件系统
    m_treeModel->clear();
    m_treeModel->setHorizontalHeaderLabels(QStringList() << QStringLiteral("文件管理"));

    QList<ScenarioSummary> scenarios = m_server->scanScenarios();

    // 按想定目录分组（同一目录可能有多个 XML）
    QMap<QString, QList<ScenarioSummary>> dirMap;
    for (const auto &s : scenarios) {
        dirMap[s.scenarioDir].append(s);
    }

    // 为每个目录构建树节点
    for (auto it = dirMap.begin(); it != dirMap.end(); ++it) {
        QString dirPath = it.key();
        QString dirName = QFileInfo(dirPath).fileName();

        // 读取该想定的描述配置（想定描述 + 数据文件描述），用于 tooltip 展示
        QString scenarioDesc = m_server->loadScenarioDescription(dirPath);
        QMap<QString, QString> dataFileDescs = m_server->loadDataFileDescriptions(dirPath);

        // ---- 想定文件夹节点（不支持描述，无 tooltip） ----
        auto *scenarioItem = new QStandardItem(dirName);
        scenarioItem->setData(dirPath, Qt::UserRole);
        scenarioItem->setData("scenariodir", Qt::UserRole + 1);
        m_treeModel->invisibleRootItem()->appendRow(scenarioItem);

        // ---- XML 文件子节点（想定文件，tooltip 仅显示描述） ----
        for (const auto &summary : it.value()) {
            QString xmlName = QFileInfo(summary.filePath).fileName();
            auto *xmlItem = new QStandardItem(xmlName);
            xmlItem->setData(summary.filePath, Qt::UserRole);
            xmlItem->setData("xmlfile", Qt::UserRole + 1);
            if (!scenarioDesc.isEmpty()) {
                xmlItem->setToolTip(QStringLiteral("描述: %1").arg(scenarioDesc));
            }
            scenarioItem->appendRow(xmlItem);
        }

        // ---- 回放数据文件夹 + 数据文件（仍需遍历文件系统） ----
        QString replayDirPath = dirPath + "/回放数据";
        QDir replayDir(replayDirPath);
        if (replayDir.exists()) {
            auto *dataFolderItem = new QStandardItem(QStringLiteral("回放数据"));
            dataFolderItem->setData(replayDirPath, Qt::UserRole);
            dataFolderItem->setData("datafolder", Qt::UserRole + 1);
            scenarioItem->appendRow(dataFolderItem);

            const QStringList jsonFiles = replayDir.entryList(
                QStringList() << "*.json", QDir::Files, QDir::Name);
            for (const QString &jsonFile : jsonFiles) {
                QString jsonPath = replayDir.absoluteFilePath(jsonFile);
                auto *fileItem = new QStandardItem(jsonFile);
                fileItem->setData(jsonPath, Qt::UserRole);
                fileItem->setData("datafile", Qt::UserRole + 1);
                const QString fileDesc = dataFileDescs.value(jsonFile);
                if (!fileDesc.isEmpty()) {
                    fileItem->setToolTip(QStringLiteral("描述: %1").arg(fileDesc));
                }
                dataFolderItem->appendRow(fileItem);
            }
        }
    }

    m_server->log("INFO",
        QStringLiteral("文件树已加载，共 %1 个想定目录").arg(dirMap.size()));
}

void DataReplayWidget::populateEntityTable(const Scenario *scenario)
{
    m_entityModel->removeRows(0, m_entityModel->rowCount());
    for (const EntityInfo &entity : scenario->entities) {
        int row = m_entityModel->rowCount();
        m_entityModel->insertRow(row);

        auto *idItem = new QStandardItem(entity.id);
        idItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        m_entityModel->setItem(row, Col_ID, idItem);

        auto *nameItem = new QStandardItem(entity.name);
        nameItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        m_entityModel->setItem(row, Col_Name, nameItem);

        auto *mapItem = new QStandardItem("");
        mapItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        m_entityModel->setItem(row, Col_MapID, mapItem);
    }

    // 自动加载该想定的映射配置
    loadMappingForCurrentScenario();
}

// ==================== 回放控制 ====================

/**
 * @brief 初始化回放引擎
 *
 * 流程：创建 DataFileReader 实例 → 扫描数据文件时间范围 → 连接 NATS →
 * 选中数据文件 → 自动解析对应想定 → 初始化引擎 → 进入 Ready 状态。
 */
void DataReplayWidget::onInit()
{
    // ====== ① 获取选中的数据文件，向上查找对应想定 ======
    QModelIndexList selected = ui->treeView_Scenario->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请在左侧文件管理中选择一个数据文件"));
        return;
    }

    // 仅支持单选，多选时拒绝
    if (selected.size() > 1) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("仅支持回放单个数据文件，请只选择一个文件"));
        return;
    }

    QModelIndex index = toSourceIndex(selected.first());
    QString nodeType = index.data(Qt::UserRole + 1).toString();
    QString nodePath = index.data(Qt::UserRole).toString();

    if (nodeType != "datafile") {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请选择一个数据文件（.json）"));
        return;
    }

    // 选中数据文件 → 向上找想定目录和 XML
    // 树结构: 想定文件夹 → "回放数据" → 数据文件
    QModelIndex dataFolderIdx = index.parent();
    QModelIndex scenarioIdx = dataFolderIdx.parent();
    QString scenarioDir = scenarioIdx.data(Qt::UserRole).toString();

    QDir dir(scenarioDir);
    QStringList xmlFiles = dir.entryList(QStringList() << "*.xml", QDir::Files);
    if (xmlFiles.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("错误"),
                             QStringLiteral("想定目录下未找到 XML 文件"));
        return;
    }
    QString scenarioXmlPath = dir.absoluteFilePath(xmlFiles.first());

    // ====== ② 解析想定 XML ======
    if (!m_server->loadScenario(scenarioXmlPath)) {
        m_server->log("ERROR",
            QStringLiteral("想定加载失败: %1").arg(scenarioXmlPath));
        QMessageBox::critical(this, QStringLiteral("错误"),
                              QStringLiteral("想定加载失败，请检查文件格式"));
        return;
    }

    const Scenario *scenario = m_server->currentScenario();
    if (!scenario) return;

    m_server->log("INFO",
        QStringLiteral("想定已加载 - %1 (实体:%2)")
            .arg(scenario->name)
            .arg(scenario->entities.size()));

    // ====== ③ 更新实体表格 ======
    populateEntityTable(scenario);

    // ====== ④ 初始化回放引擎（仅回放选中的数据文件） ======
    m_server->log("INFO", "正在初始化回放引擎...");
    ui->btn_Init->setEnabled(false);

    m_server->log("INFO",
        QStringLiteral("将回放数据文件: %1").arg(nodePath));

    // 必须在 initReplay 之前设置映射，确保 Init 记录发送时已应用替换
    m_server->setEntityIdMapping(m_entityIdMapping);
    bool success = m_server->initReplay(scenario, nodePath);
    if (success) {
        m_isInitialized = true;
        m_selectedDataFile = nodePath;
        m_server->log("INFO", "回放引擎初始化完成");
    } else {
        m_server->log("ERROR", "回放引擎初始化失败");
    }

    // ====== ⑤ 更新界面 ======
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

    m_server->log("INFO", "正在开始回放...");
    m_server->startReplay();
    // 按钮状态由 stateChanged 信号驱动更新
}

void DataReplayWidget::onPause()
{
    m_server->pauseReplay();
}

void DataReplayWidget::onResume()
{
    m_server->resumeReplay();
}

void DataReplayWidget::onStop()
{
    m_server->stopReplay();
}

// ==================== 树形列表选中 ====================

void DataReplayWidget::onTreeSelectionChanged()
{
    QModelIndexList selected = ui->treeView_Scenario->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        return;
    }

    QModelIndex index = toSourceIndex(selected.first());
    QString nodeType = index.data(Qt::UserRole + 1).toString();
    QString nodePath = index.data(Qt::UserRole).toString();

    if (nodeType == "datafile") {
        // 选中数据文件 → 仅回放该文件
        m_selectedDataFile = nodePath;
    } else {
        // 其他节点（文件夹、XML 等）→ 清除选中回放文件
        m_selectedDataFile.clear();
    }
}

// ==================== 倍速控制 ====================

void DataReplayWidget::onSpeedChanged()
{
    QString text = ui->edit_Speed->text();
    bool ok = false;
    int speed = text.toInt(&ok);

    if (!ok || speed < 1 || speed > 100) {
        ui->edit_Speed->setText(QString::number(m_server->speed()));
        return;
    }

    m_server->setSpeed(speed);
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

void DataReplayWidget::onEngineStateChanged(Server_DataReplay::EngineState state)
{
    updateButtonStates();
    updateStatusBar();

    // 如果是 Stopped 状态，重置初始化标志
    if (state == Server_DataReplay::Stopped || state == Server_DataReplay::Idle) {
        m_isInitialized = false;
    }

    // 如果从 Playing 变为其他状态，更新 NATS 状态显示
    if (state == Server_DataReplay::Ready || state == Server_DataReplay::Stopped) {
        ui->label_StatusNATS->setText(QStringLiteral("NATS: 已连接"));
    }
}

void DataReplayWidget::onReplayFinished()
{
    m_server->log("INFO", "回放已全部完成");
    ui->label_StatusNATS->setText(QStringLiteral("NATS: 已连接"));
    m_isInitialized = false;
}

void DataReplayWidget::onError(const QString &error)
{
    m_server->log("ERROR", error);
    QMessageBox::warning(this, QStringLiteral("错误"), error);
}

void DataReplayWidget::onNatsConnected(bool connected)
{
    if (connected) {
        ui->label_StatusNATS->setText(QStringLiteral("NATS: 已连接"));
        m_server->log("INFO", "NATS 连接成功");
    } else {
        ui->label_StatusNATS->setText(QStringLiteral("NATS: 未连接"));
        m_server->log("WARN", "NATS 连接断开");
    }
}

// ==================== 辅助函数 ====================

void DataReplayWidget::updateButtonStates()
{
    Server_DataReplay::EngineState state = m_server->state();
    const Scenario *scenario = m_server->currentScenario();

    bool hasScenario = (scenario != nullptr);

    // 根据状态表设置按钮
    //          Idle    Ready   Playing  Paused  Stopped
    // 初始化    ✓       ✓       ✗       ✗       ✓
    // 开始      ✗       ✓       ✗       ✗       ✗
    // 暂停      ✗       ✗       ✓       ✗       ✗
    // 继续      ✗       ✗       ✗       ✓       ✗
    // 停止      ✗       ✓       ✓       ✓       ✗

    switch (state) {
    case Server_DataReplay::Idle:
        ui->btn_Init->setEnabled(true);   // Idle 时始终可初始化（选中节点后一键加载+初始化）
        ui->btn_Start->setEnabled(false);
        ui->btn_Pause->setEnabled(false);
        ui->btn_Resume->setEnabled(false);
        ui->btn_Stop->setEnabled(false);
        break;

    case Server_DataReplay::Ready:
        ui->btn_Init->setEnabled(hasScenario);
        ui->btn_Start->setEnabled(true);
        ui->btn_Pause->setEnabled(false);
        ui->btn_Resume->setEnabled(false);
        ui->btn_Stop->setEnabled(true);
        break;

    case Server_DataReplay::Playing:
        ui->btn_Init->setEnabled(false);
        ui->btn_Start->setEnabled(false);
        ui->btn_Pause->setEnabled(true);
        ui->btn_Resume->setEnabled(false);
        ui->btn_Stop->setEnabled(true);
        break;

    case Server_DataReplay::Paused:
        ui->btn_Init->setEnabled(false);
        ui->btn_Start->setEnabled(false);
        ui->btn_Pause->setEnabled(false);
        ui->btn_Resume->setEnabled(true);
        ui->btn_Stop->setEnabled(true);
        break;

    case Server_DataReplay::Stopped:
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
    Server_DataReplay::EngineState state = m_server->state();
    const Scenario *scenario = m_server->currentScenario();

    // 想定名称
    if (scenario) {
        ui->label_StatusScenario->setText(
            QStringLiteral("想定: %1").arg(scenario->name));
    } else {
        ui->label_StatusScenario->setText(QStringLiteral("想定: --"));
    }

    // 进度
    double progress = m_server->overallProgress();
    ui->label_StatusProgress->setText(
        QStringLiteral("进度: %1%").arg(qRound(progress)));

    // 状态名称
    QString stateStr;
    switch (state) {
    case Server_DataReplay::Idle:    stateStr = "Idle";    break;
    case Server_DataReplay::Ready:   stateStr = "Ready";   break;
    case Server_DataReplay::Playing: stateStr = "Playing"; break;
    case Server_DataReplay::Paused:  stateStr = "Paused";  break;
    case Server_DataReplay::Stopped: stateStr = "Stopped"; break;
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

// ==================== 搜索功能 ====================

void DataReplayWidget::onSearchFile()
{
    const QString keyword = ui->edit_SearchFile->text().trimmed();
    if (keyword.isEmpty()) {
        // 空关键词：清除过滤，显示全部，折叠所有节点
        m_proxyModel->setFilterRegularExpression(QRegularExpression());
        ui->treeView_Scenario->collapseAll();
    } else {
        // 非空关键词：设置大小写不敏感的模糊匹配正则，展开匹配节点
        m_proxyModel->setFilterRegularExpression(
            QRegularExpression(QRegularExpression::escape(keyword),
                               QRegularExpression::CaseInsensitiveOption));
        QTimer::singleShot(0, this, [this]() {
            ui->treeView_Scenario->expandAll();
        });
    }
}

void DataReplayWidget::onSearchEntity()
{
    const QString keyword = ui->lineEdit_SearchEntity->text().trimmed();
    if (m_entityProxyModel) {
        // setFilterFixedString：空字符串 → 显示全部，非空 → 模糊匹配
        m_entityProxyModel->setFilterFixedString(keyword);
    }
}

// ==================== 映射配置 ====================

void DataReplayWidget::loadMappingForCurrentScenario()
{
    const Scenario *scenario = m_server->currentScenario();
    if (!scenario)
        return;

    // 从想定文件路径反推想定目录
    QFileInfo fi(scenario->filePath);
    QString scenarioDir = fi.absolutePath();

    // 加载映射表
    m_entityIdMapping = m_server->loadEntityIdMapping(scenarioDir);

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

    m_server->log("INFO",
        QStringLiteral("已加载实体ID映射，共 %1 条").arg(m_entityIdMapping.size()));
}

void DataReplayWidget::onSaveMapping()
{
    const Scenario *scenario = m_server->currentScenario();
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

    if (!m_server->saveEntityIdMapping(scenarioDir, mappings)) {
        QMessageBox::warning(this, QStringLiteral("错误"),
                             QStringLiteral("保存映射失败"));
        return;
    }

    // 更新内存中的映射表
    m_entityIdMapping = mappings;

    m_server->log("INFO",
        QStringLiteral("映射已保存，共 %1 条").arg(mappings.size()));

    QMessageBox::information(this, QStringLiteral("提示"),
                             QStringLiteral("映射保存成功，共 %1 条").arg(mappings.size()));
}

// ==================== 代理模型索引映射 ====================

QModelIndex DataReplayWidget::toSourceIndex(const QModelIndex &viewIndex) const
{
    if (!m_proxyModel || !viewIndex.isValid())
        return viewIndex;
    return m_proxyModel->mapToSource(viewIndex);
}

// ==================== 右键菜单 ====================

void DataReplayWidget::onTreeContextMenu(const QPoint &pos)
{
    QModelIndex proxyIndex = ui->treeView_Scenario->indexAt(pos);
    if (!proxyIndex.isValid())
        return;

    QModelIndex sourceIndex = toSourceIndex(proxyIndex);
    QString nodeType = sourceIndex.data(Qt::UserRole + 1).toString();

    QMenu menu;

    // 想定文件夹节点：额外支持「添加数据文件」和「重新扫描」
    if (nodeType == "scenariodir") {
        QAction *addFilesAction = menu.addAction(QStringLiteral("添加数据文件..."));
        connect(addFilesAction, &QAction::triggered, this, [this, sourceIndex]() {
            onAddDataFiles(sourceIndex);
        });

        QAction *refreshAction = menu.addAction(QStringLiteral("重新扫描"));
        connect(refreshAction, &QAction::triggered, this, [this, sourceIndex]() {
            onRefreshDataFiles(sourceIndex);
        });

        menu.addSeparator();
    }

    // 想定文件（XML）和数据文件节点支持「编辑描述」
    if (nodeType == "xmlfile" || nodeType == "datafile") {
        QAction *editDescAction = menu.addAction(QStringLiteral("编辑描述..."));
        editDescAction->setData(QVariant::fromValue(sourceIndex));
        connect(editDescAction, &QAction::triggered, this, &DataReplayWidget::onEditDescription);
        menu.addSeparator();
    }

    // 所有节点都支持重命名和删除
    QAction *renameAction = menu.addAction(QStringLiteral("重命名"));
    QAction *deleteAction = menu.addAction(QStringLiteral("删除"));

    // 分发重命名：文件夹 vs 文件
    connect(renameAction, &QAction::triggered, this, [this, sourceIndex, nodeType]() {
        if (nodeType == "scenariodir" || nodeType == "datafolder")
            onRenameScenario(sourceIndex);
        else if (nodeType == "xmlfile" || nodeType == "datafile")
            onRenameDataFile(sourceIndex);
    });

    // 分发删除：文件夹（递归删除） vs 文件
    connect(deleteAction, &QAction::triggered, this, [this, sourceIndex, nodeType]() {
        if (nodeType == "scenariodir" || nodeType == "datafolder")
            onDeleteScenario(sourceIndex);
        else if (nodeType == "xmlfile" || nodeType == "datafile")
            onDeleteDataFile(sourceIndex);
    });

    menu.exec(ui->treeView_Scenario->viewport()->mapToGlobal(pos));
}

// ==================== 重命名 ====================

void DataReplayWidget::onRenameScenario(const QModelIndex &sourceIndex)
{
    QStandardItem *item = m_treeModel->itemFromIndex(sourceIndex);
    if (!item) return;

    QString oldName = item->text();
    QString oldDir = item->data(Qt::UserRole).toString();

    // 弹出输入对话框
    bool ok = false;
    QString newName = QInputDialog::getText(
        this,
        QStringLiteral("重命名文件夹"),
        QStringLiteral("请输入新的名称："),
        QLineEdit::Normal,
        oldName,
        &ok);

    if (!ok || newName.isEmpty() || newName == oldName)
        return;

    QRegularExpression illegalChars(R"([\/\\\:\*\?\"\<\>\|])");
    if (illegalChars.match(newName).hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("无效名称"),
                             QStringLiteral("名称不能包含以下字符: / \\ : * ? \" < > |"));
        return;
    }

    // 执行重命名
    if (!m_server->renameScenario(oldDir, newName)) {
        QMessageBox::critical(this, QStringLiteral("重命名失败"),
                              QStringLiteral("无法重命名文件夹，可能正在被占用。"));
        return;
    }

    m_server->log("INFO",
        QStringLiteral("文件夹已重命名: %1 → %2").arg(oldName, newName));

    // 刷新树以反映更改
    refreshScenarioTree();
    updateStatusBar();
}

void DataReplayWidget::onRenameDataFile(const QModelIndex &sourceIndex)
{
    QStandardItem *item = m_treeModel->itemFromIndex(sourceIndex);
    if (!item) return;

    QString oldFilePath = item->data(Qt::UserRole).toString();
    QString oldFileName = item->text();  // 树节点显示文本即为文件名，避免 QFileInfo 跨平台分隔符问题

    // 分离文件名和后缀，仅允许修改文件名部分
    QFileInfo fi(oldFileName);
    QString suffix = fi.completeSuffix();  // "json" / "tar.gz"（不含点）
    QString baseName = fi.baseName();      // 不含后缀的文件名

    bool ok = false;
    QString newBaseName = QInputDialog::getText(
        this,
        QStringLiteral("重命名数据文件"),
        QStringLiteral("请输入新的文件名（后缀不可修改）："),
        QLineEdit::Normal,
        baseName,
        &ok);

    if (!ok || newBaseName.isEmpty() || newBaseName == baseName)
        return;

    QRegularExpression illegalChars(R"([\/\\\:\*\?\"\<\>\|])");
    if (illegalChars.match(newBaseName).hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("无效名称"),
                             QStringLiteral("文件名不能包含以下字符: / \\ : * ? \" < > |"));
        return;
    }

    // 重新拼接完整文件名（强制保留原后缀）
    QString newName = suffix.isEmpty() ? newBaseName : (newBaseName + "." + suffix);

    if (!m_server->renameDataFile(oldFilePath, newName)) {
        QMessageBox::critical(this, QStringLiteral("重命名失败"),
                              QStringLiteral("无法重命名文件，可能正在被占用。"));
        return;
    }

    m_server->log("INFO",
        QStringLiteral("文件已重命名: %1 → %2").arg(oldFileName, newName));

    // 刷新树以反映更改
    refreshScenarioTree();
}

// ==================== 删除 ====================

void DataReplayWidget::onDeleteScenario(const QModelIndex &sourceIndex)
{
    QStandardItem *item = m_treeModel->itemFromIndex(sourceIndex);
    if (!item) return;

    QString name = item->text();
    int newlinePos = name.indexOf('\n');
    if (newlinePos >= 0)
        name = name.left(newlinePos).trimmed();

    QString dirPath = item->data(Qt::UserRole).toString();

    // 确认对话框
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        QStringLiteral("确认删除"),
        QStringLiteral("确定要永久删除想定「%1」及其所有数据文件吗？\n此操作不可恢复！")
            .arg(name),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (reply != QMessageBox::Yes)
        return;

    // 如果删除的是当前加载的想定，先停止引擎
    if (m_server->currentScenario()) {
        QFileInfo loadedFi(m_server->currentScenario()->filePath);
        if (loadedFi.absolutePath() == dirPath) {
            m_server->stopReplay();
        }
    }

    // 执行删除
    if (!m_server->deleteScenario(dirPath)) {
        QMessageBox::critical(this, QStringLiteral("删除失败"),
                              QStringLiteral("无法删除文件夹，可能正在被占用。"));
        return;
    }

    // 如果当前想定已被删除，清空 UI
    if (!m_server->currentScenario()) {
        m_entityModel->removeRows(0, m_entityModel->rowCount());
        m_entityIdMapping.clear();
        m_isInitialized = false;
        m_selectedDataFile.clear();
        updateButtonStates();
        updateStatusBar();
    }

    m_server->log("INFO",
        QStringLiteral("文件夹已删除: %1").arg(name));

    // 刷新树以反映更改
    refreshScenarioTree();
}

void DataReplayWidget::onDeleteDataFile(const QModelIndex &sourceIndex)
{
    QStandardItem *item = m_treeModel->itemFromIndex(sourceIndex);
    if (!item) return;

    QString filePath = item->data(Qt::UserRole).toString();
    QString fileName = item->text();  // 树节点显示文本即为文件名，避免 QFileInfo 跨平台分隔符问题

    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        QStringLiteral("确认删除"),
        QStringLiteral("确定要永久删除「%1」吗？\n此操作不可恢复！")
            .arg(fileName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (reply != QMessageBox::Yes)
        return;

    if (!m_server->deleteDataFile(filePath)) {
        QMessageBox::critical(this, QStringLiteral("删除失败"),
                              QStringLiteral("无法删除文件，可能正在被占用。"));
        return;
    }

    // 如果删除的是当前选中的数据文件，清除选中
    if (m_selectedDataFile == filePath) {
        m_selectedDataFile.clear();
    }

    m_server->log("INFO",
        QStringLiteral("文件已删除: %1").arg(filePath));

    // 刷新树以反映更改
    refreshScenarioTree();
}

// ==================== 追加数据文件 ====================

void DataReplayWidget::onAddDataFiles(const QModelIndex &sourceIndex)
{
    QStandardItem *item = m_treeModel->itemFromIndex(sourceIndex);
    if (!item) return;

    QString scenarioDir = item->data(Qt::UserRole).toString();
    QString scenarioName = item->text();

    // 弹出文件选择对话框（支持多选 JSON 和 TXT）
    QStringList selectedFiles = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("选择数据文件 - %1").arg(scenarioName),
        QString(),
        QStringLiteral("数据文件 (*.json *.txt);;所有文件 (*.*)"));

    if (selectedFiles.isEmpty())
        return;

    // 调用后端复制文件
    int count = m_server->addDataFiles(scenarioDir, selectedFiles);
    if (count < 0) {
        QMessageBox::critical(this, QStringLiteral("错误"),
                             QStringLiteral("无法创建回放数据目录"));
        return;
    }

    m_server->log("INFO",
        QStringLiteral("已添加 %1/%2 个数据文件到「%3」")
            .arg(count).arg(selectedFiles.size()).arg(scenarioName));

    if (count > 0) {
        refreshScenarioTree();
    }
}

void DataReplayWidget::onRefreshDataFiles(const QModelIndex &sourceIndex)
{
    QStandardItem *item = m_treeModel->itemFromIndex(sourceIndex);
    if (!item) return;

    QString scenarioDir = item->data(Qt::UserRole).toString();
    QString scenarioName = item->text();

    QStringList files = m_server->refreshDataFiles(scenarioDir);

    m_server->log("INFO",
        QStringLiteral("「%1」重新扫描完成，共 %2 个数据文件")
            .arg(scenarioName).arg(files.size()));

    refreshScenarioTree();
}

// ==================== 编辑描述 ====================

void DataReplayWidget::onEditDescription()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;

    QModelIndex sourceIndex = action->data().value<QModelIndex>();
    if (!sourceIndex.isValid())
        return;

    // 按节点类型分发到想定文件/数据文件描述编辑
    QString nodeType = sourceIndex.data(Qt::UserRole + 1).toString();
    if (nodeType == "xmlfile") {
        editScenarioDescription(sourceIndex);
    } else if (nodeType == "datafile") {
        editDataFileDescription(sourceIndex);
    }
}

void DataReplayWidget::editScenarioDescription(const QModelIndex &sourceIndex)
{
    QStandardItem *item = m_treeModel->itemFromIndex(sourceIndex);
    if (!item)
        return;

    // xmlfile 节点的 UserRole 是想定 XML 文件路径，想定目录为其所在目录
    QString xmlPath = item->data(Qt::UserRole).toString();
    QFileInfo fi(xmlPath);
    QString scenarioDir = fi.absolutePath();
    QString scenarioName = fi.fileName();

    // 读取当前想定描述
    QString currentDesc = m_server->loadScenarioDescription(scenarioDir);

    DescriptionDialog dlg(scenarioName, currentDesc, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    // 保存：想定描述 + 现有数据文件描述（增量合并，仅写 description.json）
    QMap<QString, QString> dataFileDescs = m_server->loadDataFileDescriptions(scenarioDir);
    if (!m_server->saveDescription(scenarioDir, dlg.description(), dataFileDescs)) {
        QMessageBox::warning(this, QStringLiteral("错误"),
                             QStringLiteral("保存想定描述失败"));
        return;
    }

    m_server->log("INFO",
        QStringLiteral("想定文件描述已保存: %1").arg(scenarioName));
    refreshScenarioTree();
}

void DataReplayWidget::editDataFileDescription(const QModelIndex &sourceIndex)
{
    QStandardItem *item = m_treeModel->itemFromIndex(sourceIndex);
    if (!item)
        return;

    QString fileName = item->text();

    // 由树结构向上找想定目录：数据文件 → 回放数据 → 想定文件夹
    QModelIndex dataFolderIdx = sourceIndex.parent();
    QModelIndex scenarioIdx = dataFolderIdx.parent();
    QString scenarioDir = scenarioIdx.data(Qt::UserRole).toString();
    if (scenarioDir.isEmpty())
        return;

    // 读取当前数据文件描述（文件名 → 描述）
    QMap<QString, QString> dataFileDescs = m_server->loadDataFileDescriptions(scenarioDir);
    QString currentDesc = dataFileDescs.value(fileName);

    DescriptionDialog dlg(fileName, currentDesc, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    // 更新该文件描述后整体保存（增量合并，仅写 description.json）
    dataFileDescs[fileName] = dlg.description();
    QString scenarioDesc = m_server->loadScenarioDescription(scenarioDir);
    if (!m_server->saveDescription(scenarioDir, scenarioDesc, dataFileDescs)) {
        QMessageBox::warning(this, QStringLiteral("错误"),
                             QStringLiteral("保存数据文件描述失败"));
        return;
    }

    m_server->log("INFO",
        QStringLiteral("数据文件描述已保存: %1").arg(fileName));
    refreshScenarioTree();
}

// ==================== 导入想定 ====================

void DataReplayWidget::onImportScenario()
{
    // ① 选择 XML 文件
    QString xmlPath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择想定 XML 文件"),
        QString(),
        QStringLiteral("XML 想定文件 (*.xml);;所有文件 (*.*)"));

    if (xmlPath.isEmpty())
        return;

    // ② 预览导入（解析 XML，检查目标是否存在）
    ImportPreview preview = m_server->previewImport(xmlPath);
    if (preview.sceName.isEmpty()) {
        QMessageBox::critical(this, QStringLiteral("导入失败"),
                             QStringLiteral("无法解析想定 XML 文件，请检查文件格式。"));
        return;
    }

    // ③ 弹出确认框
    ImportDialog dlg(preview, this);
    if (dlg.exec() != QDialog::Accepted) {
        m_server->log("INFO",
            QStringLiteral("用户取消了导入: %1").arg(preview.targetDirName));
        return;
    }

    // ④ 执行导入（仅复制 XML，创建空的 回放数据/ 目录）
    bool success = m_server->importScenario(xmlPath, preview.targetDirName);
    if (!success) {
        QMessageBox::critical(this, QStringLiteral("导入失败"),
                             QStringLiteral("导入过程中发生错误，请检查磁盘空间和权限。"));
        return;
    }

    m_server->log("INFO",
        QStringLiteral("想定导入成功: %1").arg(preview.targetDirName));

    // ⑤ 刷新界面
    refreshScenarioTree();
}

// ==================== 外部 NATS 指令控制 ====================
//
// 通过 NATS 接收外部控制指令，驱动回放操作：
//   {"topic":"INIT","filename":"xxx.json"}  → 查找并选中数据文件，初始化
//   {"topic":"START"}                       → 开始回放
//   {"topic":"PAUSE"}                       → 暂停回放
//   {"topic":"RESUME"}                      → 继续回放
//   {"topic":"STOP"}                        → 停止回放
// 本函数由 Facade 的 natsMessageReceived 信号调用，已在主线程执行。

void DataReplayWidget::onNATSMessage(const QString &topicName, const QByteArray &data)
{
    // 解析 JSON（data 为长度明确的二进制数据，不会越界读取）
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "onNATSMessage: JSON 解析失败, error =" << parseError.errorString();
        return;
    }

    const QJsonObject obj = doc.object();
    const QString fileName = obj.value(QLatin1String("filename")).toString();
    const QString cmd      = obj.value(QLatin1String("topic")).toString();

    if (cmd.isEmpty()) {
        qWarning() << "onNATSMessage: 缺少指令字段 topic";
        return;
    }

    if (cmd == QLatin1String("INIT")) {
        handleInitCommand(fileName);
    } else if (cmd == QLatin1String("START")) {
        onStart();
    } else if (cmd == QLatin1String("PAUSE")) {
        onPause();
    } else if (cmd == QLatin1String("RESUME")) {
        onResume();
    } else if (cmd == QLatin1String("STOP")) {
        onStop();
    } else {
        qWarning() << "onNATSMessage: 未知指令" << cmd;
    }
}

void DataReplayWidget::handleInitCommand(const QString &fileName)
{
    QStandardItem *root = m_treeModel->invisibleRootItem();

    // 遍历所有想定文件夹 → "回放数据"文件夹 → 匹配文件名的数据文件。
    // 按节点类型标记（UserRole+1）查找，不硬编码树中的位置。
    for (int s = 0; s < root->rowCount(); ++s) {
        QStandardItem *scenarioItem = root->child(s);
        if (!scenarioItem
            || scenarioItem->data(Qt::UserRole + 1).toString() != QStringLiteral("scenariodir"))
            continue;

        for (int d = 0; d < scenarioItem->rowCount(); ++d) {
            QStandardItem *folderItem = scenarioItem->child(d);
            if (!folderItem
                || folderItem->data(Qt::UserRole + 1).toString() != QStringLiteral("datafolder"))
                continue;

            for (int f = 0; f < folderItem->rowCount(); ++f) {
                QStandardItem *fileItem = folderItem->child(f);
                if (!fileItem || fileItem->text() != fileName)
                    continue;

                // 找到目标数据文件：源索引 → proxy 索引（view 只认 proxy），唯一选中
                QModelIndex proxyIdx = m_proxyModel->mapFromSource(fileItem->index());
                QItemSelectionModel *selModel = ui->treeView_Scenario->selectionModel();
                selModel->select(proxyIdx,
                                 QItemSelectionModel::ClearAndSelect
                                 | QItemSelectionModel::Current);
                ui->treeView_Scenario->setCurrentIndex(proxyIdx);
                ui->treeView_Scenario->scrollTo(proxyIdx);

                // 复用现有初始化流程（加载想定 + 填充实体表 + 设置映射 + initReplay）
                onInit();
                return;
            }
        }
    }

    qWarning() << "onNATSMessage INIT: 未找到数据文件" << fileName;
}
