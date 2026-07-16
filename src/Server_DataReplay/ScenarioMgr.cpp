/**
 * @file ScenarioMgr.cpp
 * @brief 想定管理类的实现
 *
 * 实现想定的扫描、加载、解析功能：
 * - scanScenarios()：扫描 dataFiles/ 目录树，生成想定摘要列表供前端展示
 * - loadScenario()：解析 XML 想定文件，关联回放数据文件
 * - parseScenarioXml()：流式解析想定 XML，提取元信息和实体列表
 * - loadEntityIdMapping() / saveEntityIdMapping()：读写 mapping.json 映射配置
 */

#include "ScenarioMgr.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>
#include <QCoreApplication>

ScenarioMgr::ScenarioMgr(QObject *parent)
    : QObject(parent)
{
}

ScenarioMgr::~ScenarioMgr()
{
    delete m_currentScenario;
}

QString ScenarioMgr::dataFilesRoot() const
{
    // 基于可执行文件目录定位 dataFiles/，不受工作目录变化影响
    return QCoreApplication::applicationDirPath() + "/../dataFiles";
}

QList<ScenarioSummary> ScenarioMgr::scanScenarios()
{
    QList<ScenarioSummary> result;
    QString rootPath = dataFilesRoot();
    QDir rootDir(rootPath);

    if (!rootDir.exists()) {
        qWarning() << "dataFiles directory not found:" << rootPath;
        return result;
    }

    // 遍历 dataFiles/ 下的所有子目录（每个子目录 = 一个想定）
    const QFileInfoList dirList = rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo &dirInfo : dirList) {
        QDir scenarioDir(dirInfo.absoluteFilePath());

        // 每个想定目录下取所有 .xml 文件作为想定文件
        const QStringList xmlFiles = scenarioDir.entryList(QStringList() << "*.xml", QDir::Files);

        for (const QString &xmlFile : xmlFiles) {
            QString xmlPath = scenarioDir.absoluteFilePath(xmlFile);

            Scenario tempScenario;
            if (parseScenarioXml(xmlPath, tempScenario)) {
                ScenarioSummary summary;
                summary.name        = tempScenario.name;
                summary.scenarioDir = dirInfo.absoluteFilePath();
                summary.filePath    = xmlPath;
                summary.entityCount = tempScenario.entities.size();
                result.append(summary);
            }
        }
    }

    return result;
}

bool ScenarioMgr::loadScenario(const QString &scenarioFilePath)
{
    // ① 清除上一次加载的想定
    if (m_currentScenario) {
        delete m_currentScenario;
        m_currentScenario = nullptr;
        emit scenarioUnloaded();
    }

    // ② 完整解析 XML 想定文件
    Scenario *scenario = new Scenario();
    if (!parseScenarioXml(scenarioFilePath, *scenario)) {
        delete scenario;
        qWarning() << "Failed to parse scenario XML:" << scenarioFilePath;
        return false;
    }

    scenario->filePath = scenarioFilePath;

    // ③ 关联回放数据：自动扫描想定目录下的"回放数据/"子目录
    QFileInfo fileInfo(scenarioFilePath);
    QString scenarioDir = fileInfo.absolutePath();
    scenario->dataFiles = findDataFiles(scenarioDir);

    // ④ 保存为当前想定并通知前端
    m_currentScenario = scenario;
    emit scenarioLoaded(*scenario);

    qDebug() << "Scenario loaded:" << scenario->name
             << "entities:" << scenario->entities.size()
             << "dataFiles:" << scenario->dataFiles.size();

    return true;
}

const Scenario *ScenarioMgr::currentScenario() const
{
    return m_currentScenario;
}

bool ScenarioMgr::parseScenarioXml(const QString &filePath, Scenario &scenario)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open scenario file:" << filePath;
        return false;
    }

    QXmlStreamReader xml(&file);

    // 主循环：按实际 XML 结构分两个区域解析
    //   <ScenarioInfo>  → 想定元信息（Attribute Name="..." Value="..." 格式）
    //   <Entities>      → 仿真实体列表（仅提取 ID 和 Name）
    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartElement) {

            // ========== ScenarioInfo 区域：解析想定元信息 ==========
            if (xml.name() == QLatin1String("ScenarioInfo")) {
                // 遍历 <Attribute Name="..." Value="..."/> 子元素，直到 </ScenarioInfo>
                while (!(xml.tokenType() == QXmlStreamReader::EndElement &&
                         xml.name() == QLatin1String("ScenarioInfo"))) {
                    xml.readNext();

                    if (xml.tokenType() == QXmlStreamReader::StartElement &&
                        xml.name() == QLatin1String("Attribute")) {
                        QXmlStreamAttributes attrs = xml.attributes();
                        QString name  = attrs.value("Name").toString();
                        QString value = attrs.value("Value").toString();

                        if (name == QLatin1String("SceName")) {
                            scenario.name = value;
                        } else if (name == QLatin1String("CreateTime")) {
                            scenario.createTime = value;
                        } else if (name == QLatin1String("StartTime")) {
                            // 兼容有无毫秒的两种格式
                            scenario.startTime = QDateTime::fromString(
                                value.trimmed(), "yyyy-MM-dd HH:mm:ss");
                            if (!scenario.startTime.isValid()) {
                                scenario.startTime = QDateTime::fromString(
                                    value.trimmed(), "yyyy-MM-dd HH:mm:ss.zzz");
                            }
                        } else if (name == QLatin1String("EndTime")) {
                            scenario.endTime = QDateTime::fromString(
                                value.trimmed(), "yyyy-MM-dd HH:mm:ss");
                            if (!scenario.endTime.isValid()) {
                                scenario.endTime = QDateTime::fromString(
                                    value.trimmed(), "yyyy-MM-dd HH:mm:ss.zzz");
                            }
                        } else if (name == QLatin1String("SimStep")) {
                            scenario.simStepMs = value.toInt();
                        }
                    }
                }
            }

            // ========== Entities 区域：解析仿真实体列表 ==========
            else if (xml.name() == QLatin1String("Entities")) {
                // 提取每个 Entity 的 ID 和 Name
                while (!(xml.tokenType() == QXmlStreamReader::EndElement &&
                         xml.name() == QLatin1String("Entities"))) {
                    xml.readNext();

                    if (xml.tokenType() == QXmlStreamReader::StartElement &&
                        xml.name() == QLatin1String("Entity")) {
                        EntityInfo entity;
                        QXmlStreamAttributes entAttrs = xml.attributes();
                        entity.id   = entAttrs.value("ID").toString();
                        entity.name = entAttrs.value("Name").toString();

                        // 跳过 Entity 内部的子元素，不解析
                        while (!(xml.tokenType() == QXmlStreamReader::EndElement &&
                                 xml.name() == QLatin1String("Entity"))) {
                            xml.readNext();
                        }
                        scenario.entities.append(entity);
                    }
                }
            }
        }
    }

    file.close();

    if (xml.hasError()) {
        qWarning() << "XML parse error:" << xml.errorString();
        return false;
    }

    // 必须至少有想定名称，否则视为解析失败
    return !scenario.name.isEmpty();
}

QStringList ScenarioMgr::findDataFiles(const QString &scenarioDir)
{
    QStringList dataFiles;
    // 回放数据固定放在想定目录下的 "回放数据/" 子目录
    QDir replayDir(scenarioDir + "/回放数据");

    if (!replayDir.exists()) {
        qWarning() << "回放数据 directory not found:" << replayDir.absolutePath();
        return dataFiles;
    }

    // 按文件名排序收集所有 .txt 数据文件
    const QStringList files = replayDir.entryList(
        QStringList() << "*.txt", QDir::Files, QDir::Name);

    for (const QString &file : files) {
        dataFiles.append(replayDir.absoluteFilePath(file));
    }

    return dataFiles;
}

// ==================== 实体ID映射配置 ====================

QMap<QString, QString> ScenarioMgr::loadEntityIdMapping(const QString &scenarioDir)
{
    // 从想定目录下的 mapping.json 读取"实体ID"映射组
    // 返回 QMap<当前值, 映射值>（字符串键值对）
    QMap<QString, QString> result;

    QString mappingPath = scenarioDir + "/mapping.json";
    QFile file(mappingPath);
    if (!file.open(QIODevice::ReadOnly))
        return result;  // 文件不存在，返回空表

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject())
        return result;

    QJsonObject root = doc.object();
    if (!root.contains("实体ID"))
        return result;

    // 遍历"实体ID"数组，提取每条 {当前值, 映射值}
    QJsonArray entityIdArray = root["实体ID"].toArray();
    for (const QJsonValue &entry : entityIdArray) {
        if (!entry.isObject())
            continue;
        QJsonObject obj = entry.toObject();
        // 用 toVariant().toString() 统一处理数值和字符串类型
        QString curVal = obj["当前值"].toVariant().toString();
        QString mapVal = obj["映射值"].toVariant().toString();
        if (!curVal.isEmpty()) {
            result[curVal] = mapVal;
        }
    }

    return result;
}

bool ScenarioMgr::saveEntityIdMapping(const QString &scenarioDir,
                                       const QMap<QString, QString> &newMappings)
{
    if (newMappings.isEmpty())
        return true;

    const QString mappingPath = scenarioDir + "/mapping.json";

    // ① 读取现有映射文件
    QJsonObject root;
    QFile file(mappingPath);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isObject())
            root = doc.object();
    }

    // ② 将现有条目按"当前值"索引
    QMap<QString, QJsonObject> entries;
    const QJsonArray oldArray = root["实体ID"].toArray();
    for (const QJsonValue &val : oldArray) {
        QJsonObject obj = val.toObject();
        QString curVal = obj["当前值"].toVariant().toString();
        if (!curVal.isEmpty())
            entries[curVal] = obj;
    }

    // ③ 增量合并
    for (auto it = newMappings.begin(); it != newMappings.end(); ++it) {
        QJsonObject entry = entries.value(it.key());
        entry["当前值"] = it.key();
        entry["映射值"] = it.value();
        entries[it.key()] = entry;
    }

    // ⑤ 重建数组并写回
    QJsonArray newArray;
    for (auto it = entries.cbegin(); it != entries.cend(); ++it)
        newArray.append(it.value());
    root["实体ID"] = newArray;

    QFile outFile(mappingPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to open mapping.json for writing:" << mappingPath;
        return false;
    }
    // Indented 生成带缩进和换行的格式化 JSON，便于人工阅读和手动编辑
    outFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    outFile.close();

    qDebug() << "Entity ID mapping saved to:" << mappingPath
             << "total entries:" << entries.size();
    return true;
}

// ==================== 文件管理操作 ====================

bool ScenarioMgr::renameScenario(const QString &oldDirPath, const QString &newName)
{
    QDir oldDir(oldDirPath);
    if (!oldDir.exists()) {
        qWarning() << "场景目录不存在:" << oldDirPath;
        return false;
    }

    // 构造新目录路径
    QString parentPath = QFileInfo(oldDirPath).absolutePath();
    QString newDirPath = parentPath + "/" + newName;

    if (QDir(newDirPath).exists()) {
        qWarning() << "目标目录已存在:" << newDirPath;
        return false;
    }

    // 仅重命名目录，不修改内部文件（树结构基于文件系统扫描，无需名称绑定）
    if (!oldDir.rename(oldDirPath, newDirPath)) {
        qWarning() << "目录重命名失败:" << oldDirPath << "->" << newDirPath;
        return false;
    }

    // 如果是当前已加载的想定，更新 XML 文件路径
    if (m_currentScenario) {
        QFileInfo loadedFi(m_currentScenario->filePath);
        if (loadedFi.absolutePath() == oldDirPath) {
            QString oldXmlName = loadedFi.fileName();
            m_currentScenario->filePath = newDirPath + "/" + oldXmlName;
            m_currentScenario->name = newName;
        }
    }

    qDebug() << "想定已重命名:" << oldDirPath << "->" << newDirPath;
    return true;
}

bool ScenarioMgr::renameDataFile(const QString &oldFilePath, const QString &newName)
{
    QFileInfo fi(oldFilePath);
    QString parentDir = fi.absolutePath();

    // 直接使用用户输入的名称，不强制修改后缀
    QString newFilePath = parentDir + "/" + newName;

    if (QFile::exists(newFilePath)) {
        qWarning() << "目标文件已存在:" << newFilePath;
        return false;
    }

    if (!QFile::rename(oldFilePath, newFilePath)) {
        qWarning() << "文件重命名失败:" << oldFilePath << "->" << newFilePath;
        return false;
    }

    // 更新当前想定的 dataFiles 列表
    if (m_currentScenario) {
        int idx = m_currentScenario->dataFiles.indexOf(oldFilePath);
        if (idx >= 0)
            m_currentScenario->dataFiles[idx] = newFilePath;
    }

    qDebug() << "数据文件已重命名:" << oldFilePath << "->" << newFilePath;
    return true;
}

bool ScenarioMgr::deleteScenario(const QString &scenarioDirPath)
{
    QDir dir(scenarioDirPath);
    if (!dir.exists()) {
        qWarning() << "想定目录不存在:" << scenarioDirPath;
        return false;
    }

    // 检查是否正在删除当前已加载的想定
    bool isCurrentLoaded = false;
    if (m_currentScenario) {
        QFileInfo fi(m_currentScenario->filePath);
        if (fi.absolutePath() == scenarioDirPath)
            isCurrentLoaded = true;
    }

    if (!dir.removeRecursively()) {
        qWarning() << "删除目录失败:" << scenarioDirPath;
        return false;
    }

    // 如果删除了当前想定，清除状态
    if (isCurrentLoaded) {
        delete m_currentScenario;
        m_currentScenario = nullptr;
        emit scenarioUnloaded();
    }

    qDebug() << "想定已删除:" << scenarioDirPath;
    return true;
}

bool ScenarioMgr::deleteDataFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.exists()) {
        qWarning() << "数据文件不存在:" << filePath;
        return false;
    }

    if (!file.remove()) {
        qWarning() << "删除文件失败:" << filePath;
        return false;
    }

    // 从当前想定的 dataFiles 列表中移除
    if (m_currentScenario)
        m_currentScenario->dataFiles.removeAll(filePath);

    qDebug() << "数据文件已删除:" << filePath;
    return true;
}
