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
#include "ScenarioFilterProxyModel.h"
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
#include <QDir>
#include <QTimer>
#include <QMenu>
#include <QInputDialog>
#include <QRegularExpression>

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

    // ==================== 创建后端模块 ====================
    m_scenarioMgr = new ScenarioMgr(this);
    m_replayEngine = new ReplayEngine(this);

    // ==================== 启动时自动连接 NATS ====================
    // 在后台异步发起 NATS 连接（内部含指数退避重连机制），不阻塞 UI 启动
    Communication_NATS::getInstance().initNATSConnect();

    // ==================== 文件管理搜索框 ====================
    // 代码创建搜索框插入到"文件管理"面板顶部，不修改 .ui 文件
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索文件..."));
    m_searchEdit->setClearButtonEnabled(true);
    ui->verticalLayout_FileMgr->insertWidget(0, m_searchEdit);

    // 启用右键菜单
    ui->treeView_Scenario->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->treeView_Scenario, &QTreeView::customContextMenuRequested,
            this, &DataReplayWidget::onTreeContextMenu);

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

    // -- NATS 连接状态信号 --
    connect(&Communication_NATS::getInstance(), &Communication_NATS::natsConnected,
            this, &DataReplayWidget::onNatsConnected);

    // ==================== 初始界面状态 ====================
    ui->label_StatusNATS->setText(QStringLiteral("NATS: 连接中..."));
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

    // 创建过滤代理模型，插入在源 model 和 view 之间
    m_proxyModel = new ScenarioFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_treeModel);
    ui->treeView_Scenario->setModel(m_proxyModel);

    // 搜索框输入 → 代理过滤 → 自动展开所有匹配节点
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_proxyModel->setFilterFixedString(text);
        // 延迟展开：确保代理模型已完成过滤后再展开所有可见节点
        QTimer::singleShot(0, this, [this]() {
            ui->treeView_Scenario->expandAll();
        });
    });
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

    ui->tableView_Entities->setModel(m_entityModel);

    // 设置列宽
    ui->tableView_Entities->setColumnWidth(Col_ID, 80);
    ui->tableView_Entities->setColumnWidth(Col_Name, 120);
    ui->tableView_Entities->setColumnWidth(Col_MapID, 100);
}

// ==================== 想定扫描与加载 ====================

void DataReplayWidget::refreshScenarioTree()
{
    // 启动时递归扫描 dataFiles/ 目录，展示完整的目录树结构
    // 仅列出文件名，不做文件信息估算（轻量扫描）
    m_treeModel->clear();
    m_treeModel->setHorizontalHeaderLabels(QStringList() << QStringLiteral("文件管理"));

    QString rootPath = m_scenarioMgr->dataFilesRoot();
    QDir rootDir(rootPath);

    if (!rootDir.exists()) {
        LogService::instance().log("WARN", "dataFiles 目录不存在: " + rootPath);
        return;
    }

    // 遍历 dataFiles/ 下的所有子目录（每个子目录 = 一个想定）
    const QFileInfoList scenarioDirs = rootDir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QFileInfo &dirInfo : scenarioDirs) {
        QString scenarioDirPath = dirInfo.absoluteFilePath();
        QString scenarioName = dirInfo.fileName();

        // ---- 想定文件夹节点 ----
        auto *scenarioItem = new QStandardItem(scenarioName);
        scenarioItem->setData(scenarioDirPath, Qt::UserRole);
        scenarioItem->setData("scenariodir", Qt::UserRole + 1);
        scenarioItem->setToolTip(QStringLiteral("想定目录: %1").arg(scenarioDirPath));
        m_treeModel->invisibleRootItem()->appendRow(scenarioItem);

        QDir scenarioDir(scenarioDirPath);

        // ---- XML 文件子节点 ----
        const QStringList xmlFiles = scenarioDir.entryList(
            QStringList() << "*.xml", QDir::Files, QDir::Name);
        for (const QString &xmlFile : xmlFiles) {
            QString xmlPath = scenarioDir.absoluteFilePath(xmlFile);
            auto *xmlItem = new QStandardItem(xmlFile);
            xmlItem->setData(xmlPath, Qt::UserRole);
            xmlItem->setData("xmlfile", Qt::UserRole + 1);
            xmlItem->setToolTip(QStringLiteral("想定文件: %1").arg(xmlPath));
            scenarioItem->appendRow(xmlItem);
        }

        // ---- 回放数据文件夹节点 ----
        QString replayDirPath = scenarioDirPath + "/回放数据";
        QDir replayDir(replayDirPath);
        if (replayDir.exists()) {
            auto *dataFolderItem = new QStandardItem(QStringLiteral("回放数据"));
            dataFolderItem->setData(replayDirPath, Qt::UserRole);
            dataFolderItem->setData("datafolder", Qt::UserRole + 1);
            dataFolderItem->setToolTip(QStringLiteral("回放数据目录: %1").arg(replayDirPath));
            scenarioItem->appendRow(dataFolderItem);

            // ---- 数据文件子节点（.txt） ----
            const QStringList txtFiles = replayDir.entryList(
                QStringList() << "*.txt", QDir::Files, QDir::Name);
            for (const QString &txtFile : txtFiles) {
                QString txtPath = replayDir.absoluteFilePath(txtFile);
                auto *fileItem = new QStandardItem(txtFile);
                fileItem->setData(txtPath, Qt::UserRole);
                fileItem->setData("datafile", Qt::UserRole + 1);
                fileItem->setToolTip(QStringLiteral("数据文件: %1").arg(txtPath));
                dataFolderItem->appendRow(fileItem);
            }
        }
    }

    LogService::instance().log("INFO",
        QStringLiteral("文件树已加载，共 %1 个想定目录").arg(scenarioDirs.size()));
}

void DataReplayWidget::onLoadScenario()
{
    // 获取当前选中项（view 返回 proxy index，需映射到源 model）
    QModelIndexList selected = ui->treeView_Scenario->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先在左侧树形列表中选择一个想定"));
        return;
    }

    QModelIndex index = toSourceIndex(selected.first());
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

                // 展开树节点（源索引 → proxy 索引）
                QModelIndex scenarioIdx = item->index();
                ui->treeView_Scenario->expand(m_proxyModel->mapFromSource(scenarioIdx));
                break;
        }
    }

    // ---- 更新实体表格 ----
    m_entityModel->removeRows(0, m_entityModel->rowCount());
    for (const EntityInfo &entity : scenario->entities) {
        int row = m_entityModel->rowCount();
        m_entityModel->insertRow(row);

        // ID/名称 —— 只读不可编辑
        auto *idItem = new QStandardItem(entity.id);
        idItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        m_entityModel->setItem(row, Col_ID, idItem);

        auto *nameItem = new QStandardItem(entity.name);
        nameItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        m_entityModel->setItem(row, Col_Name, nameItem);

        // 映射ID —— 允许双击编辑
        auto *mapItem = new QStandardItem("");
        mapItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        m_entityModel->setItem(row, Col_MapID, mapItem);
    }

    // 加载该想定的映射关系，预填映射值
    loadMappingForCurrentScenario();

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

    QModelIndex index = toSourceIndex(selected.first());
    QString nodeType = index.data(Qt::UserRole + 1).toString();
    QString nodePath = index.data(Qt::UserRole).toString();

    if (nodeType != "datafile") {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请选择一个数据文件（.txt）"));
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
    if (!m_scenarioMgr->loadScenario(scenarioXmlPath)) {
        LogService::instance().log("ERROR",
            QStringLiteral("想定加载失败: %1").arg(scenarioXmlPath));
        QMessageBox::critical(this, QStringLiteral("错误"),
                              QStringLiteral("想定加载失败，请检查文件格式"));
        return;
    }

    const Scenario *scenario = m_scenarioMgr->currentScenario();
    if (!scenario) return;

    LogService::instance().log("INFO",
        QStringLiteral("想定已加载 - %1 (实体:%2)")
            .arg(scenario->name)
            .arg(scenario->entities.size()));

    // ====== ③ 更新实体表格 ======
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

    // 加载实体映射
    loadMappingForCurrentScenario();

    // ====== ④ 初始化回放引擎（仅回放选中的数据文件） ======
    LogService::instance().log("INFO", "正在初始化回放引擎...");
    ui->btn_Init->setEnabled(false);

    QStringList filesToReplay = QStringList() << nodePath;
    LogService::instance().log("INFO",
        QStringLiteral("将回放数据文件: %1").arg(nodePath));

    bool success = m_replayEngine->initialize(scenario, filesToReplay);
    if (success) {
        m_isInitialized = true;
        m_selectedDataFiles = filesToReplay;
        m_replayEngine->setEntityIdMapping(m_entityIdMapping);
        LogService::instance().log("INFO", "回放引擎初始化完成");
    } else {
        LogService::instance().log("ERROR", "回放引擎初始化失败");
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

    QModelIndex index = toSourceIndex(selected.first());
    QString nodeType = index.data(Qt::UserRole + 1).toString();
    QString nodePath = index.data(Qt::UserRole).toString();

    if (nodeType == "datafile") {
        // 选中数据文件 → 仅回放该文件
        m_selectedDataFiles = QStringList() << nodePath;
    } else {
        // 其他节点（文件夹、XML 等）→ 清除选中回放文件
        m_selectedDataFiles.clear();
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

void DataReplayWidget::onNatsConnected(bool connected)
{
    if (connected) {
        ui->label_StatusNATS->setText(QStringLiteral("NATS: 已连接"));
        LogService::instance().log("INFO", "NATS 连接成功");
    } else {
        ui->label_StatusNATS->setText(QStringLiteral("NATS: 未连接"));
        LogService::instance().log("WARN", "NATS 连接断开");
    }
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
        ui->btn_Init->setEnabled(true);   // Idle 时始终可初始化（选中节点后一键加载+初始化）
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
    if (!m_scenarioMgr->renameScenario(oldDir, newName)) {
        QMessageBox::critical(this, QStringLiteral("重命名失败"),
                              QStringLiteral("无法重命名文件夹，可能正在被占用。"));
        return;
    }

    LogService::instance().log("INFO",
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
    QFileInfo fi(oldFilePath);
    QString oldFileName = fi.fileName();

    bool ok = false;
    QString newName = QInputDialog::getText(
        this,
        QStringLiteral("重命名数据文件"),
        QStringLiteral("请输入新的文件名："),
        QLineEdit::Normal,
        oldFileName,
        &ok);

    if (!ok || newName.isEmpty() || newName == oldFileName)
        return;

    QRegularExpression illegalChars(R"([\/\\\:\*\?\"\<\>\|])");
    if (illegalChars.match(newName).hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("无效名称"),
                             QStringLiteral("文件名不能包含以下字符: / \\ : * ? \" < > |"));
        return;
    }

    if (!m_scenarioMgr->renameDataFile(oldFilePath, newName)) {
        QMessageBox::critical(this, QStringLiteral("重命名失败"),
                              QStringLiteral("无法重命名文件，可能正在被占用。"));
        return;
    }

    LogService::instance().log("INFO",
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
    QFileInfo fi(dirPath);
    QString scenarioDir = fi.isFile() ? fi.absolutePath() : dirPath;

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
    if (m_scenarioMgr->currentScenario()) {
        QFileInfo loadedFi(m_scenarioMgr->currentScenario()->filePath);
        if (loadedFi.absolutePath() == scenarioDir) {
            m_replayEngine->stop();
        }
    }

    // 执行删除
    if (!m_scenarioMgr->deleteScenario(scenarioDir)) {
        QMessageBox::critical(this, QStringLiteral("删除失败"),
                              QStringLiteral("无法删除文件夹，可能正在被占用。"));
        return;
    }

    // 如果当前想定已被删除，清空 UI
    if (!m_scenarioMgr->currentScenario()) {
        m_entityModel->removeRows(0, m_entityModel->rowCount());
        m_entityIdMapping.clear();
        m_isInitialized = false;
        m_selectedDataFiles.clear();
        updateButtonStates();
        updateStatusBar();
    }

    LogService::instance().log("INFO",
        QStringLiteral("文件夹已删除: %1").arg(name));

    // 刷新树以反映更改
    refreshScenarioTree();
}

void DataReplayWidget::onDeleteDataFile(const QModelIndex &sourceIndex)
{
    QStandardItem *item = m_treeModel->itemFromIndex(sourceIndex);
    if (!item) return;

    QString filePath = item->data(Qt::UserRole).toString();
    QFileInfo fi(filePath);
    QString fileName = fi.fileName();

    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        QStringLiteral("确认删除"),
        QStringLiteral("确定要永久删除「%1」吗？\n此操作不可恢复！")
            .arg(fileName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (reply != QMessageBox::Yes)
        return;

    if (!m_scenarioMgr->deleteDataFile(filePath)) {
        QMessageBox::critical(this, QStringLiteral("删除失败"),
                              QStringLiteral("无法删除文件，可能正在被占用。"));
        return;
    }

    // 从选中跟踪移除
    m_selectedDataFiles.removeAll(filePath);

    LogService::instance().log("INFO",
        QStringLiteral("文件已删除: %1").arg(filePath));

    // 刷新树以反映更改
    refreshScenarioTree();
}
