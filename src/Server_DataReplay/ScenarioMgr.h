#ifndef SCENARIOMGR_H
#define SCENARIOMGR_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QList>
#include <QMap>
#include "Server_DataReplay_global.h"

/**
 * @brief 仿真实体信息
 *
 * 描述想定中的一个仿真对象（舰船、飞机、汽车等），
 * 仅保留标识和名称用于实体映射配置。
 * 数据来源：XML 中 <Entities> 区域内的 <Entity> 元素。
 */
struct EntityInfo {
    QString id;             //!< 实体唯一标识，例如 "1001"（来自 Entity 元素的 ID 属性）
    QString name;           //!< 实体显示名称，例如 "东方之星"（来自 Entity 元素的 Name 属性）
};

/**
 * @brief 想定完整信息
 *
 * 一个想定代表一次仿真演练的完整描述，包含元信息（名称、时间范围、步长等）
 * 和参与仿真的实体列表。
 * 数据来源：XML 想定文件（<Scenario> 根元素）解析 + 代码自动关联数据文件。
 */
struct Scenario {
    QString name;                       //!< 想定名称，例如 "海上编队巡逻想定"（XML: ScenarioInfo/Attribute[@Name='SceName']）
    QString filePath;                   //!< 想定 XML 文件的完整路径（代码设置，非 XML 解析结果）
    QString createTime;                 //!< 想定创建时间，字符串格式（XML: ScenarioInfo/Attribute[@Name='CreateTime']）
    QDateTime startTime;                //!< 仿真开始时间（XML: ScenarioInfo/Attribute[@Name='StartTime']）
    QDateTime endTime;                  //!< 仿真结束时间，作为回放自动停止的上限（XML: ScenarioInfo/Attribute[@Name='EndTime']）
    int simStepMs = 100;                //!< 仿真步长（毫秒），控制每次 tick 推进的时间窗口大小（XML: ScenarioInfo/Attribute[@Name='SimStep']）
    QStringList dataFiles;              //!< 关联的回放数据文件绝对路径列表（代码自动扫描"回放数据/"子目录，非 XML 解析）
    QList<EntityInfo> entities;         //!< 想定中包含的仿真实体列表（仅含 ID 和 Name）
};

/**
 * @brief 想定摘要（用于树形列表展示）
 *
 * 轻量级信息，在 scanScenarios() 阶段快速生成，
 * 不包含实体详情和数据文件关联，仅用于 UI 列表展示。
 */
struct ScenarioSummary {
    QString name;           //!< 想定名称
    QString scenarioDir;    //!< 想定目录的绝对路径（dataFiles/ 下的子目录）
    QString filePath;       //!< 想定 XML 文件的完整路径
    int entityCount = 0;    //!< 实体数量（来自 XML 中 <Entities> 区域的 Entity 元素计数）
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

    /** @brief 获取 dataFiles 根目录路径 */
    QString dataFilesRoot() const;

    /**
     * @brief 从想定目录加载实体ID映射表
     * @param scenarioDir 想定目录路径
     * @return 当前值 → 映射值 的映射表（文件不存在或解析失败返回空表）
     */
    QMap<QString, QString> loadEntityIdMapping(const QString &scenarioDir);

    /**
     * @brief 增量保存实体ID映射到想定目录下的 mapping.json
     * @param scenarioDir 想定目录路径
     * @param newMappings 本次新增/更新的映射（当前值 → 映射值）
     * @return true 保存成功
     */
    bool saveEntityIdMapping(const QString &scenarioDir, const QMap<QString, QString> &newMappings);

    /**
     * @brief 重命名想定文件夹（仅修改目录名，内部文件不变）
     * @param oldDirPath 原想定目录的绝对路径
     * @param newName    新目录名（不含路径前缀）
     * @return true 重命名成功
     * @note 若重命名的恰为当前已加载想定，同步更新 m_currentScenario 的路径和名称
     */
    bool renameScenario(const QString &oldDirPath, const QString &newName);

    /**
     * @brief 重命名数据文件
     * @param oldFilePath 原数据文件的绝对路径
     * @param newName     新文件名（直接使用用户输入，不强制修改后缀）
     * @return true 重命名成功
     * @note 若文件属于当前已加载想定，同步更新 m_currentScenario->dataFiles 中的路径
     */
    bool renameDataFile(const QString &oldFilePath, const QString &newName);

    /**
     * @brief 删除整个想定目录（含所有数据文件和配置）
     * @param scenarioDirPath 想定目录的绝对路径
     * @return true 删除成功；若为当前已加载想定则同时清除
     */
    bool deleteScenario(const QString &scenarioDirPath);

    /**
     * @brief 删除单个数据文件
     * @param filePath 数据文件的绝对路径
     * @return true 删除成功，自动从当前想定 dataFiles 列表中移除
     */
    bool deleteDataFile(const QString &filePath);

private:
    /**
     * @brief 解析想定 XML 文件
     */
    bool parseScenarioXml(const QString &filePath, Scenario &scenario);

    /** @brief 关联想定目录下的数据文件 */
    QStringList findDataFiles(const QString &scenarioDir);

    Scenario *m_currentScenario = nullptr;
};

#endif // SCENARIOMGR_H
