/**
 * @file DataReplayWidget.h
 * @brief 数据回放主界面的头文件
 *
 * 定义主窗口类，负责 UI 展示和用户交互。
 * 通过调用后端模块（ScenarioMgr、ReplayEngine）实现业务逻辑。
 */

#ifndef DATAREPLAYWIDGET_H
#define DATAREPLAYWIDGET_H

#include <QWidget>
#include <QStandardItemModel>
#include <QIntValidator>
#include <QMap>

#include "ReplayEngine.h"

QT_BEGIN_NAMESPACE
namespace Ui { class DataReplayWidget; }
QT_END_NAMESPACE

// 前置声明后端类
class ScenarioMgr;
struct Scenario;

/**
 * @brief 数据回放主界面
 *
 * 负责 UI 展示、用户交互，通过调用后端模块实现业务逻辑。
 * UI 布局主要在 .ui 文件中定义，本类负责信号/槽连接和界面联动。
 */
class DataReplayWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DataReplayWidget(QWidget *parent = nullptr);
    ~DataReplayWidget();

protected:
    /** @brief 窗口关闭事件：停止回放 */
    void closeEvent(QCloseEvent *event) override;

private slots:
    /** @brief 加载想定 */
    void onLoadScenario();

    /** @brief 初始化回放 */
    void onInit();

    /** @brief 开始回放 */
    void onStart();

    /** @brief 暂停回放 */
    void onPause();

    /** @brief 继续回放 */
    void onResume();

    /** @brief 停止回放 */
    void onStop();

    /** @brief 保存实体ID映射 */
    void onSaveMapping();

    /** @brief 树形列表选中项变化 */
    void onTreeSelectionChanged();

    /** @brief 倍速输入变化 */
    void onSpeedChanged();

    /** @brief 追加日志 */
    void appendLog(const QString &message);

    /** @brief 更新仿真时间显示 */
    void onSimTimeChanged(const QDateTime &simTime);

    /** @brief 更新进度显示 */
    void onProgressChanged(double percent);

    /** @brief 更新引擎状态 */
    void onEngineStateChanged(ReplayEngine::State state);

    /** @brief 回放完成 */
    void onReplayFinished();

    /** @brief 错误处理 */
    void onError(const QString &error);

private:
    /** @brief 初始化树形列表模型 */
    void initTreeModel();

    /** @brief 初始化实体表格模型 */
    void initEntityTable();

    /** @brief 从当前想定目录加载映射并填充映射列 */
    void loadMappingForCurrentScenario();

    /** @brief 扫描想定并更新树形列表 */
    void refreshScenarioTree();

    /** @brief 更新按钮状态（根据当前引擎状态） */
    void updateButtonStates();

    /** @brief 更新状态栏 */
    void updateStatusBar();

    /** @brief 设置仿真时间标签 */
    void setSimTimeLabel(const QDateTime &time);

    Ui::DataReplayWidget *ui;

    // 后端模块
    ScenarioMgr    *m_scenarioMgr = nullptr;
    ReplayEngine   *m_replayEngine = nullptr;

    // 模型
    QStandardItemModel *m_treeModel = nullptr;
    QStandardItemModel *m_entityModel = nullptr;

    // 校验器
    QIntValidator *m_speedValidator = nullptr;

    // 状态
    bool m_isInitialized = false;
    QStringList m_selectedDataFiles;   //!< 当前选中的数据文件列表（可多选）

    // 映射
    QMap<QString, QString> m_entityIdMapping;   //!< 实体ID映射表（当前值 → 映射值）
};

#endif // DATAREPLAYWIDGET_H
