#include "ScenarioMgr.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
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
    // 从执行目录的相对路径定位 dataFiles/
    // DLL 位于 bin/ 下，dataFiles/ 与 bin/ 同级
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

    // 遍历 dataFiles/ 下所有子目录
    const QFileInfoList dirList = rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo &dirInfo : dirList) {
        QDir scenarioDir(dirInfo.absoluteFilePath());

        // 查找该目录下的 XML 文件作为想定文件
        const QStringList xmlFiles = scenarioDir.entryList(QStringList() << "*.xml", QDir::Files);

        for (const QString &xmlFile : xmlFiles) {
            QString xmlPath = scenarioDir.absoluteFilePath(xmlFile);

            // 解析 XML 获取想定名称和实体数量
            Scenario tempScenario;
            if (parseScenarioXml(xmlPath, tempScenario)) {
                ScenarioSummary summary;
                summary.name = tempScenario.name;
                summary.scenarioDir = dirInfo.absoluteFilePath();
                summary.filePath = xmlPath;
                summary.entityCount = tempScenario.entities.size();
                result.append(summary);
            }
        }
    }

    return result;
}

bool ScenarioMgr::loadScenario(const QString &scenarioFilePath)
{
    // 清除当前想定
    if (m_currentScenario) {
        delete m_currentScenario;
        m_currentScenario = nullptr;
        emit scenarioUnloaded();
    }

    // 解析 XML
    Scenario *scenario = new Scenario();
    if (!parseScenarioXml(scenarioFilePath, *scenario)) {
        delete scenario;
        qWarning() << "Failed to parse scenario XML:" << scenarioFilePath;
        return false;
    }

    scenario->filePath = scenarioFilePath;

    // 关联数据文件：想定目录下的 "回放数据/" 子目录
    QFileInfo fileInfo(scenarioFilePath);
    QString scenarioDir = fileInfo.absolutePath();
    scenario->dataFiles = findDataFiles(scenarioDir);

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
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open scenario file:" << filePath;
        return false;
    }

    QXmlStreamReader xml(&file);

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartElement) {
            // 想定元信息
            if (xml.name() == QLatin1String("Name")) {
                scenario.name = xml.readElementText();
            } else if (xml.name() == QLatin1String("Description")) {
                scenario.description = xml.readElementText();
            } else if (xml.name() == QLatin1String("StartTime")) {
                scenario.startTime = QDateTime::fromString(xml.readElementText().trimmed(), "yyyy-MM-dd HH:mm:ss");
            } else if (xml.name() == QLatin1String("EndTime")) {
                scenario.endTime = QDateTime::fromString(xml.readElementText().trimmed(), "yyyy-MM-dd HH:mm:ss");
            } else if (xml.name() == QLatin1String("SimStep")) {
                scenario.simStepMs = xml.readElementText().toInt();
            }
            // 仿真实体
            else if (xml.name() == QLatin1String("Entity")) {
                EntityInfo entity;
                QXmlStreamAttributes attrs = xml.attributes();
                entity.id = attrs.value("ID").toString();
                entity.name = attrs.value("Name").toString();
                entity.type = attrs.value("type").toString();

                // 读取子元素 Attribute
                while (!(xml.tokenType() == QXmlStreamReader::EndElement &&
                         xml.name() == QLatin1String("Entity"))) {
                    xml.readNext();

                    if (xml.tokenType() == QXmlStreamReader::StartElement &&
                        xml.name() == QLatin1String("Attribute")) {
                        QXmlStreamAttributes attrAttrs = xml.attributes();
                        entity.x = attrAttrs.value("X").toDouble();
                        entity.y = attrAttrs.value("Y").toDouble();
                        entity.z = attrAttrs.value("Z").toDouble();
                        entity.speed = attrAttrs.value("Speed").toDouble();
                        entity.heading = attrAttrs.value("Heading").toDouble();
                        entity.status = attrAttrs.value("Status").toString();
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

    return !scenario.name.isEmpty();
}

QStringList ScenarioMgr::findDataFiles(const QString &scenarioDir)
{
    QStringList dataFiles;
    QDir replayDir(scenarioDir + "/回放数据");

    if (!replayDir.exists()) {
        qWarning() << "回放数据 directory not found:" << replayDir.absolutePath();
        return dataFiles;
    }

    // 查找所有 .txt 文件
    const QStringList files = replayDir.entryList(QStringList() << "*.txt", QDir::Files, QDir::Name);

    for (const QString &file : files) {
        dataFiles.append(replayDir.absoluteFilePath(file));
    }

    return dataFiles;
}

void ScenarioMgr::estimateDataFileInfo(const QString &filePath, quint64 &recordCount,
                                        QDateTime &minTime, QDateTime &maxTime)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open data file:" << filePath;
        return;
    }

    recordCount = 0;
    minTime = QDateTime();
    maxTime = QDateTime();
    bool firstLine = true;

    // 读取第一行（获取起始时间）
    QByteArray firstLineBytes = file.readLine();
    if (firstLineBytes.isEmpty()) {
        file.close();
        return;
    }

    recordCount++;

    // 解析外层时间戳
    QString firstLineStr = QString::fromUtf8(firstLineBytes).trimmed();
    int spacePos = firstLineStr.indexOf(' ');
    if (spacePos > 0) {
        QString tsStr = firstLineStr.left(spacePos);
        minTime = QDateTime::fromString(tsStr, "yyyy-MM-dd HH:mm:ss.zzz");
    }

    // 从文件尾倒序读取最后几行获取结束时间和估算行数
    qint64 fileSize = file.size();
    const int bufferSize = 4096;
    QByteArray tailBuffer;

    if (fileSize > bufferSize) {
        file.seek(fileSize - bufferSize);
        tailBuffer = file.read(bufferSize);
    } else {
        file.seek(0);
        tailBuffer = file.readAll();
    }

    // 从尾部缓冲区提取最后一行
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

    // 估算行数：取中间位置的行样本，估算平均行字节数
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
        // 小文件直接计算
        file.seek(0);
        QByteArray allData = file.readAll();
        recordCount = allData.count('\n');
    }

    file.close();
}
