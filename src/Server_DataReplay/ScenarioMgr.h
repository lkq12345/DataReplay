#ifndef SCENARIOMGR_H
#define SCENARIOMGR_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QList>
#include "Server_DataReplay_global.h"

/**
 * @brief 仿真实体信息
 */
struct EntityInfo {
    QString id;
    QString name;
    QString type;
    double x = 0.0, y = 0.0, z = 0.0;
    double speed = 0.0, heading = 0.0;
    QString status;
};

/**
 * @brief 想定完整信息
 */
struct Scenario {
    QString name;
    QString description;
    QString filePath;
    QDateTime startTime;
    QDateTime endTime;
    int simStepMs = 100;
    QStringList dataFiles;
    QList<EntityInfo> entities;
};

/**
 * @brief 想定摘要（用于列表展示）
 */
struct ScenarioSummary {
    QString name;
    QString scenarioDir;
    QString filePath;
    int entityCount = 0;
};

/**
 * @brief 数据文件预扫描信息
 */
struct DataFileInfo {
    QString filePath;
    qint64 fileSize = 0;
    quint64 recordCount = 0;
    QDateTime minTime;
    QDateTime maxTime;
};

/**
 * @brief 想定管理类
 *
 * 负责扫描 dataFiles/ 目录下的想定文件夹，
 * 解析 XML 想定文件，关联数据文件，提供想定信息查询。
 */
class SERVER_DATAREPLAY_EXPORT ScenarioMgr : public QObject
{
    Q_OBJECT

public:
    explicit ScenarioMgr(QObject *parent = nullptr);
    ~ScenarioMgr();

    /** @brief 扫描 dataFiles/ 目录，返回所有想定摘要列表 */
    QList<ScenarioSummary> scanScenarios();

    /** @brief 加载指定想定（解析 XML + 关联数据文件） */
    bool loadScenario(const QString &scenarioFilePath);

    /** @brief 获取当前想定完整信息 */
    const Scenario *currentScenario() const;

    /** @brief 获取指定数据文件的预扫描信息 */
    DataFileInfo getDataFileInfo(const QString &filePath);

    /** @brief 获取 dataFiles 根目录路径 */
    QString dataFilesRoot() const;

signals:
    void scenarioLoaded(const Scenario &scenario);
    void scenarioUnloaded();

private:
    /** @brief 解析想定 XML 文件 */
    bool parseScenarioXml(const QString &filePath, Scenario &scenario);

    /** @brief 关联想定目录下的数据文件 */
    QStringList findDataFiles(const QString &scenarioDir);

    /** @brief 估算数据文件的基本信息（不加载全文） */
    void estimateDataFileInfo(const QString &filePath, quint64 &recordCount,
                              QDateTime &minTime, QDateTime &maxTime);

    Scenario *m_currentScenario = nullptr;
};

#endif // SCENARIOMGR_H
