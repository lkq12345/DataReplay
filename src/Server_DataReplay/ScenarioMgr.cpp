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
    // 从可执行文件所在目录的相对路径定位 dataFiles/
    // 目录结构：bin/（exe + dll）、dataFiles/（想定数据）
    return QDir::currentPath() + "/../dataFiles";
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

            // 快速解析 XML 拿到名称和实体数量（不加载数据文件）
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

DataFileInfo ScenarioMgr::getDataFileInfo(const QString &filePath)
{
    DataFileInfo info;
    info.filePath = filePath;
    QFileInfo fi(filePath);
    info.fileSize = fi.size();

    // 估算记录数和时间范围（不加载全文，仅快速扫描）
    quint64 recordCount = 0;
    QDateTime minTime, maxTime;
    estimateDataFileInfo(filePath, recordCount, minTime, maxTime);
    info.recordCount = recordCount;
    info.minTime = minTime;
    info.maxTime = maxTime;

    return info;
}

bool ScenarioMgr::parseScenarioXml(const QString &filePath, Scenario &scenario)
{
    // 使用流式解析器，内存友好
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open scenario file:" << filePath;
        return false;
    }

    QXmlStreamReader xml(&file);

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartElement) {
            // ----- 想定元信息 -----
            if (xml.name() == QLatin1String("Name")) {
                scenario.name = xml.readElementText();
            } else if (xml.name() == QLatin1String("Description")) {
                scenario.description = xml.readElementText();
            } else if (xml.name() == QLatin1String("StartTime")) {
                scenario.startTime = QDateTime::fromString(
                    xml.readElementText().trimmed(), "yyyy-MM-dd HH:mm:ss");
            } else if (xml.name() == QLatin1String("EndTime")) {
                scenario.endTime = QDateTime::fromString(
                    xml.readElementText().trimmed(), "yyyy-MM-dd HH:mm:ss");
            } else if (xml.name() == QLatin1String("SimStep")) {
                scenario.simStepMs = xml.readElementText().toInt();
            }
            // ----- 仿真实体 -----
            else if (xml.name() == QLatin1String("Entity")) {
                EntityInfo entity;
                // 实体属性：ID、名称、类型
                QXmlStreamAttributes attrs = xml.attributes();
                entity.id   = attrs.value("ID").toString();
                entity.name = attrs.value("Name").toString();
                entity.type = attrs.value("type").toString();

                // 读取实体的 <Attribute> 子元素
                while (!(xml.tokenType() == QXmlStreamReader::EndElement &&
                         xml.name() == QLatin1String("Entity"))) {
                    xml.readNext();

                    if (xml.tokenType() == QXmlStreamReader::StartElement &&
                        xml.name() == QLatin1String("Attribute")) {
                        QXmlStreamAttributes attrAttrs = xml.attributes();
                        entity.x       = attrAttrs.value("X").toDouble();
                        entity.y       = attrAttrs.value("Y").toDouble();
                        entity.z       = attrAttrs.value("Z").toDouble();
                        entity.speed   = attrAttrs.value("Speed").toDouble();
                        entity.heading = attrAttrs.value("Heading").toDouble();
                        entity.status  = attrAttrs.value("Status").toString();
                    }
                }
                scenario.entities.append(entity);
            }
        }
    }

    file.close();

    if (xml.hasError()) {
        qWarning() << "XML parse error:" << xml.errorString();
        return false;
    }

    // 必须至少有心定名称，否则视为解析失败
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

void ScenarioMgr::estimateDataFileInfo(const QString &filePath, quint64 &recordCount,
                                        QDateTime &minTime, QDateTime &maxTime)
{
    // 快速估算数据文件的基本信息：起始时间、结束时间、总行数
    // 设计目标：不加载全文到内存，仅读取首行 + 尾部 4KB 缓冲区

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open data file:" << filePath;
        return;
    }

    recordCount = 0;
    minTime = QDateTime();
    maxTime = QDateTime();
    bool firstLine = true;

    // ---- 读取第一行获取起始时间 ----
    QByteArray firstLineBytes = file.readLine();
    if (firstLineBytes.isEmpty()) {
        file.close();
        return;
    }

    recordCount++;

    // 解析首行外层时间戳（前 23 字符）
    QString firstLineStr = QString::fromUtf8(firstLineBytes).trimmed();
    int spacePos = firstLineStr.indexOf(' ');
    if (spacePos > 0) {
        QString tsStr = firstLineStr.left(spacePos);
        minTime = QDateTime::fromString(tsStr, "yyyy-MM-dd HH:mm:ss.zzz");
    }

    // ---- 从文件尾部提取最后一行的时间 ----
    qint64 fileSize = file.size();
    const int bufferSize = 4096;
    QByteArray tailBuffer;

    if (fileSize > bufferSize) {
        // 大文件：只读取尾部 4KB
        file.seek(fileSize - bufferSize);
        tailBuffer = file.read(bufferSize);
    } else {
        // 小文件：全量读取
        file.seek(0);
        tailBuffer = file.readAll();
    }

    QString tailStr = QString::fromUtf8(tailBuffer);
    QStringList lines = tailStr.split('\n', QString::SkipEmptyParts);
    if (!lines.isEmpty()) {
        QString lastLine = lines.last().trimmed();
        int lastSpacePos = lastLine.indexOf(' ');
        if (lastSpacePos > 0) {
            QString tsStr = lastLine.left(lastSpacePos);
            maxTime = QDateTime::fromString(tsStr, "yyyy-MM-dd HH:mm:ss.zzz");
        }
    }

    // ---- 估算总行数：取文件中部的样本，计算平均行字节数 ----
    const int sampleBufferSize = 8192;
    qint64 samplePos = fileSize / 2;
    if (samplePos + sampleBufferSize < fileSize) {
        file.seek(samplePos);
        QByteArray sample = file.read(sampleBufferSize);
        int newlineCount = sample.count('\n');
        if (newlineCount > 0) {
            double avgBytesPerLine = (double)sampleBufferSize / newlineCount;
            recordCount = (quint64)(fileSize / avgBytesPerLine);
        }
    } else {
        // 文件太小：直接数换行符
        file.seek(0);
        QByteArray allData = file.readAll();
        recordCount = allData.count('\n');
    }

    file.close();
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

    QString mappingPath = scenarioDir + "/mapping.json";

    // ① 读取现有文件内容（不存在则从空结构开始）
    QJsonObject root;
    QFile file(mappingPath);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isObject())
            root = doc.object();
    }

    // ② 将现有"实体ID"数组转为 QMap，key=当前值，value=完整JSON对象（保留其他字段）
    QMap<QString, QJsonObject> existingEntries;
    if (root.contains("实体ID")) {
        QJsonArray oldArray = root["实体ID"].toArray();
        for (const QJsonValue &entry : oldArray) {
            if (!entry.isObject())
                continue;
            QJsonObject obj = entry.toObject();
            QString curVal = obj["当前值"].toVariant().toString();
            if (!curVal.isEmpty())
                existingEntries[curVal] = obj;
        }
    }

    // ③ 增量合并：遍历新的映射键值对，更新或追加
    for (auto it = newMappings.begin(); it != newMappings.end(); ++it) {
        const QString &curVal = it.key();
        const QString &mapVal = it.value();

        QJsonObject entry;
        // 智能类型保持：值为纯数字 → JSON 整数；否则 → JSON 字符串
        bool curIsInt = false, mapIsInt = false;
        int curInt = curVal.toInt(&curIsInt);
        int mapInt = mapVal.toInt(&mapIsInt);

        entry["当前值"] = curIsInt ? QJsonValue(curInt) : QJsonValue(curVal);
        entry["映射值"] = mapIsInt ? QJsonValue(mapInt) : QJsonValue(mapVal);

        existingEntries[curVal] = entry;
    }

    // ④ 将合并后的映射表重建为 JSON 数组，写回"实体ID"键
    QJsonArray newArray;
    for (auto it = existingEntries.begin(); it != existingEntries.end(); ++it) {
        newArray.append(it.value());
    }
    root["实体ID"] = newArray;

    // ⑤ 格式化写入文件（Indented 美化输出，方便手动编辑）
    QFile outFile(mappingPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to open mapping.json for writing:" << mappingPath;
        return false;
    }

    QJsonDocument doc(root);
    outFile.write(doc.toJson(QJsonDocument::Indented));
    outFile.close();

    qDebug() << "Entity ID mapping saved to:" << mappingPath
             << "total entries:" << existingEntries.size();
    return true;
}
