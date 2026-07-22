/**********************************************************
* 文件名: eight_protocol_benchmark_test.cpp
* 日期: 2026-07-20
* 版本: v1.2
* 更新记录: 精确核对 SPI mode 0-3 数据及 I2C repeated START、ACK/NACK 数量
* 描述: 重放仿真 golden VCD，核对八协议行为及零 Mismatch 合同
**********************************************************/

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>

#include "protocol_analysis.h"
#include "test_temp_directory.h"

namespace {

bool expect(const bool condition, const QString& message)
{
    if (!condition) QTextStream(stderr) << "FAIL: " << message << '\n';
    return condition;
}

bool readJson(const QString& path, QJsonObject* const object)
{
    if (object == nullptr) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    *object = document.object();
    return true;
}

bool copyFixtureFile(const QString& source, const QString& destination)
{
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) return false;
    QFile::remove(destination);
    return QFile::copy(source, destination);
}

bool copyGoldenInputs(const QString& benchmarkRoot, const QString& taskRoot)
{
    const QDir source(QDir(benchmarkRoot).filePath(QStringLiteral("golden")));
    const QDir destination(taskRoot);
    return copyFixtureFile(
               source.filePath(QStringLiteral("waveform/capture.vcd")),
               destination.filePath(QStringLiteral("waveform/capture.vcd"))) &&
        copyFixtureFile(
               source.filePath(QStringLiteral("waveform/capture_schema.json")),
               destination.filePath(QStringLiteral("waveform/capture_schema.json"))) &&
        copyFixtureFile(
               source.filePath(QStringLiteral("evidence/capture_sidecar.json")),
               destination.filePath(QStringLiteral("evidence/capture_sidecar.json")));
}

bool verifySrec(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    bool hasData = false;
    QByteArray terminal;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) continue;
        if (line.size() < 4 || line.at(0) != 'S') return false;
        const QByteArray bytes = QByteArray::fromHex(line.mid(2));
        if (bytes.isEmpty() || bytes.size() != static_cast<unsigned char>(bytes.at(0)) + 1) {
            return false;
        }
        unsigned int checksum = 0U;
        for (const char value : bytes) checksum += static_cast<unsigned char>(value);
        if ((checksum & 0xffU) != 0xffU) return false;
        if (line.startsWith("S3")) hasData = true;
        if (line.startsWith("S7")) terminal = line;
    }
    return hasData && terminal == QByteArrayLiteral("S70500000000FA");
}

bool fileContains(const QString& path, const QByteArray& needle)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.readAll().contains(needle);
}

QHash<QString, QList<QJsonObject>> eventsByGroup(const QJsonArray& events)
{
    QHash<QString, QList<QJsonObject>> result;
    for (const QJsonValue& value : events) {
        const QJsonObject event = value.toObject();
        result[event.value(QStringLiteral("group_id")).toString()].append(event);
    }
    return result;
}

bool verifySpiEvents(const QList<QJsonObject>& events, const QJsonObject& contract)
{
    const QJsonObject expectedModeCounts = contract.value(QStringLiteral("mode_counts")).toObject();
    const QJsonArray expectedTxCycle = contract.value(QStringLiteral("tx_cycle")).toArray();
    const QJsonArray expectedRxCycle = contract.value(QStringLiteral("rx_cycle")).toArray();
    QHash<int, int> actualModeCounts;
    if (!expect(!expectedTxCycle.isEmpty() && expectedTxCycle.size() == expectedRxCycle.size(),
                QStringLiteral("SPI data cycles are non-empty and aligned"))) {
        return false;
    }
    for (int index = 0; index < events.size(); ++index) {
        const QJsonObject fields = events.at(index).value(QStringLiteral("fields")).toObject();
        const int mode = fields.value(QStringLiteral("mode")).toInt(-1);
        ++actualModeCounts[mode];
        const int cycleIndex = index % expectedTxCycle.size();
        if (!expect(fields.value(QStringLiteral("mode_available")).toBool(),
                    QStringLiteral("SPI transfer %1 exposes simulation mode hint").arg(index)) ||
            !expect(mode == index % 4,
                    QStringLiteral("SPI transfer %1 mode is %2").arg(index).arg(index % 4)) ||
            !expect(fields.value(QStringLiteral("cpol")).toInt(-1) == mode / 2,
                    QStringLiteral("SPI transfer %1 CPOL matches mode").arg(index)) ||
            !expect(fields.value(QStringLiteral("cpha")).toInt(-1) == mode % 2,
                    QStringLiteral("SPI transfer %1 CPHA matches mode").arg(index)) ||
            !expect(fields.value(QStringLiteral("bit_count")).toInt() == 8,
                    QStringLiteral("SPI transfer %1 has 8 bits").arg(index)) ||
            !expect(fields.value(QStringLiteral("tx")).toString() ==
                        expectedTxCycle.at(cycleIndex).toString(),
                    QStringLiteral("SPI transfer %1 TX data matches").arg(index)) ||
            !expect(fields.value(QStringLiteral("rx")).toString() ==
                        expectedRxCycle.at(cycleIndex).toString(),
                    QStringLiteral("SPI transfer %1 RX data matches").arg(index))) {
            return false;
        }
    }
    for (auto iterator = expectedModeCounts.constBegin(); iterator != expectedModeCounts.constEnd(); ++iterator) {
        const int mode = iterator.key().toInt();
        if (!expect(actualModeCounts.value(mode) == iterator.value().toInt(),
                    QStringLiteral("SPI mode %1 transfer count: %2")
                        .arg(mode).arg(actualModeCounts.value(mode)))) {
            return false;
        }
    }
    return true;
}

bool verifyI2cEvents(const QList<QJsonObject>& events, const QJsonObject& contract)
{
    QHash<QString, int> typeCounts;
    int ackCount = 0;
    int nackCount = 0;
    int repeatedStartCount = 0;
    int readCount = 0;
    int writeCount = 0;
    QHash<QString, int> transferCounts;
    for (const QJsonObject& event : events) {
        const QString type = event.value(QStringLiteral("type")).toString();
        const QString summary = event.value(QStringLiteral("summary")).toString();
        ++typeCounts[type];
        if (type == QStringLiteral("i2c_ack")) {
            event.value(QStringLiteral("fields")).toObject().value(QStringLiteral("ack")).toBool()
                ? ++ackCount : ++nackCount;
        }
        if (type == QStringLiteral("i2c_segment") &&
            summary.startsWith(QStringLiteral("I2C REPEATED_START"))) {
            ++repeatedStartCount;
            if (!expect(summary.contains(
                            QStringLiteral("ADDR=%1").arg(contract.value(
                                QStringLiteral("repeated_start_write_address")).toString())),
                        QStringLiteral("I2C repeated START write address matches firmware"))) {
                return false;
            }
        }
        if (type == QStringLiteral("i2c_transfer")) {
            const QJsonObject fields = event.value(QStringLiteral("fields")).toObject();
            const QString operation = fields.value(QStringLiteral("operation")).toString();
            const QString address = fields.value(QStringLiteral("address")).toString();
            ++transferCounts[operation + QLatin1Char('@') + address];
            if (operation == QStringLiteral("read")) ++readCount;
            if (operation == QStringLiteral("write")) ++writeCount;
        }
    }
    const QJsonObject expectedTypeCounts = contract.value(QStringLiteral("type_counts")).toObject();
    for (auto iterator = expectedTypeCounts.constBegin(); iterator != expectedTypeCounts.constEnd(); ++iterator) {
        if (!expect(typeCounts.value(iterator.key()) == iterator.value().toInt(),
                    QStringLiteral("I2C %1 count: %2")
                        .arg(iterator.key()).arg(typeCounts.value(iterator.key())))) {
            return false;
        }
    }
    for (const QJsonValue& value : contract.value(QStringLiteral("transfer_contracts")).toArray()) {
        const QJsonObject expected = value.toObject();
        const QString key = expected.value(QStringLiteral("operation")).toString() +
            QLatin1Char('@') + expected.value(QStringLiteral("address")).toString();
        if (!expect(transferCounts.value(key) == expected.value(QStringLiteral("count")).toInt(),
                    QStringLiteral("I2C %1 transfer count: %2")
                        .arg(key).arg(transferCounts.value(key)))) {
            return false;
        }
    }
    return expect(ackCount == contract.value(QStringLiteral("ack_count")).toInt(),
                  QStringLiteral("I2C ACK count: %1").arg(ackCount)) &&
        expect(nackCount == contract.value(QStringLiteral("nack_count")).toInt(),
               QStringLiteral("I2C NACK count: %1").arg(nackCount)) &&
        expect(repeatedStartCount == contract.value(QStringLiteral("repeated_start_count")).toInt(),
               QStringLiteral("I2C repeated START count: %1").arg(repeatedStartCount)) &&
        expect(readCount == contract.value(QStringLiteral("read_count")).toInt(),
               QStringLiteral("I2C read count: %1").arg(readCount)) &&
        expect(writeCount == contract.value(QStringLiteral("write_count")).toInt(),
               QStringLiteral("I2C write count: %1").arg(writeCount));
}

bool verifyJtagScans(const QList<QJsonObject>& events, const QJsonObject& contract)
{
    const QJsonObject scanContracts = contract.value(QStringLiteral("scan_contracts")).toObject();
    QHash<QString, QList<QJsonObject>> scansByRegister;
    for (const QJsonObject& event : events) {
        if (event.value(QStringLiteral("type")).toString() != QStringLiteral("jtag_scan")) continue;
        const QJsonObject fields = event.value(QStringLiteral("fields")).toObject();
        scansByRegister[fields.value(QStringLiteral("register")).toString()].append(fields);
    }
    for (auto iterator = scanContracts.constBegin(); iterator != scanContracts.constEnd(); ++iterator) {
        const QString registerName = iterator.key();
        const QJsonObject expected = iterator.value().toObject();
        const QList<QJsonObject> actual = scansByRegister.value(registerName);
        if (!expect(actual.size() == expected.value(QStringLiteral("count")).toInt(),
                    QStringLiteral("JTAG %1 scan count: %2")
                        .arg(registerName.toUpper()).arg(actual.size()))) {
            return false;
        }
        for (const QJsonObject& fields : actual) {
            if (!expect(fields.value(QStringLiteral("bit_count")).toInt() ==
                            expected.value(QStringLiteral("bit_count")).toInt(),
                        QStringLiteral("JTAG %1 bit count").arg(registerName.toUpper())) ||
                !expect(fields.value(QStringLiteral("tdi_bits")).toString() ==
                            expected.value(QStringLiteral("tdi_bits")).toString(),
                        QStringLiteral("JTAG %1 TDI value").arg(registerName.toUpper())) ||
                !expect(fields.value(QStringLiteral("tdo_bits")).toString() ==
                            expected.value(QStringLiteral("tdo_bits")).toString(),
                        QStringLiteral("JTAG %1 TDO value").arg(registerName.toUpper()))) {
                return false;
            }
        }
    }
    return true;
}

bool verifyExpectedEvents(const QJsonObject& analysis, const QJsonObject& expected)
{
    const QJsonArray protocolEvents = analysis.value(QStringLiteral("protocol_events")).toArray();
    const QHash<QString, QList<QJsonObject>> grouped = eventsByGroup(protocolEvents);
    const QJsonObject groups = expected.value(QStringLiteral("groups")).toObject();
    for (auto iterator = groups.constBegin(); iterator != groups.constEnd(); ++iterator) {
        const QString groupId = iterator.key();
        const QJsonObject contract = iterator.value().toObject();
        const QList<QJsonObject> actual = grouped.value(groupId);
        if (!expect(actual.size() == contract.value(QStringLiteral("expected_events")).toInt(),
                    QStringLiteral("%1 event count: %2").arg(groupId).arg(actual.size()))) {
            return false;
        }
        QSet<QString> actualTypes;
        int completeTransactions = 0;
        const QString transactionType = contract.value(QStringLiteral("transaction_type")).toString();
        for (const QJsonObject& event : actual) {
            const QString type = event.value(QStringLiteral("type")).toString();
            actualTypes.insert(type);
            if (type == transactionType) ++completeTransactions;
        }
        if (!expect(completeTransactions >= contract.value(QStringLiteral("minimum_transactions")).toInt(),
                    QStringLiteral("%1 complete transaction count: %2")
                        .arg(groupId).arg(completeTransactions))) {
            return false;
        }
        for (const QJsonValue& value : contract.value(QStringLiteral("required_types")).toArray()) {
            const QString requiredType = value.toString();
            if (!expect(actualTypes.contains(requiredType),
                        QStringLiteral("%1 contains %2").arg(groupId, requiredType))) {
                return false;
            }
        }
        if (groupId == QStringLiteral("spi") && !verifySpiEvents(actual, contract)) return false;
        if (groupId == QStringLiteral("i2c") && !verifyI2cEvents(actual, contract)) return false;
        if (groupId == QStringLiteral("jtag") && !verifyJtagScans(actual, contract)) return false;
    }

    QStringList summaries;
    for (const QJsonValue& value : protocolEvents) {
        summaries.append(value.toObject().value(QStringLiteral("summary")).toString());
    }
    const QString joinedSummaries = summaries.join(QLatin1Char('\n'));
    for (const QJsonValue& value : expected.value(QStringLiteral("required_semantics")).toArray()) {
        if (!expect(joinedSummaries.contains(value.toString()),
                    QStringLiteral("semantic summary contains %1").arg(value.toString()))) {
            return false;
        }
    }

    if (!expect(!grouped.contains(QStringLiteral("mismatch")),
                QStringLiteral("golden has no mismatch protocol event"))) {
        return false;
    }
    for (const QJsonValue& value : analysis.value(QStringLiteral("groups")).toArray()) {
        const QJsonObject group = value.toObject();
        const QString groupId = group.value(QStringLiteral("id")).toString();
        if (groups.contains(groupId) &&
            (!expect(group.value(QStringLiteral("status")).toString() == QStringLiteral("event_detected"),
                     QStringLiteral("%1 group status is event_detected").arg(groupId)) ||
             !expect(group.value(QStringLiteral("reason")).toString() == QStringLiteral("检测到真实协议事务"),
                     QStringLiteral("%1 group reason describes detected activity").arg(groupId)))) {
            return false;
        }
        if (groupId == QStringLiteral("mismatch")) {
            return expect(group.value(QStringLiteral("transactions")).toArray().isEmpty(),
                          QStringLiteral("mismatch group is explicitly empty"));
        }
    }
    return expect(false, QStringLiteral("analysis contains mismatch group"));
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    if (argc < 2) {
        QTextStream(stderr) << "usage: eight_protocol_benchmark_test <benchmark-root> [--update-golden]\n";
        return 2;
    }

    const QString benchmarkRoot = QDir::cleanPath(QString::fromLocal8Bit(argv[1]));
    const bool updateGolden = argc >= 3 && QString::fromLocal8Bit(argv[2]) == QStringLiteral("--update-golden");
    QJsonObject expected;
    QJsonObject manifest;
    QJsonObject behaviorMatrix;
    if (!expect(readJson(QDir(benchmarkRoot).filePath(QStringLiteral("expected_protocols.json")), &expected),
                QStringLiteral("expected protocol contract is readable")) ||
        !expect(readJson(QDir(benchmarkRoot).filePath(QStringLiteral("benchmark_manifest.json")), &manifest),
                QStringLiteral("benchmark manifest is readable")) ||
        !expect(manifest.value(QStringLiteral("entry_address")).toString() == QStringLiteral("0x00000000"),
                QStringLiteral("benchmark manifest fixes the run entry at 0x0")) ||
        !expect(manifest.value(QStringLiteral("program_done_marker")).toString() ==
                    QStringLiteral("PROGRAM_RUN_DONE") &&
                    fileContains(QDir(benchmarkRoot).filePath(QStringLiteral(
                        "firmware/noelv_eight_protocol_benchmark.c")),
                        QByteArrayLiteral("PROGRAM_RUN_DONE")),
                QStringLiteral("firmware and manifest expose the host program-done marker")) ||
        !expect(manifest.value(QStringLiteral("minimum_complete_transactions_per_protocol")).toInt() == 48 &&
                    manifest.value(QStringLiteral("expected_ahb_transactions")).toInt() == 256,
                QStringLiteral("manifest fixes the expanded golden transaction counts")) ||
        !expect((behaviorMatrix = manifest.value(QStringLiteral("board_behavior_matrix")).toObject())
                        .value(QStringLiteral("ahb")).toObject()
                        .value(QStringLiteral("explicit_accesses")).toInt() == 448 &&
                    behaviorMatrix.value(QStringLiteral("uart")).toObject()
                        .value(QStringLiteral("binary_bytes")).toInt() == 48 &&
                    behaviorMatrix.value(QStringLiteral("spi")).toObject()
                        .value(QStringLiteral("transfers")).toInt() == 48 &&
                    behaviorMatrix.value(QStringLiteral("can")).toObject()
                        .value(QStringLiteral("attempted_frames")).toInt() == 32 &&
                    behaviorMatrix.value(QStringLiteral("i2c")).toObject()
                        .value(QStringLiteral("repeated_start_reads")).toInt() == 12 &&
                    behaviorMatrix.value(QStringLiteral("jtag")).toObject()
                        .value(QStringLiteral("host_scans")).toInt() == 48,
                QStringLiteral("manifest exposes the expanded board behavior matrix")) ||
        !expect(fileContains(QDir(benchmarkRoot).filePath(QStringLiteral(
                        "firmware/noelv_eight_protocol_benchmark.c")),
                        QByteArrayLiteral("I2C_WRITE | (readBack != 0 ? 0U : I2C_STOP)")),
                QStringLiteral("firmware keeps the bus active before repeated START reads")) ||
        !expect(fileContains(QDir(benchmarkRoot).filePath(QStringLiteral(
                        "firmware/noelv_eight_protocol_benchmark.c")),
                        QByteArrayLiteral("(transaction & 0x3U) == 0x3U")),
                QStringLiteral("firmware schedules exactly 12 repeated START reads")) ||
        !expect(verifySrec(QDir(benchmarkRoot).filePath(
                    QStringLiteral("firmware/noelv_eight_protocol_benchmark.srec"))),
                QStringLiteral("firmware SREC checksums and 0x0 termination entry are valid"))) {
        return 1;
    }

    QTemporaryDir temporary(lockstepTestTemporaryTemplate(QStringLiteral("eight_protocol")));
    if (!updateGolden &&
        !expect(temporary.isValid() && copyGoldenInputs(benchmarkRoot, temporary.path()),
                QStringLiteral("golden inputs can be copied to an isolated task"))) {
        return 1;
    }
    const QString taskRoot = updateGolden
        ? QDir(benchmarkRoot).filePath(QStringLiteral("golden")) : temporary.path();

    lockstep::protocol_analyzer::ProtocolAnalysisRequest request;
    request.taskRootPath = taskRoot;
    request.taskId = QStringLiteral("eight-protocol-golden");
    request.reportDiagnosticsToErrorRegistry = false;
    const auto result = lockstep::protocol_analyzer::ProtocolAnalyzer().analyzeTask(request);
    if (!expect(result.success && result.wroteAnalysis,
                QStringLiteral("protocol analyzer accepts the golden VCD: %1").arg(result.errorMessage)) ||
        !verifyExpectedEvents(result.analysis, expected)) {
        return 1;
    }

    if (!updateGolden) {
        QJsonObject committed;
        if (!expect(readJson(
                        QDir(benchmarkRoot).filePath(QStringLiteral("golden/evidence/protocol_analysis.json")),
                        &committed),
                    QStringLiteral("committed golden analysis is readable")) ||
            !expect(committed.value(QStringLiteral("protocol_events")).toArray() ==
                        result.analysis.value(QStringLiteral("protocol_events")).toArray(),
                    QStringLiteral("replayed protocol events match committed golden analysis")) ||
            !expect(committed.value(QStringLiteral("groups")).toArray() ==
                        result.analysis.value(QStringLiteral("groups")).toArray(),
                    QStringLiteral("replayed protocol groups match committed golden analysis"))) {
            return 1;
        }
    }

    QTextStream(stdout) << (updateGolden ? "updated" : "verified")
                        << " eight-protocol golden benchmark\n";
    return 0;
}
