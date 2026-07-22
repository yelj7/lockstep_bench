/**********************************************************
* 文件名: lockstep_trace_analysis_test.cpp
* 日期: 2026-07-19
* 版本: v3.2
* 更新记录: 增加基于全局时间戳的稀疏 UART 8N1 字节流重建回归。
* 描述: 验证九协议、稀疏事件、mismatch 和共享时间轴进入展示模型。
**********************************************************/

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTextStream>

#include "error_registry.h"
#include "protocol_analysis.h"
#include "test_temp_directory.h"
#include "waveform_trace_viewer.h"

namespace {

bool writeTextFile(const QString& path, const QString& text)
{
    const QFileInfo info(path);
    QDir dir;
    if (!dir.mkpath(info.absolutePath())) {
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    const QByteArray payload = text.toUtf8();
    if (file.write(payload) != payload.size()) {
        return false;
    }
    return file.commit();
}

void setPackedValue(QString* const bits, const int lsb, const int width, const quint64 value)
{
    if (bits == nullptr) {
        return;
    }
    for (int bit = 0; bit < width; ++bit) {
        (*bits)[bits->size() - 1 - (lsb + bit)] = ((value >> bit) & 1U) != 0U
            ? QLatin1Char('1') : QLatin1Char('0');
    }
}

bool writeWideProtocolFixture(const QString& taskRootPath)
{
    const QString waveformPath = QDir(taskRootPath).filePath(QStringLiteral("waveform"));
    if (!QDir().mkpath(waveformPath)) return false;
    QString bits(1024, QLatin1Char('0'));
    for (const int realSourceBit : {768, 770, 773, 776, 781, 782}) {
        setPackedValue(&bits, realSourceBit, 1, 1U);
    }
    setPackedValue(&bits, 512, 1, 1U);
    setPackedValue(&bits, 513, 1, 1U);
    setPackedValue(&bits, 547, 1, 1U);
    setPackedValue(&bits, 576, 1, 1U);
    setPackedValue(&bits, 577, 1, 1U);
    setPackedValue(&bits, 608, 1, 1U);
    setPackedValue(&bits, 609, 1, 1U);

    QString vcd = QStringLiteral(
        "$timescale 1 ns $end\n$scope module logic $end\n"
        "$var wire 1024 ! lockstep_trace_sample [1023:0] $end\n"
        "$upscope $end\n$enddefinitions $end\n");
    int time = 0;
    const auto emitSample = [&vcd, &time](const QString& value) {
        vcd += QStringLiteral("#%1\nb%2 !\n").arg(time).arg(value);
        time += 10;
    };
    emitSample(bits);

    setPackedValue(&bits, 32, 32, 0x80001000U);
    setPackedValue(&bits, 416, 1, 1U);
    setPackedValue(&bits, 417, 2, 2U);
    setPackedValue(&bits, 429, 1, 1U);
    setPackedValue(&bits, 320, 16, 1U);
    emitSample(bits);
    setPackedValue(&bits, 32, 32, 0x80002000U);
    emitSample(bits);
    setPackedValue(&bits, 417, 2, 0U);
    emitSample(bits);

    setPackedValue(&bits, 516, 1, 1U);
    setPackedValue(&bits, 520, 8, 0x55U);
    setPackedValue(&bits, 518, 1, 1U);
    emitSample(bits);
    setPackedValue(&bits, 518, 1, 0U);
    emitSample(bits);

    setPackedValue(&bits, 547, 1, 0U);
    emitSample(bits);
    const quint8 spiTx = 0xA5U;
    const quint8 spiRx = 0x3CU;
    for (int bit = 7; bit >= 0; --bit) {
        setPackedValue(&bits, 544, 1, 0U);
        setPackedValue(&bits, 545, 1, (spiTx >> bit) & 1U);
        setPackedValue(&bits, 546, 1, (spiRx >> bit) & 1U);
        emitSample(bits);
        setPackedValue(&bits, 544, 1, 1U);
        emitSample(bits);
    }
    setPackedValue(&bits, 547, 1, 1U);
    emitSample(bits);

    setPackedValue(&bits, 578, 1, 1U);
    setPackedValue(&bits, 584, 11, 0x123U);
    emitSample(bits);
    setPackedValue(&bits, 578, 1, 0U);
    setPackedValue(&bits, 579, 1, 1U);
    setPackedValue(&bits, 595, 4, 0xAU);
    emitSample(bits);
    setPackedValue(&bits, 579, 1, 0U);

    setPackedValue(&bits, 609, 1, 0U);
    setPackedValue(&bits, 610, 1, 1U);
    emitSample(bits);
    setPackedValue(&bits, 610, 1, 0U);
    const QList<quint8> i2cBytes = {quint8(0xA0U), quint8(0x5AU)};
    for (int byteIndex = 0; byteIndex < i2cBytes.size(); ++byteIndex) {
        const quint8 byte = i2cBytes.at(byteIndex);
        for (int bit = 7; bit >= 0; --bit) {
            setPackedValue(&bits, 608, 1, 0U);
            setPackedValue(&bits, 609, 1, (byte >> bit) & 1U);
            emitSample(bits);
            setPackedValue(&bits, 608, 1, 1U);
            setPackedValue(&bits, 614, 1, 1U);
            emitSample(bits);
            setPackedValue(&bits, 614, 1, 0U);
        }
        setPackedValue(&bits, 608, 1, 0U);
        setPackedValue(&bits, 609, 1, 0U);
        emitSample(bits);
        setPackedValue(&bits, 608, 1, 1U);
        setPackedValue(&bits, 614, 1, 1U);
        emitSample(bits);
        setPackedValue(&bits, 614, 1, 0U);
        if (byteIndex == 0) {
            setPackedValue(&bits, 608, 1, 0U);
            setPackedValue(&bits, 609, 1, 1U);
            emitSample(bits);
            setPackedValue(&bits, 608, 1, 1U);
            setPackedValue(&bits, 614, 1, 1U);
            emitSample(bits);
            setPackedValue(&bits, 614, 1, 0U);
            setPackedValue(&bits, 609, 1, 0U);
            setPackedValue(&bits, 610, 1, 1U);
            emitSample(bits);
            setPackedValue(&bits, 610, 1, 0U);
        }
    }
    setPackedValue(&bits, 608, 1, 0U);
    setPackedValue(&bits, 609, 1, 0U);
    emitSample(bits);
    setPackedValue(&bits, 608, 1, 1U);
    emitSample(bits);
    setPackedValue(&bits, 609, 1, 1U);
    setPackedValue(&bits, 611, 1, 1U);
    emitSample(bits);
    setPackedValue(&bits, 611, 1, 0U);

    setPackedValue(&bits, 646, 1, 1U);
    setPackedValue(&bits, 688, 16, 0x0800U);
    emitSample(bits);
    setPackedValue(&bits, 646, 1, 0U);
    setPackedValue(&bits, 647, 1, 1U);
    emitSample(bits);
    setPackedValue(&bits, 647, 1, 0U);

    setPackedValue(&bits, 737, 1, 1U);
    setPackedValue(&bits, 738, 1, 1U);
    setPackedValue(&bits, 736, 1, 1U);
    emitSample(bits);

    const QString schema = QStringLiteral(
        "{\"schema_version\":\"2.0\",\"sample_width\":1024,"
        "\"physical_channels\":1024,\"trace_profile_id\":"
        "\"trace.noelv.lockstep_1024\"}\n");
    return writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture.vcd")), vcd) &&
        writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture_schema.json")), schema);
}

bool writeSparseEventFixture(const QString& taskRootPath, const quint32 windowStart = 1000U,
                             const quint64 timestamp = 1250U)
{
    const QString evidencePath = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(evidencePath)) return false;
    const QString sidecar = QStringLiteral(
        "{\"schema\":\"lockstep-capture-sidecar-v3\",\"sample_rate_hz\":1000000000,"
        "\"window_start_index\":%1,\"protocol_events\":\"evidence/protocol_events.json\"}\n")
        .arg(windowStart);
    const QString events = QStringLiteral(
        "{\"schema\":\"lockstep-protocol-events-v3\",\"capture_id\":42,"
        "\"timebase_hz\":1000000000,\"implemented_source_mask\":415,"
        "\"enabled_source_mask\":387,\"design_gap_mask\":96,\"events\":[{"
        "\"timestamp_ticks\":\"%1\",\"capture_id\":42,\"local_sequence\":7,"
        "\"protocol_id\":1,\"event_type\":1,\"source_kind\":0,\"flags\":0,"
        "\"event_reason_mask\":2,\"payload_hex\":\"41\"}]}\n").arg(timestamp);
    return writeTextFile(QDir(evidencePath).filePath(QStringLiteral("capture_sidecar.json")), sidecar) &&
        writeTextFile(QDir(evidencePath).filePath(QStringLiteral("protocol_events.json")), events);
}

bool writeSparseUartWrapFixture(const QString& taskRootPath, const bool complete)
{
    const QString evidencePath = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(evidencePath)) return false;
    constexpr quint32 windowStart = 0xfffff000U;
    constexpr quint64 frameStart = 0x1'fffffff0ULL;
    constexpr quint64 bitTicks = 1040U;
    constexpr quint8 data = 0x55U;
    const int emittedBits = complete ? 10 : 3;
    const quint64 windowCount = frameStart -
        ((frameStart & ~0xffffffffULL) | windowStart) + quint64(emittedBits) * bitTicks + 1U;
    QJsonArray events;
    bool tx = true;
    int sequence = 0;
    QList<bool> levels{false};
    for (int bit = 0; bit < 8; ++bit) levels.append(((data >> bit) & 1U) != 0U);
    levels.append(true);
    for (int bit = 0; bit < emittedBits; ++bit) {
        const bool level = levels.at(bit);
        if (level == tx) continue;
        quint8 state = level ? 0x03U : 0x02U;
        if (tx && !level) state |= 0x10U;
        tx = level;
        QJsonObject event;
        event.insert(QStringLiteral("timestamp_ticks"),
                     QString::number(frameStart + quint64(bit) * bitTicks));
        event.insert(QStringLiteral("capture_id"), complete ? 83 : 84);
        event.insert(QStringLiteral("local_sequence"), sequence++);
        event.insert(QStringLiteral("protocol_id"), 1);
        event.insert(QStringLiteral("event_type"), 1);
        event.insert(QStringLiteral("source_kind"), 0);
        event.insert(QStringLiteral("flags"), 0);
        event.insert(QStringLiteral("event_reason_mask"), 2);
        event.insert(QStringLiteral("payload_hex"),
                     QStringLiteral("%1").arg(state, 2, 16, QLatin1Char('0')));
        events.append(event);
    }
    QJsonObject archive{{QStringLiteral("schema"), QStringLiteral("lockstep-protocol-events-v3")},
                        {QStringLiteral("capture_id"), complete ? 83 : 84},
                        {QStringLiteral("timebase_hz"), 120'000'000},
                        {QStringLiteral("implemented_source_mask"), 0x19f},
                        {QStringLiteral("enabled_source_mask"), 0x19f},
                        {QStringLiteral("design_gap_mask"), 0x060},
                        {QStringLiteral("events"), events}};
    QJsonObject sidecar{{QStringLiteral("schema"), QStringLiteral("lockstep-capture-sidecar-v3")},
                        {QStringLiteral("sample_rate_hz"), 120'000'000},
                        {QStringLiteral("actual_sample_count"), static_cast<qint64>(windowCount)},
                        {QStringLiteral("window_start_index"), static_cast<qint64>(windowStart)},
                        {QStringLiteral("window_end_index"),
                         QString::number(quint64(windowStart) + windowCount - 1U)},
                        {QStringLiteral("protocol_events"), QStringLiteral("evidence/protocol_events.json")}};
    return writeTextFile(QDir(evidencePath).filePath(QStringLiteral("capture_sidecar.json")),
                         QString::fromUtf8(QJsonDocument(sidecar).toJson(QJsonDocument::Compact))) &&
        writeTextFile(QDir(evidencePath).filePath(QStringLiteral("protocol_events.json")),
                      QString::fromUtf8(QJsonDocument(archive).toJson(QJsonDocument::Compact)));
}

bool writeSparseRoundingFixture(const QString& taskRootPath)
{
    const QString waveformPath = QDir(taskRootPath).filePath(QStringLiteral("waveform"));
    if (!QDir().mkpath(waveformPath) || !writeSparseEventFixture(taskRootPath, 0U, 2U)) return false;
    QString bits(1024, QLatin1Char('0'));
    const QString vcd = QStringLiteral(
        "$timescale 1 ps $end\n$scope module logic $end\n"
        "$var wire 1024 ! lockstep_trace_sample [1023:0] $end\n"
        "$upscope $end\n$enddefinitions $end\n#0\nb%1 !\n#20000\nb%1 !\n").arg(bits);
    const QString schema = QStringLiteral(
        "{\"schema_version\":\"2.0\",\"sample_width\":1024,"
        "\"physical_channels\":1024}\n");
    const QString evidencePath = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
    const QString eventPath = QDir(evidencePath).filePath(QStringLiteral("protocol_events.json"));
    QJsonObject archive;
    QFile eventFile(eventPath);
    if (!eventFile.open(QIODevice::ReadOnly) ||
        !(archive = QJsonDocument::fromJson(eventFile.readAll()).object()).contains(QStringLiteral("events"))) {
        return false;
    }
    eventFile.close();
    archive.insert(QStringLiteral("timebase_hz"), 120'000'000);
    return writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture.vcd")), vcd) &&
        writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture_schema.json")), schema) &&
        writeTextFile(eventPath, QString::fromUtf8(QJsonDocument(archive).toJson(QJsonDocument::Compact)));
}

bool writeSparsePrecedenceFixture(const QString& taskRootPath)
{
    if (!writeWideProtocolFixture(taskRootPath)) return false;
    const QString evidencePath = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(evidencePath)) return false;
    QJsonArray events;
    const auto append = [&events](const int sequence, const quint64 timestamp, const int protocol,
                                  const quint8 state) {
        QJsonObject event;
        event.insert(QStringLiteral("timestamp_ticks"), QString::number(timestamp));
        event.insert(QStringLiteral("capture_id"), 85);
        event.insert(QStringLiteral("local_sequence"), sequence);
        event.insert(QStringLiteral("protocol_id"), protocol);
        event.insert(QStringLiteral("event_type"), 1);
        event.insert(QStringLiteral("source_kind"), 0);
        event.insert(QStringLiteral("flags"), 0);
        event.insert(QStringLiteral("event_reason_mask"), protocol == 1 ? 2 : 0x10);
        event.insert(QStringLiteral("payload_hex"),
                     QStringLiteral("%1").arg(state, 2, 16, QLatin1Char('0')));
        events.append(event);
    };
    append(0, 35U, 1, 0x12U);
    append(1, 5U, 4, 0x05U);
    for (int bit = 0; bit < 8; ++bit) append(2 + bit, 6U + quint64(bit), 4, 0x41U);
    QJsonObject archive{{QStringLiteral("schema"), QStringLiteral("lockstep-protocol-events-v3")},
                        {QStringLiteral("capture_id"), 85},
                        {QStringLiteral("timebase_hz"), 1'000'000'000},
                        {QStringLiteral("implemented_source_mask"), 0x19f},
                        {QStringLiteral("enabled_source_mask"), 0x19f},
                        {QStringLiteral("design_gap_mask"), 0x060},
                        {QStringLiteral("events"), events}};
    QJsonObject sidecar{{QStringLiteral("schema"), QStringLiteral("lockstep-capture-sidecar-v3")},
                        {QStringLiteral("sample_rate_hz"), 1'000'000'000},
                        {QStringLiteral("actual_sample_count"), 39},
                        {QStringLiteral("window_start_index"), 0},
                        {QStringLiteral("window_end_index"), QStringLiteral("38")},
                        {QStringLiteral("protocol_events"), QStringLiteral("evidence/protocol_events.json")}};
    return writeTextFile(QDir(evidencePath).filePath(QStringLiteral("capture_sidecar.json")),
                         QString::fromUtf8(QJsonDocument(sidecar).toJson(QJsonDocument::Compact))) &&
        writeTextFile(QDir(evidencePath).filePath(QStringLiteral("protocol_events.json")),
                      QString::fromUtf8(QJsonDocument(archive).toJson(QJsonDocument::Compact)));
}

bool writeSparseUartMarkerFixture(const QString& taskRootPath)
{
    const QString evidencePath = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(evidencePath)) return false;

    constexpr quint64 timebaseHz = 120'000'000ULL;
    constexpr quint64 bitTicks = 1040ULL;
    quint64 tick = 100'000ULL;
    quint32 sequence = 0U;
    bool tx = true;
    QJsonArray events;
    const QByteArray text = QByteArrayLiteral("PROGRAM_RUN_DONE\n");
    for (const char rawByte : text) {
        const quint8 byte = static_cast<quint8>(rawByte);
        QList<bool> levels;
        levels.append(false);
        for (int bit = 0; bit < 8; ++bit) levels.append(((byte >> bit) & 1U) != 0U);
        levels.append(true);
        for (int bit = 0; bit < levels.size(); ++bit) {
            const bool level = levels.at(bit);
            if (level != tx) {
                tx = level;
                QJsonObject event;
                event.insert(QStringLiteral("timestamp_ticks"), QString::number(tick + quint64(bit) * bitTicks));
                event.insert(QStringLiteral("capture_id"), 77);
                event.insert(QStringLiteral("local_sequence"), static_cast<qint64>(sequence++));
                event.insert(QStringLiteral("protocol_id"), 1);
                event.insert(QStringLiteral("event_type"), 1);
                event.insert(QStringLiteral("source_kind"), 0);
                event.insert(QStringLiteral("flags"), 0);
                event.insert(QStringLiteral("event_reason_mask"), 2);
                event.insert(QStringLiteral("payload_hex"), tx ? QStringLiteral("47") : QStringLiteral("56"));
                events.append(event);
            }
        }
        tick += 10ULL * bitTicks;
    }

    QJsonObject archive;
    archive.insert(QStringLiteral("schema"), QStringLiteral("lockstep-protocol-events-v3"));
    archive.insert(QStringLiteral("capture_id"), 77);
    archive.insert(QStringLiteral("timebase_hz"), static_cast<qint64>(timebaseHz));
    archive.insert(QStringLiteral("implemented_source_mask"), 0x19f);
    archive.insert(QStringLiteral("enabled_source_mask"), 0x19f);
    archive.insert(QStringLiteral("design_gap_mask"), 0x060);
    archive.insert(QStringLiteral("events"), events);

    QJsonObject sidecar;
    sidecar.insert(QStringLiteral("schema"), QStringLiteral("lockstep-capture-sidecar-v3"));
    sidecar.insert(QStringLiteral("sample_rate_hz"), static_cast<qint64>(timebaseHz));
    sidecar.insert(QStringLiteral("window_start_index"), 0);
    sidecar.insert(QStringLiteral("protocol_events"), QStringLiteral("evidence/protocol_events.json"));
    return writeTextFile(
               QDir(evidencePath).filePath(QStringLiteral("capture_sidecar.json")),
               QString::fromUtf8(QJsonDocument(sidecar).toJson(QJsonDocument::Compact))) &&
        writeTextFile(
               QDir(evidencePath).filePath(QStringLiteral("protocol_events.json")),
               QString::fromUtf8(QJsonDocument(archive).toJson(QJsonDocument::Compact)));
}

bool writeSparseUartNegativeFixture(const QString& taskRootPath)
{
    const QString evidencePath = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(evidencePath)) return false;
    constexpr quint64 bitTicks = 1040U;
    bool tx = true;
    bool rx = true;
    int sequence = 0;
    QJsonArray events;
    const auto appendState = [&](const quint64 timestamp, const bool nextTx, const bool nextRx) {
        quint8 state = quint8((nextTx ? 1U : 0U) | (nextRx ? 2U : 0U));
        if (tx && !nextTx) state |= 0x10U;
        if (rx && !nextRx) state |= 0x20U;
        tx = nextTx;
        rx = nextRx;
        QJsonObject event;
        event.insert(QStringLiteral("timestamp_ticks"), QString::number(timestamp));
        event.insert(QStringLiteral("capture_id"), 82);
        event.insert(QStringLiteral("local_sequence"), sequence++);
        event.insert(QStringLiteral("protocol_id"), 1);
        event.insert(QStringLiteral("event_type"), 1);
        event.insert(QStringLiteral("source_kind"), 0);
        event.insert(QStringLiteral("flags"), 0);
        event.insert(QStringLiteral("event_reason_mask"), 2);
        event.insert(QStringLiteral("payload_hex"),
                     QStringLiteral("%1").arg(state, 2, 16, QLatin1Char('0')));
        events.append(event);
    };
    const auto appendFrame = [&](const quint64 start, const quint8 data, const bool stopHigh,
                                 const bool txDirection, const int emittedBits = 10) {
        QList<bool> levels{false};
        for (int bit = 0; bit < 8; ++bit) levels.append(((data >> bit) & 1U) != 0U);
        levels.append(stopHigh);
        bool current = txDirection ? tx : rx;
        for (int bit = 0; bit < emittedBits; ++bit) {
            if (levels.at(bit) == current) continue;
            current = levels.at(bit);
            appendState(start + quint64(bit) * bitTicks,
                        txDirection ? current : tx, txDirection ? rx : current);
        }
        if (emittedBits == 10 && !current) {
            appendState(start + 10U * bitTicks, txDirection ? true : tx,
                        txDirection ? rx : true);
        }
    };
    appendFrame(100'000U, 0x33U, false, true);
    appendFrame(112'000U, 0x00U, false, true);
    appendFrame(124'000U, 0xa6U, true, true);
    appendFrame(136'000U, 0x5aU, true, false);
    constexpr quint64 truncatedStart = 148'000U;
    appendFrame(truncatedStart, 0xc3U, true, true, 3);
    constexpr quint64 windowEnd = truncatedStart + 3U * bitTicks;

    QJsonObject archive{{QStringLiteral("schema"), QStringLiteral("lockstep-protocol-events-v3")},
                        {QStringLiteral("capture_id"), 82},
                        {QStringLiteral("timebase_hz"), 120'000'000},
                        {QStringLiteral("implemented_source_mask"), 0x19f},
                        {QStringLiteral("enabled_source_mask"), 0x19f},
                        {QStringLiteral("design_gap_mask"), 0x060},
                        {QStringLiteral("events"), events}};
    QJsonObject sidecar{{QStringLiteral("schema"), QStringLiteral("lockstep-capture-sidecar-v3")},
                        {QStringLiteral("sample_rate_hz"), 120'000'000},
                        {QStringLiteral("window_start_index"), 0},
                        {QStringLiteral("window_end_index"), QString::number(windowEnd)},
                        {QStringLiteral("protocol_events"), QStringLiteral("evidence/protocol_events.json")}};
    return writeTextFile(QDir(evidencePath).filePath(QStringLiteral("capture_sidecar.json")),
                         QString::fromUtf8(QJsonDocument(sidecar).toJson(QJsonDocument::Compact))) &&
        writeTextFile(QDir(evidencePath).filePath(QStringLiteral("protocol_events.json")),
                      QString::fromUtf8(QJsonDocument(archive).toJson(QJsonDocument::Compact)));
}

bool writeSparseSpiFixture(const QString& taskRootPath)
{
    const QString evidencePath = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(evidencePath)) return false;
    constexpr quint32 tx = 0xa5c33c5aU;
    constexpr quint32 rx = 0x3cc3a55aU;
    QJsonArray events;
    for (int bit = 31; bit >= 0; --bit) {
        const quint8 state = quint8(0x21U | (((tx >> bit) & 1U) << 1U) |
                                    (((rx >> bit) & 1U) << 2U));
        QJsonObject event;
        event.insert(QStringLiteral("timestamp_ticks"), QString::number(200'000ULL + quint64(31 - bit) * 4ULL));
        event.insert(QStringLiteral("capture_id"), 78);
        event.insert(QStringLiteral("local_sequence"), 31 - bit);
        event.insert(QStringLiteral("protocol_id"), 2);
        event.insert(QStringLiteral("event_type"), 1);
        event.insert(QStringLiteral("source_kind"), 0);
        event.insert(QStringLiteral("flags"), 0);
        event.insert(QStringLiteral("event_reason_mask"), 4);
        event.insert(QStringLiteral("payload_hex"), QStringLiteral("%1").arg(state, 2, 16, QLatin1Char('0')));
        events.append(event);
    }
    QJsonObject archive;
    archive.insert(QStringLiteral("schema"), QStringLiteral("lockstep-protocol-events-v3"));
    archive.insert(QStringLiteral("capture_id"), 78);
    archive.insert(QStringLiteral("timebase_hz"), 120'000'000);
    archive.insert(QStringLiteral("implemented_source_mask"), 0x19f);
    archive.insert(QStringLiteral("enabled_source_mask"), 0x19f);
    archive.insert(QStringLiteral("design_gap_mask"), 0x060);
    archive.insert(QStringLiteral("events"), events);
    QJsonObject sidecar;
    sidecar.insert(QStringLiteral("schema"), QStringLiteral("lockstep-capture-sidecar-v3"));
    sidecar.insert(QStringLiteral("sample_rate_hz"), 120'000'000);
    sidecar.insert(QStringLiteral("window_start_index"), 0);
    sidecar.insert(QStringLiteral("protocol_events"), QStringLiteral("evidence/protocol_events.json"));
    return writeTextFile(QDir(evidencePath).filePath(QStringLiteral("capture_sidecar.json")),
                         QString::fromUtf8(QJsonDocument(sidecar).toJson(QJsonDocument::Compact))) &&
        writeTextFile(QDir(evidencePath).filePath(QStringLiteral("protocol_events.json")),
                      QString::fromUtf8(QJsonDocument(archive).toJson(QJsonDocument::Compact)));
}

bool writeSparseI2cFixture(const QString& taskRootPath)
{
    const QString evidencePath = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(evidencePath)) return false;
    QJsonArray events;
    quint32 sequence = 0U;
    quint64 tick = 300'000ULL;
    const auto appendEvent = [&](const quint8 state) {
        QJsonObject event;
        event.insert(QStringLiteral("timestamp_ticks"), QString::number(tick));
        event.insert(QStringLiteral("capture_id"), 79);
        event.insert(QStringLiteral("local_sequence"), static_cast<qint64>(sequence++));
        event.insert(QStringLiteral("protocol_id"), 4);
        event.insert(QStringLiteral("event_type"), 1);
        event.insert(QStringLiteral("source_kind"), 0);
        event.insert(QStringLiteral("flags"), 0);
        event.insert(QStringLiteral("event_reason_mask"), 0x10);
        event.insert(QStringLiteral("payload_hex"), QStringLiteral("%1").arg(state, 2, 16, QLatin1Char('0')));
        events.append(event);
        tick += 180ULL;
    };
    appendEvent(0x05U);
    for (const quint8 byte : {quint8(0xa0U), quint8(0x5aU)}) {
        for (int bit = 7; bit >= 0; --bit) appendEvent(quint8(0x41U | (((byte >> bit) & 1U) << 1U)));
        appendEvent(0x43U);
    }
    appendEvent(0x0bU);
    QJsonObject archive;
    archive.insert(QStringLiteral("schema"), QStringLiteral("lockstep-protocol-events-v3"));
    archive.insert(QStringLiteral("capture_id"), 79);
    archive.insert(QStringLiteral("timebase_hz"), 120'000'000);
    archive.insert(QStringLiteral("implemented_source_mask"), 0x19f);
    archive.insert(QStringLiteral("enabled_source_mask"), 0x19f);
    archive.insert(QStringLiteral("design_gap_mask"), 0x060);
    archive.insert(QStringLiteral("events"), events);
    QJsonObject sidecar;
    sidecar.insert(QStringLiteral("schema"), QStringLiteral("lockstep-capture-sidecar-v3"));
    sidecar.insert(QStringLiteral("sample_rate_hz"), 120'000'000);
    sidecar.insert(QStringLiteral("window_start_index"), 0);
    sidecar.insert(QStringLiteral("protocol_events"), QStringLiteral("evidence/protocol_events.json"));
    return writeTextFile(QDir(evidencePath).filePath(QStringLiteral("capture_sidecar.json")),
                         QString::fromUtf8(QJsonDocument(sidecar).toJson(QJsonDocument::Compact))) &&
        writeTextFile(QDir(evidencePath).filePath(QStringLiteral("protocol_events.json")),
                      QString::fromUtf8(QJsonDocument(archive).toJson(QJsonDocument::Compact)));
}

bool writeSparseJtagFixture(const QString& taskRootPath)
{
    const QString evidencePath = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(evidencePath)) return false;
    QJsonArray events;
    quint32 sequence = 0U;
    quint64 tick = 400'000ULL;
    const auto appendCycle = [&](const bool tms, const bool tdi, const bool tdo) {
        const quint16 state = quint16(0x0a10U | (tms ? 0x20U : 0U) |
                                      (tdi ? 0x40U : 0U) | (tdo ? 0x80U : 0U));
        QJsonObject event;
        event.insert(QStringLiteral("timestamp_ticks"), QString::number(tick));
        event.insert(QStringLiteral("capture_id"), 80);
        event.insert(QStringLiteral("local_sequence"), static_cast<qint64>(sequence++));
        event.insert(QStringLiteral("protocol_id"), 7);
        event.insert(QStringLiteral("event_type"), 1);
        event.insert(QStringLiteral("source_kind"), 0);
        event.insert(QStringLiteral("flags"), 1);
        event.insert(QStringLiteral("event_reason_mask"), 0x80);
        event.insert(QStringLiteral("payload_hex"),
                     QStringLiteral("%1%2").arg(state & 0xffU, 2, 16, QLatin1Char('0'))
                                                .arg((state >> 8U) & 0xffU, 2, 16, QLatin1Char('0')));
        events.append(event);
        tick += 55ULL;
    };
    for (int cycle = 0; cycle < 5; ++cycle) appendCycle(true, false, false);
    appendCycle(false, false, false);
    appendCycle(true, false, false);
    appendCycle(false, false, false);
    appendCycle(false, false, false);
    constexpr quint8 tdiData = 0xa5U;
    constexpr quint8 tdoData = 0x3cU;
    for (int bit = 0; bit < 8; ++bit) {
        appendCycle(bit == 7, ((tdiData >> bit) & 1U) != 0U, ((tdoData >> bit) & 1U) != 0U);
    }
    appendCycle(true, false, false);
    appendCycle(false, false, false);
    QJsonObject archive;
    archive.insert(QStringLiteral("schema"), QStringLiteral("lockstep-protocol-events-v3"));
    archive.insert(QStringLiteral("capture_id"), 80);
    archive.insert(QStringLiteral("timebase_hz"), 120'000'000);
    archive.insert(QStringLiteral("implemented_source_mask"), 0x19f);
    archive.insert(QStringLiteral("enabled_source_mask"), 0x19f);
    archive.insert(QStringLiteral("design_gap_mask"), 0x060);
    archive.insert(QStringLiteral("events"), events);
    QJsonObject sidecar;
    sidecar.insert(QStringLiteral("schema"), QStringLiteral("lockstep-capture-sidecar-v3"));
    sidecar.insert(QStringLiteral("sample_rate_hz"), 120'000'000);
    sidecar.insert(QStringLiteral("window_start_index"), 0);
    sidecar.insert(QStringLiteral("protocol_events"), QStringLiteral("evidence/protocol_events.json"));
    return writeTextFile(QDir(evidencePath).filePath(QStringLiteral("capture_sidecar.json")),
                         QString::fromUtf8(QJsonDocument(sidecar).toJson(QJsonDocument::Compact))) &&
        writeTextFile(QDir(evidencePath).filePath(QStringLiteral("protocol_events.json")),
                      QString::fromUtf8(QJsonDocument(archive).toJson(QJsonDocument::Compact)));
}

bool writeRawUartFixture(const QString& taskRootPath)
{
    const QString waveformPath = QDir(taskRootPath).filePath(QStringLiteral("waveform"));
    const QString evidencePath = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(waveformPath) || !QDir().mkpath(evidencePath)) return false;
    QString bits(1024, QLatin1Char('0'));
    setPackedValue(&bits, 512, 1, 1U);
    setPackedValue(&bits, 513, 1, 1U);
    setPackedValue(&bits, 768, 1, 1U);
    QString vcd = QStringLiteral(
        "$timescale 1 ps $end\n$scope module logic $end\n"
        "$var wire 1024 ! lockstep_trace_sample [1023:0] $end\n"
        "$upscope $end\n$enddefinitions $end\n");
    constexpr int bitSamples = 9;
    constexpr int startSample = 2;
    constexpr quint8 value = 0x55U;
    for (int sample = 0; sample < 110; ++sample) {
        bool tx = true;
        if (sample >= startSample && sample < startSample + bitSamples) {
            tx = false;
        } else if (sample >= startSample + bitSamples &&
                   sample < startSample + bitSamples * 9) {
            const int dataBit = (sample - startSample - bitSamples) / bitSamples;
            tx = ((value >> dataBit) & 1U) != 0U;
        }
        setPackedValue(&bits, 512, 1, tx ? 1U : 0U);
        vcd += QStringLiteral("#%1\nb%2 !\n").arg(sample * 1'000'000).arg(bits);
    }
    const QString schema = QStringLiteral(
        "{\"schema_version\":\"2.0\",\"sample_width\":1024,"
        "\"physical_channels\":1024}\n");
    const QString sidecar = QStringLiteral(
        "{\"schema\":\"lockstep-capture-sidecar-v2\","
        "\"sample_rate_hz\":1000000}\n");
    return writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture.vcd")), vcd) &&
        writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture_schema.json")), schema) &&
        writeTextFile(QDir(evidencePath).filePath(QStringLiteral("capture_sidecar.json")), sidecar);
}

bool writeRawUartNegativeFixture(const QString& taskRootPath)
{
    const QString waveformPath = QDir(taskRootPath).filePath(QStringLiteral("waveform"));
    const QString evidencePath = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(waveformPath) || !QDir().mkpath(evidencePath)) return false;
    constexpr int bitSamples = 9;
    constexpr int sampleCount = 480;
    QList<bool> tx(sampleCount, true);
    QList<bool> rx(sampleCount, true);
    const auto addFrame = [](QList<bool>* const line, const int start, const quint8 data,
                             const bool stopHigh, const int bitCount = 10) {
        for (int sample = start; sample < line->size() && sample < start + bitCount * bitSamples;
             ++sample) {
            const int bit = (sample - start) / bitSamples;
            bool level = false;
            if (bit >= 1 && bit <= 8) level = ((data >> (bit - 1)) & 1U) != 0U;
            if (bit == 9) level = stopHigh;
            (*line)[sample] = level;
        }
    };
    addFrame(&tx, 2, 0x33U, false);
    addFrame(&tx, 110, 0x00U, false);
    addFrame(&tx, 220, 0xa6U, true);
    addFrame(&rx, 330, 0x5aU, true);
    addFrame(&tx, 455, 0xc3U, true, 3);

    QString bits(1024, QLatin1Char('0'));
    setPackedValue(&bits, 512, 1, 1U);
    setPackedValue(&bits, 513, 1, 1U);
    setPackedValue(&bits, 768, 1, 1U);
    QString vcd = QStringLiteral(
        "$timescale 1 ps $end\n$scope module logic $end\n"
        "$var wire 1024 ! lockstep_trace_sample [1023:0] $end\n"
        "$upscope $end\n$enddefinitions $end\n");
    for (int sample = 0; sample < sampleCount; ++sample) {
        setPackedValue(&bits, 512, 1, tx.at(sample));
        setPackedValue(&bits, 513, 1, rx.at(sample));
        vcd += QStringLiteral("#%1\nb%2 !\n").arg(sample * 1'000'000).arg(bits);
    }
    const QString schema = QStringLiteral(
        "{\"schema_version\":\"2.0\",\"sample_width\":1024,"
        "\"physical_channels\":1024}\n");
    const QString sidecar = QStringLiteral(
        "{\"schema\":\"lockstep-capture-sidecar-v2\","
        "\"sample_rate_hz\":1000000}\n");
    return writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture.vcd")), vcd) &&
        writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture_schema.json")), schema) &&
        writeTextFile(QDir(evidencePath).filePath(QStringLiteral("capture_sidecar.json")), sidecar);
}

bool writeRawI2cReadFixture(const QString& taskRootPath)
{
    const QString waveformPath = QDir(taskRootPath).filePath(QStringLiteral("waveform"));
    if (!QDir().mkpath(waveformPath)) return false;
    QString bits(1024, QLatin1Char('0'));
    setPackedValue(&bits, 608, 1, 1U);
    setPackedValue(&bits, 609, 1, 1U);
    setPackedValue(&bits, 782, 1, 1U);
    QString vcd = QStringLiteral(
        "$timescale 1 ns $end\n$scope module logic $end\n"
        "$var wire 1024 ! lockstep_trace_sample [1023:0] $end\n"
        "$upscope $end\n$enddefinitions $end\n");
    int time = 0;
    const auto emitSample = [&]() {
        vcd += QStringLiteral("#%1\nb%2 !\n").arg(time).arg(bits);
        time += 10;
    };
    emitSample();
    setPackedValue(&bits, 609, 1, 0U);
    emitSample();
    const auto clockBit = [&](const bool sda) {
        setPackedValue(&bits, 608, 1, 0U);
        setPackedValue(&bits, 609, 1, sda);
        emitSample();
        setPackedValue(&bits, 608, 1, 1U);
        emitSample();
    };
    for (const quint8 byte : {quint8(0xa1U), quint8(0x5aU)}) {
        for (int bit = 7; bit >= 0; --bit) clockBit(((byte >> bit) & 1U) != 0U);
        clockBit(byte == 0xa1U ? false : true);
    }
    setPackedValue(&bits, 608, 1, 0U);
    setPackedValue(&bits, 609, 1, 0U);
    emitSample();
    setPackedValue(&bits, 608, 1, 1U);
    emitSample();
    setPackedValue(&bits, 609, 1, 1U);
    emitSample();
    const QString schema = QStringLiteral(
        "{\"schema_version\":\"2.0\",\"sample_width\":1024,"
        "\"physical_channels\":1024}\n");
    return writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture.vcd")), vcd) &&
        writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture_schema.json")), schema);
}

bool writeObservableSpiModesFixture(const QString& taskRootPath)
{
    const QString waveformPath = QDir(taskRootPath).filePath(QStringLiteral("waveform"));
    if (!QDir().mkpath(waveformPath)) return false;
    QString bits(1024, QLatin1Char('0'));
    setPackedValue(&bits, 547, 1, 1U);
    setPackedValue(&bits, 781, 1, 1U);
    QString vcd = QStringLiteral(
        "$timescale 1 ns $end\n$scope module logic $end\n"
        "$var wire 1024 ! lockstep_trace_sample [1023:0] $end\n"
        "$upscope $end\n$enddefinitions $end\n");
    int time = 0;
    const auto emitSample = [&]() {
        vcd += QStringLiteral("#%1\nb%2 !\n").arg(time).arg(bits);
        time += 10;
    };
    emitSample();
    for (int mode = 0; mode < 4; ++mode) {
        const bool cpol = (mode & 2) != 0;
        const bool cpha = (mode & 1) != 0;
        const bool sampleOnRising = cpol == cpha;
        setPackedValue(&bits, 544, 1, cpol);
        setPackedValue(&bits, 550, 2, mode);
        setPackedValue(&bits, 547, 1, 1U);
        emitSample();
        setPackedValue(&bits, 547, 1, 0U);
        emitSample();
        const quint8 tx = quint8(0xa0U | mode);
        const quint8 rx = quint8(0x50U | mode);
        for (int bit = 7; bit >= 0; --bit) {
            setPackedValue(&bits, 545, 1, (tx >> bit) & 1U);
            setPackedValue(&bits, 546, 1, (rx >> bit) & 1U);
            setPackedValue(&bits, 544, 1, sampleOnRising ? 0U : 1U);
            emitSample();
            setPackedValue(&bits, 544, 1, sampleOnRising ? 1U : 0U);
            emitSample();
            if ((sampleOnRising ? true : false) != cpol) {
                setPackedValue(&bits, 544, 1, cpol);
                emitSample();
            }
        }
        setPackedValue(&bits, 547, 1, 1U);
        emitSample();
    }
    const QString schema = QStringLiteral(
        "{\"schema_version\":\"2.0\",\"sample_width\":1024,"
        "\"physical_channels\":1024,\"spi_mode_hint_available\":true}\n");
    return writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture.vcd")), vcd) &&
        writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture_schema.json")), schema);
}

bool writeSparseMergedSpiFixture(const QString& taskRootPath)
{
    if (!writeObservableSpiModesFixture(taskRootPath)) return false;
    const QString evidencePath = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(evidencePath)) return false;
    QJsonArray events;
    for (int bit = 0; bit < 32; ++bit) {
        QJsonObject event;
        event.insert(QStringLiteral("timestamp_ticks"), QString::number(40U + quint64(bit) * 28U));
        event.insert(QStringLiteral("capture_id"), 86);
        event.insert(QStringLiteral("local_sequence"), bit);
        event.insert(QStringLiteral("protocol_id"), 2);
        event.insert(QStringLiteral("event_type"), 1);
        event.insert(QStringLiteral("source_kind"), 0);
        event.insert(QStringLiteral("flags"), 0);
        event.insert(QStringLiteral("event_reason_mask"), 4);
        const quint8 state = quint8(0x21U | ((bit & 1) != 0 ? 0x02U : 0U));
        event.insert(QStringLiteral("payload_hex"),
                     QStringLiteral("%1").arg(state, 2, 16, QLatin1Char('0')));
        events.append(event);
    }
    QJsonObject archive{{QStringLiteral("schema"), QStringLiteral("lockstep-protocol-events-v3")},
                        {QStringLiteral("capture_id"), 86},
                        {QStringLiteral("timebase_hz"), 1'000'000'000},
                        {QStringLiteral("implemented_source_mask"), 0x19f},
                        {QStringLiteral("enabled_source_mask"), 0x19f},
                        {QStringLiteral("design_gap_mask"), 0x060},
                        {QStringLiteral("events"), events}};
    QJsonObject sidecar{{QStringLiteral("schema"), QStringLiteral("lockstep-capture-sidecar-v3")},
                        {QStringLiteral("sample_rate_hz"), 1'000'000'000},
                        {QStringLiteral("actual_sample_count"), 1000},
                        {QStringLiteral("window_start_index"), 0},
                        {QStringLiteral("window_end_index"), QStringLiteral("999")},
                        {QStringLiteral("protocol_events"), QStringLiteral("evidence/protocol_events.json")}};
    return writeTextFile(QDir(evidencePath).filePath(QStringLiteral("capture_sidecar.json")),
                         QString::fromUtf8(QJsonDocument(sidecar).toJson(QJsonDocument::Compact))) &&
        writeTextFile(QDir(evidencePath).filePath(QStringLiteral("protocol_events.json")),
                      QString::fromUtf8(QJsonDocument(archive).toJson(QJsonDocument::Compact)));
}

struct JtagCycleFixture final {
    bool tms = false;
    bool tdi = false;
    bool tdo = false;
};

QList<JtagCycleFixture> jtagBoundaryCycles()
{
    QList<JtagCycleFixture> cycles;
    const auto cycle = [&cycles](const bool tms, const bool tdi = false, const bool tdo = false) {
        cycles.append({tms, tdi, tdo});
    };
    for (int index = 0; index < 5; ++index) cycle(true);
    cycle(false); cycle(true); cycle(true); cycle(false); cycle(false);
    cycle(true, true, false); cycle(true); cycle(false);
    cycle(true); cycle(false); cycle(false);
    cycle(false, true, false); cycle(true, false, true);
    cycle(false); cycle(false); cycle(true); cycle(false);
    cycle(true, true, true); cycle(true); cycle(false);
    cycle(true); cycle(false); cycle(false); cycle(true, false, true); cycle(true); cycle(false);
    cycle(true); cycle(true); cycle(false); cycle(false); cycle(false, true, true);
    return cycles;
}

bool writeVcdJtagBoundaryFixture(const QString& taskRootPath)
{
    const QString waveformPath = QDir(taskRootPath).filePath(QStringLiteral("waveform"));
    if (!QDir().mkpath(waveformPath)) return false;
    QString bits(1024, QLatin1Char('0'));
    setPackedValue(&bits, 776, 1, 1U);
    QString vcd = QStringLiteral(
        "$timescale 1 ns $end\n$scope module logic $end\n"
        "$var wire 1024 ! lockstep_trace_sample [1023:0] $end\n"
        "$upscope $end\n$enddefinitions $end\n");
    int time = 0;
    const auto emitSample = [&]() {
        vcd += QStringLiteral("#%1\nb%2 !\n").arg(time).arg(bits);
        time += 10;
    };
    emitSample();
    for (const JtagCycleFixture& cycle : jtagBoundaryCycles()) {
        setPackedValue(&bits, 736, 1, 0U);
        setPackedValue(&bits, 737, 1, cycle.tms);
        setPackedValue(&bits, 738, 1, cycle.tdi);
        setPackedValue(&bits, 739, 1, cycle.tdo);
        emitSample();
        setPackedValue(&bits, 736, 1, 1U);
        emitSample();
    }
    const QString schema = QStringLiteral(
        "{\"schema_version\":\"2.0\",\"sample_width\":1024,"
        "\"physical_channels\":1024}\n");
    return writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture.vcd")), vcd) &&
        writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture_schema.json")), schema);
}

bool writeSparseJtagBoundaryFixture(const QString& taskRootPath)
{
    if (!writeWideProtocolFixture(taskRootPath)) return false;
    const QString evidencePath = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(evidencePath)) return false;
    QJsonArray events;
    quint64 timestamp = 10'000U;
    int sequence = 0;
    for (const JtagCycleFixture& cycle : jtagBoundaryCycles()) {
        const quint16 state = quint16(0x0a10U | (cycle.tms ? 0x20U : 0U) |
                                      (cycle.tdi ? 0x40U : 0U) | (cycle.tdo ? 0x80U : 0U));
        QJsonObject event;
        event.insert(QStringLiteral("timestamp_ticks"), QString::number(timestamp));
        event.insert(QStringLiteral("capture_id"), 81);
        event.insert(QStringLiteral("local_sequence"), sequence++);
        event.insert(QStringLiteral("protocol_id"), 7);
        event.insert(QStringLiteral("event_type"), 1);
        event.insert(QStringLiteral("source_kind"), 0);
        event.insert(QStringLiteral("flags"), 1);
        event.insert(QStringLiteral("event_reason_mask"), 0x80);
        event.insert(QStringLiteral("payload_hex"),
                     QStringLiteral("%1%2").arg(state & 0xffU, 2, 16, QLatin1Char('0'))
                                                .arg((state >> 8U) & 0xffU, 2, 16, QLatin1Char('0')));
        events.append(event);
        timestamp += 55U;
    }
    QJsonObject archive{{QStringLiteral("schema"), QStringLiteral("lockstep-protocol-events-v3")},
                        {QStringLiteral("capture_id"), 81},
                        {QStringLiteral("timebase_hz"), 120'000'000},
                        {QStringLiteral("implemented_source_mask"), 0x19f},
                        {QStringLiteral("enabled_source_mask"), 0x19f},
                        {QStringLiteral("design_gap_mask"), 0x060},
                        {QStringLiteral("events"), events}};
    QJsonObject sidecar{{QStringLiteral("schema"), QStringLiteral("lockstep-capture-sidecar-v3")},
                        {QStringLiteral("sample_rate_hz"), 120'000'000},
                        {QStringLiteral("window_start_index"), 0},
                        {QStringLiteral("protocol_events"), QStringLiteral("evidence/protocol_events.json")}};
    return writeTextFile(QDir(evidencePath).filePath(QStringLiteral("capture_sidecar.json")),
                         QString::fromUtf8(QJsonDocument(sidecar).toJson(QJsonDocument::Compact))) &&
        writeTextFile(QDir(evidencePath).filePath(QStringLiteral("protocol_events.json")),
                      QString::fromUtf8(QJsonDocument(archive).toJson(QJsonDocument::Compact)));
}

bool writeTrustedScalarOrderFixture(const QString& taskRootPath)
{
    const QString waveformPath = QDir(taskRootPath).filePath(QStringLiteral("waveform"));
    if (!QDir().mkpath(waveformPath)) return false;
    const QHash<int, QString> anchors = {
        {0, QStringLiteral("sample_abs_index[0]")},
        {32, QStringLiteral("ahb_haddr[0]")},
        {417, QStringLiteral("ahb_htrans[0]")},
        {512, QStringLiteral("UART_TX")},
        {544, QStringLiteral("SPI_SCLK")},
        {608, QStringLiteral("I2C_SCL")},
        {736, QStringLiteral("JTAG_TCK")},
        {1023, QStringLiteral("NOELV_1024_RESERVED[239]")}
    };
    QString vcd = QStringLiteral(
        "$version\n  host export\n$end\n$timescale 1 ns $end\n$scope module logic $end\n");
    for (int channel = 0; channel < 1024; ++channel) {
        const QString reference = anchors.value(
            channel, QStringLiteral("product_semantic_%1").arg(channel));
        vcd += QStringLiteral("$var wire 1 s%1 %2 $end\n").arg(channel).arg(reference);
    }
    vcd += QStringLiteral("$upscope $end\n$enddefinitions $end\n#0\n");
    for (int channel = 0; channel < 1024; ++channel) {
        vcd += QStringLiteral("0s%1\n").arg(channel);
    }
    vcd += QStringLiteral("#10\n1s506\n");
    const QString schema = QStringLiteral(
        "{\"schema_version\":\"2.0\",\"sample_width\":1024,"
        "\"physical_channels\":1024}\n");
    return writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture.vcd")), vcd) &&
        writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture_schema.json")), schema);
}

bool expect(bool condition, const QString& message)
{
    if (!condition) {
        QTextStream(stderr) << "FAIL: " << message << '\n';
    }
    return condition;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    QTemporaryDir tempDir(lockstepTestTemporaryTemplate(QStringLiteral("trace_analysis")));
    if (!expect(tempDir.isValid(), QStringLiteral("temporary directory is valid"))) {
        return 1;
    }

    const QString taskRootPath = tempDir.path();

    lockstep::waveform_viewer::WaveformTraceViewer viewer;
    const lockstep::waveform_viewer::WaveformViewModel emptyModel = viewer.loadTask(taskRootPath);
    if (!expect(emptyModel.groups.size() == 9, QStringLiteral("M11 exposes 9 protocol bundles without VCD"))) {
        return 1;
    }
    for (const lockstep::waveform_viewer::WaveformGroupView& group : emptyModel.groups) {
        if (!expect(!group.fields.isEmpty(), QStringLiteral("every default protocol bundle has collapsed member fields"))) {
            return 1;
        }
    }

    if (!expect(writeWideProtocolFixture(taskRootPath) && writeSparseEventFixture(taskRootPath),
                QStringLiteral("1024-bit trace and sparse event fixtures can be written"))) {
        return 1;
    }

    const lockstep::waveform_viewer::WaveformViewModel missingAnalysisModel = viewer.loadTask(taskRootPath);
    if (!expect(missingAnalysisModel.hasVcd && !missingAnalysisModel.hasAnalysis, QStringLiteral("M11 detects VCD without analysis"))) {
        return 1;
    }
    if (!expect(missingAnalysisModel.status == QStringLiteral("analysis_missing"), QStringLiteral("M11 marks missing analysis"))) {
        return 1;
    }

    lockstep::error_handling::ErrorRegistry registry;
    lockstep::protocol_analyzer::ProtocolAnalyzer analyzer;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest request;
    request.taskRootPath = taskRootPath;
    request.taskId = QStringLiteral("trace_test");
    request.errorRegistry = &registry;
    request.reportDiagnosticsToErrorRegistry = true;

    const lockstep::protocol_analyzer::ProtocolAnalysisResult result = analyzer.analyzeTask(request);
    if (!expect(result.success, QStringLiteral("analysis succeeds"))) {
        QTextStream(stderr) << result.errorMessage << '\n';
        return 1;
    }
    if (!expect(QFileInfo::exists(result.analysisPath), QStringLiteral("analysis file exists"))) {
        return 1;
    }

    const QJsonArray groups = result.analysis.value(QStringLiteral("groups")).toArray();
    if (!expect(groups.size() == 9, QStringLiteral("analysis has 9 protocol groups"))) {
        return 1;
    }
    bool sparseUartSeen = false;
    for (const QJsonValue& value : result.analysis.value(QStringLiteral("protocol_events")).toArray()) {
        const QJsonObject event = value.toObject();
        sparseUartSeen = sparseUartSeen ||
            (event.value(QStringLiteral("group_id")).toString() == QStringLiteral("uart") &&
             event.value(QStringLiteral("type")).toString() == QStringLiteral("raw_line_event") &&
             event.value(QStringLiteral("start_time")).toInteger() == 250);
    }
    if (!expect(sparseUartSeen, QStringLiteral("sparse raw UART event joins the VCD timeline"))) {
        return 1;
    }
    QTemporaryDir wrappedTimelineTask(lockstepTestTemporaryTemplate(QStringLiteral("wrapped_timeline")));
    if (!expect(wrappedTimelineTask.isValid() &&
                    writeWideProtocolFixture(wrappedTimelineTask.path()) &&
                    writeSparseEventFixture(wrappedTimelineTask.path(), 0xfffffff0U,
                                            0x1'00000010ULL),
                QStringLiteral("wrapped global timestamp fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest wrappedTimelineRequest;
    wrappedTimelineRequest.taskRootPath = wrappedTimelineTask.path();
    wrappedTimelineRequest.taskId = QStringLiteral("wrapped_timeline_test");
    wrappedTimelineRequest.reportDiagnosticsToErrorRegistry = false;
    const auto wrappedTimelineResult = analyzer.analyzeTask(wrappedTimelineRequest);
    bool wrappedTimelineAligned = false;
    for (const QJsonValue& value :
         wrappedTimelineResult.analysis.value(QStringLiteral("protocol_events")).toArray()) {
        const QJsonObject event = value.toObject();
        wrappedTimelineAligned = wrappedTimelineAligned ||
            (event.value(QStringLiteral("type")).toString() == QStringLiteral("raw_line_event") &&
             event.value(QStringLiteral("start_time")).toInteger() == 32);
    }
    if (!expect(wrappedTimelineResult.success && wrappedTimelineAligned,
                QStringLiteral("64-bit timestamp aligns across the 32-bit sample-index wrap"))) return 1;

    QTemporaryDir invalidTimelineTask(lockstepTestTemporaryTemplate(QStringLiteral("invalid_timeline")));
    if (!expect(invalidTimelineTask.isValid() &&
                    writeWideProtocolFixture(invalidTimelineTask.path()) &&
                    writeSparseEventFixture(invalidTimelineTask.path(), 1000U, 999U),
                QStringLiteral("invalid pre-window timestamp fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest invalidTimelineRequest;
    invalidTimelineRequest.taskRootPath = invalidTimelineTask.path();
    invalidTimelineRequest.taskId = QStringLiteral("invalid_timeline_test");
    invalidTimelineRequest.reportDiagnosticsToErrorRegistry = false;
    const auto invalidTimelineResult = analyzer.analyzeTask(invalidTimelineRequest);
    if (!expect(!invalidTimelineResult.success &&
                    invalidTimelineResult.status == QStringLiteral("failed"),
                QStringLiteral("event before the VCD window fails time-axis validation"))) return 1;

    for (const bool complete : {true, false}) {
        QTemporaryDir wrapUartTask(lockstepTestTemporaryTemplate(
            complete ? QStringLiteral("wrap_uart_complete") : QStringLiteral("wrap_uart_truncated")));
        if (!expect(wrapUartTask.isValid() && writeWideProtocolFixture(wrapUartTask.path()) &&
                        writeSparseUartWrapFixture(wrapUartTask.path(), complete),
                    QStringLiteral("post-wrap UART fixture is writable"))) return 1;
        lockstep::protocol_analyzer::ProtocolAnalysisRequest request;
        request.taskRootPath = wrapUartTask.path();
        request.taskId = complete ? QStringLiteral("wrap_uart_complete_test")
                                  : QStringLiteral("wrap_uart_truncated_test");
        request.reportDiagnosticsToErrorRegistry = false;
        const auto wrapResult = analyzer.analyzeTask(request);
        bool matchingFrame = false;
        for (const QJsonValue& value :
             wrapResult.analysis.value(QStringLiteral("protocol_events")).toArray()) {
            const QJsonObject event = value.toObject();
            const QJsonObject fields = event.value(QStringLiteral("fields")).toObject();
            matchingFrame = matchingFrame ||
                (event.value(QStringLiteral("type")).toString() == QStringLiteral("uart_frame") &&
                 fields.value(QStringLiteral("source")).toString() ==
                     QStringLiteral("sparse_timestamp_edges") &&
                 fields.value(QStringLiteral("complete")).toBool() == complete &&
                 fields.value(QStringLiteral("truncated")).toBool() != complete);
        }
        if (!expect(wrapResult.success && matchingFrame,
                    complete ? QStringLiteral("post-wrap UART frame completes in aligned epoch")
                             : QStringLiteral("post-wrap UART frame truncates at aligned window end"))) return 1;
    }

    QTemporaryDir roundingTask(lockstepTestTemporaryTemplate(QStringLiteral("sparse_rounding")));
    if (!expect(roundingTask.isValid() && writeSparseRoundingFixture(roundingTask.path()),
                QStringLiteral("120 MHz sparse rounding fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest roundingRequest;
    roundingRequest.taskRootPath = roundingTask.path();
    roundingRequest.taskId = QStringLiteral("sparse_rounding_test");
    roundingRequest.reportDiagnosticsToErrorRegistry = false;
    const auto roundingResult = analyzer.analyzeTask(roundingRequest);
    bool roundedExactly = false;
    for (const QJsonValue& value :
         roundingResult.analysis.value(QStringLiteral("protocol_events")).toArray()) {
        const QJsonObject event = value.toObject();
        roundedExactly = roundedExactly ||
            (event.value(QStringLiteral("type")).toString() == QStringLiteral("raw_line_event") &&
             event.value(QStringLiteral("start_time")).toInteger() == 16'667);
    }
    if (!expect(roundingResult.success && roundedExactly,
                QStringLiteral("120 MHz tick 2 rounds to exactly 16667 ps like VCD"))) return 1;
    for (const QJsonValue& value : groups) {
        const QJsonObject group = value.toObject();
        const QString id = group.value(QStringLiteral("id")).toString();
        if (id == QStringLiteral("eth") || id == QStringLiteral("usb")) {
            if (!expect(group.value(QStringLiteral("status")).toString() == QStringLiteral("unavailable") &&
                            group.value(QStringLiteral("reason")).toString().contains(
                                QStringLiteral("unavailable/design_gap")),
                        QStringLiteral("event design_gap mask forces ETH/USB unavailable"))) {
                return 1;
            }
        }
    }

    lockstep::error_handling::ErrorEvent recoveryEvent;
    recoveryEvent.code = QStringLiteral("CAPTURE_RECOVERY_FAILED");
    recoveryEvent.source = QStringLiteral("Sampling");
    recoveryEvent.message = QStringLiteral("capture recovery failed");
    lockstep::error_handling::ErrorRecord recoveryRecord;
    QString error;
    if (!expect(registry.appendTaskError(taskRootPath, recoveryEvent, &recoveryRecord, &error),
                QStringLiteral("diagnostic can be appended to M14"))) {
        QTextStream(stderr) << error << '\n';
        return 1;
    }
    if (!expect(recoveryRecord.context.value(QStringLiteral("suggestion")).toString()
                    .contains(QStringLiteral("PL 复位")),
                QStringLiteral("machine-readable diagnostic suggestion reaches ErrorRegistry"))) {
        return 1;
    }
    QList<lockstep::error_handling::ErrorRecord> records;
    if (!expect(registry.loadTaskErrors(taskRootPath, &records, &error) && !records.isEmpty(),
                QStringLiteral("M14 task errors can be loaded"))) {
        QTextStream(stderr) << error << '\n';
        return 1;
    }

    const lockstep::waveform_viewer::WaveformViewModel model = viewer.loadTask(taskRootPath);
    if (!expect(model.hasVcd && model.hasAnalysis, QStringLiteral("M11 sees VCD and analysis"))) {
        return 1;
    }
    if (!expect(model.groups.size() == 9, QStringLiteral("M11 exposes 9 display groups"))) {
        return 1;
    }
    if (!expect(!model.samples.isEmpty(), QStringLiteral("M11 exposes real 1024-bit VCD samples"))) {
        return 1;
    }
    if (!expect(model.timeRangeText.startsWith(QStringLiteral("0 .. ")),
                QStringLiteral("VCD timescale is applied to the shared axis"))) {
        return 1;
    }
    const lockstep::waveform_viewer::WaveformGroupView& ahbGroup = model.groups.first();
    if (!expect(!ahbGroup.fields.isEmpty() && ahbGroup.fields.first().width == 32,
                QStringLiteral("same-name AHB address bits are merged into one hexadecimal bus"))) {
        return 1;
    }
    if (!expect(!ahbGroup.transactions.isEmpty() &&
                    ahbGroup.transactions.first().contains(QStringLiteral("AHB WRITE")) &&
                    ahbGroup.transactions.first().contains(QStringLiteral("0x80001000")),
                QStringLiteral("AHB activity is decoded into a hexadecimal event on the shared timeline"))) {
        return 1;
    }
    if (!expect(!model.keyBehaviors.isEmpty(), QStringLiteral("M11 exposes key behaviors"))) {
        return 1;
    }
    if (!expect(model.keyBehaviors.join(QLatin1Char('\n')).contains(QStringLiteral("AHB WRITE")),
                QStringLiteral("protocol page receives group transactions, not only mismatch events"))) {
        return 1;
    }

    const QJsonArray protocolEvents =
        result.analysis.value(QStringLiteral("protocol_events")).toArray();
    QSet<QString> decodedGroups;
    QStringList eventSummaries;
    int ahbEventCount = 0;
    int i2cEventCount = 0;
    bool hasI2cAckEvent = false;
    bool realSpiModeUnavailable = false;
    for (const QJsonValue& value : protocolEvents) {
        const QJsonObject event = value.toObject();
        const QString groupId = event.value(QStringLiteral("group_id")).toString();
        decodedGroups.insert(groupId);
        eventSummaries.append(event.value(QStringLiteral("summary")).toString());
        if (groupId == QStringLiteral("ahb")) ++ahbEventCount;
        if (groupId == QStringLiteral("i2c")) ++i2cEventCount;
        hasI2cAckEvent = hasI2cAckEvent || event.value(QStringLiteral("type")).toString() == QStringLiteral("i2c_ack");
        const QJsonObject fields = event.value(QStringLiteral("fields")).toObject();
        realSpiModeUnavailable = realSpiModeUnavailable ||
            (groupId == QStringLiteral("spi") &&
             fields.value(QStringLiteral("mode_available")).toBool(true) == false &&
             !fields.contains(QStringLiteral("mode")));
    }
    for (const QString& required : {QStringLiteral("ahb"), QStringLiteral("uart"),
                                    QStringLiteral("spi"), QStringLiteral("can"),
                                    QStringLiteral("i2c"), QStringLiteral("eth"),
                                    QStringLiteral("jtag")}) {
        if (!expect(decodedGroups.contains(required),
                    QStringLiteral("fixture decodes %1 events").arg(required))) return 1;
    }
    if (!expect(!decodedGroups.contains(QStringLiteral("usb")),
                QStringLiteral("USB design gap never produces synthetic transactions"))) return 1;
    if (!expect(ahbEventCount == 2 &&
                    eventSummaries.join(QLatin1Char('\n')).contains(QStringLiteral("0x80001000")) &&
                    eventSummaries.join(QLatin1Char('\n')).contains(QStringLiteral("0x80002000")),
                QStringLiteral("AHB back-to-back address and data phases remain paired")) ||
                !expect(i2cEventCount >= 2 &&
                    eventSummaries.join(QLatin1Char('\n')).contains(QStringLiteral("REPEATED_START")),
                QStringLiteral("I2C repeated START preserves the preceding segment"))) return 1;
    if (!expect(hasI2cAckEvent, QStringLiteral("I2C ACK/NACK is emitted as a first-class event")) ||
        !expect(realSpiModeUnavailable,
                QStringLiteral("real SPI probe does not invent unavailable CPOL/CPHA mode"))) return 1;
    const QJsonArray keyBehaviors = result.analysis.value(QStringLiteral("key_behaviors")).toArray();
    if (!expect(keyBehaviors.size() >= protocolEvents.size(),
                QStringLiteral("key_behaviors includes protocol events, not only mismatch")) ||
        !expect(!keyBehaviors.isEmpty() &&
                    keyBehaviors.first().toObject().value(QStringLiteral("start_time")).toInteger() <=
                    keyBehaviors.last().toObject().value(QStringLiteral("start_time")).toInteger(),
                QStringLiteral("key_behaviors are ordered by capture time"))) return 1;

    QTemporaryDir rawUartTask(lockstepTestTemporaryTemplate(QStringLiteral("raw_uart")));
    if (!expect(rawUartTask.isValid() && writeRawUartFixture(rawUartTask.path()),
                QStringLiteral("raw UART fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest rawUartRequest;
    rawUartRequest.taskRootPath = rawUartTask.path();
    rawUartRequest.taskId = QStringLiteral("raw_uart_test");
    rawUartRequest.reportDiagnosticsToErrorRegistry = false;
    const auto rawUartResult = analyzer.analyzeTask(rawUartRequest);
    bool rawUartDecoded = false;
    for (const QJsonValue& value : rawUartResult.analysis.value(QStringLiteral("protocol_events")).toArray()) {
        const QJsonObject event = value.toObject();
        const QJsonObject fields = event.value(QStringLiteral("fields")).toObject();
        rawUartDecoded = rawUartDecoded ||
            (event.value(QStringLiteral("group_id")).toString() == QStringLiteral("uart") &&
             fields.value(QStringLiteral("data")).toString() == QStringLiteral("0x55") &&
             !fields.value(QStringLiteral("frame_error")).toBool());
    }
    if (!expect(rawUartResult.success && rawUartDecoded,
                QStringLiteral("raw 115200 8N1 UART is decoded from hardware TX samples"))) return 1;
    const auto rawUartModel = viewer.loadTask(rawUartTask.path());
    if (!expect(rawUartModel.timeRangeText == QStringLiteral("0 .. 109000 ns"),
                QStringLiteral("picosecond UI axis truncates the final three digits to ns")) ||
        !expect(!rawUartModel.samples.isEmpty() && rawUartModel.samples.last().time == 109000,
                QStringLiteral("sample and event coordinates use the same ns scale"))) return 1;

    QTemporaryDir uartNegativeTask(lockstepTestTemporaryTemplate(QStringLiteral("uart_negative")));
    if (!expect(uartNegativeTask.isValid() && writeRawUartNegativeFixture(uartNegativeTask.path()),
                QStringLiteral("UART negative fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest uartNegativeRequest;
    uartNegativeRequest.taskRootPath = uartNegativeTask.path();
    uartNegativeRequest.taskId = QStringLiteral("uart_negative_test");
    uartNegativeRequest.reportDiagnosticsToErrorRegistry = false;
    const auto uartNegativeResult = analyzer.analyzeTask(uartNegativeRequest);
    bool framingError = false;
    bool breakSeen = false;
    bool recoveredTx = false;
    bool rxSeen = false;
    bool truncated = false;
    for (const QJsonValue& value :
         uartNegativeResult.analysis.value(QStringLiteral("protocol_events")).toArray()) {
        const QJsonObject event = value.toObject();
        if (event.value(QStringLiteral("type")).toString() != QStringLiteral("uart_frame")) continue;
        const QJsonObject fields = event.value(QStringLiteral("fields")).toObject();
        const QString direction = fields.value(QStringLiteral("direction")).toString();
        const QString data = fields.value(QStringLiteral("data")).toString();
        framingError = framingError || (direction == QStringLiteral("TX") &&
            data == QStringLiteral("0x33") && fields.value(QStringLiteral("frame_error")).toBool() &&
            !fields.value(QStringLiteral("break")).toBool() &&
            event.value(QStringLiteral("severity")).toString() == QStringLiteral("error"));
        breakSeen = breakSeen || (direction == QStringLiteral("TX") &&
            data == QStringLiteral("0x00") && fields.value(QStringLiteral("break")).toBool());
        recoveredTx = recoveredTx || (direction == QStringLiteral("TX") &&
            data == QStringLiteral("0xA6") && !fields.value(QStringLiteral("frame_error")).toBool());
        rxSeen = rxSeen || (direction == QStringLiteral("RX") &&
            data == QStringLiteral("0x5A") && !fields.value(QStringLiteral("frame_error")).toBool());
        truncated = truncated || (direction == QStringLiteral("TX") &&
            !fields.value(QStringLiteral("complete")).toBool(true) &&
            fields.value(QStringLiteral("truncated")).toBool() &&
            event.value(QStringLiteral("severity")).toString() == QStringLiteral("warning"));
    }
    if (!expect(uartNegativeResult.success && framingError && breakSeen && recoveredTx && rxSeen && truncated,
                QStringLiteral("UART reports framing error, break, recovery, RX, and EOF truncation"))) return 1;

    QTemporaryDir rawI2cTask(lockstepTestTemporaryTemplate(QStringLiteral("raw_i2c_read")));
    if (!expect(rawI2cTask.isValid() && writeRawI2cReadFixture(rawI2cTask.path()),
                QStringLiteral("raw I2C read fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest rawI2cRequest;
    rawI2cRequest.taskRootPath = rawI2cTask.path();
    rawI2cRequest.taskId = QStringLiteral("raw_i2c_read_test");
    rawI2cRequest.reportDiagnosticsToErrorRegistry = false;
    const auto rawI2cResult = analyzer.analyzeTask(rawI2cRequest);
    int rawAckCount = 0;
    bool finalReadNack = false;
    bool cleanReadTransfer = false;
    for (const QJsonValue& value : rawI2cResult.analysis.value(QStringLiteral("protocol_events")).toArray()) {
        const QJsonObject event = value.toObject();
        const QJsonObject fields = event.value(QStringLiteral("fields")).toObject();
        if (event.value(QStringLiteral("type")).toString() == QStringLiteral("i2c_ack")) {
            ++rawAckCount;
            finalReadNack = finalReadNack ||
                (!fields.value(QStringLiteral("ack")).toBool() &&
                 fields.value(QStringLiteral("normal_final_read_nack")).toBool() &&
                 event.value(QStringLiteral("severity")).toString().isEmpty());
        }
        cleanReadTransfer = cleanReadTransfer ||
            (event.value(QStringLiteral("type")).toString() == QStringLiteral("i2c_transfer") &&
             fields.value(QStringLiteral("operation")).toString() == QStringLiteral("read") &&
             event.value(QStringLiteral("severity")).toString().isEmpty());
    }
    if (!expect(rawI2cResult.success && rawAckCount == 2 && finalReadNack && cleanReadTransfer,
                QStringLiteral("raw SDA supplies ACK state and final read NACK is normal"))) return 1;

    QTemporaryDir precedenceTask(lockstepTestTemporaryTemplate(QStringLiteral("sparse_precedence")));
    if (!expect(precedenceTask.isValid() && writeSparsePrecedenceFixture(precedenceTask.path()),
                QStringLiteral("mixed VCD and incomplete sparse fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest precedenceRequest;
    precedenceRequest.taskRootPath = precedenceTask.path();
    precedenceRequest.taskId = QStringLiteral("sparse_precedence_test");
    precedenceRequest.reportDiagnosticsToErrorRegistry = false;
    const auto precedenceResult = analyzer.analyzeTask(precedenceRequest);
    bool vcdUartPreserved = false;
    bool truncatedSparseUart = false;
    bool incompleteI2cSegment = false;
    int preservedI2cAcks = 0;
    for (const QJsonValue& value :
         precedenceResult.analysis.value(QStringLiteral("protocol_events")).toArray()) {
        const QJsonObject event = value.toObject();
        const QJsonObject fields = event.value(QStringLiteral("fields")).toObject();
        vcdUartPreserved = vcdUartPreserved ||
            (event.value(QStringLiteral("type")).toString() == QStringLiteral("uart_frame") &&
             fields.value(QStringLiteral("source")).toString() == QStringLiteral("rtl_frame_hint"));
        truncatedSparseUart = truncatedSparseUart ||
            (event.value(QStringLiteral("type")).toString() == QStringLiteral("uart_frame") &&
             fields.value(QStringLiteral("truncated")).toBool());
        incompleteI2cSegment = incompleteI2cSegment ||
            (event.value(QStringLiteral("type")).toString() == QStringLiteral("i2c_segment") &&
             !fields.value(QStringLiteral("complete")).toBool(true));
        if (event.value(QStringLiteral("type")).toString() == QStringLiteral("i2c_ack")) {
            ++preservedI2cAcks;
        }
    }
    if (!expect(precedenceResult.success && vcdUartPreserved && truncatedSparseUart &&
                    incompleteI2cSegment && preservedI2cAcks > 0,
                QStringLiteral("incomplete sparse events preserve VCD UART and I2C ACK evidence"))) return 1;

    QTemporaryDir sparseUartTask(lockstepTestTemporaryTemplate(QStringLiteral("sparse_uart")));
    if (!expect(sparseUartTask.isValid() && writeWideProtocolFixture(sparseUartTask.path()) &&
                    writeSparseUartMarkerFixture(sparseUartTask.path()),
                QStringLiteral("sparse UART marker fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest sparseUartRequest;
    sparseUartRequest.taskRootPath = sparseUartTask.path();
    sparseUartRequest.taskId = QStringLiteral("sparse_uart_test");
    sparseUartRequest.reportDiagnosticsToErrorRegistry = false;
    const auto sparseUartResult = analyzer.analyzeTask(sparseUartRequest);
    QByteArray sparseUartText;
    bool sparseUartFrameError = false;
    for (const QJsonValue& value : sparseUartResult.analysis.value(QStringLiteral("protocol_events")).toArray()) {
        const QJsonObject event = value.toObject();
        const QJsonObject fields = event.value(QStringLiteral("fields")).toObject();
        if (event.value(QStringLiteral("group_id")).toString() != QStringLiteral("uart") ||
            event.value(QStringLiteral("type")).toString() != QStringLiteral("uart_frame") ||
            fields.value(QStringLiteral("direction")).toString() != QStringLiteral("TX") ||
            fields.value(QStringLiteral("source")).toString() != QStringLiteral("sparse_timestamp_edges")) {
            continue;
        }
        bool ok = false;
        const int byte = fields.value(QStringLiteral("data")).toString().mid(2).toInt(&ok, 16);
        if (ok) sparseUartText.append(static_cast<char>(byte));
        sparseUartFrameError = sparseUartFrameError || fields.value(QStringLiteral("frame_error")).toBool();
    }
    if (!expect(sparseUartResult.success && sparseUartText == QByteArrayLiteral("PROGRAM_RUN_DONE\n") &&
                    !sparseUartFrameError,
                QStringLiteral("sparse timestamped UART edges reconstruct PROGRAM_RUN_DONE"))) {
        QTextStream(stderr) << "decoded=" << sparseUartText.toHex()
                            << " frame_error=" << sparseUartFrameError << '\n';
        return 1;
    }
    if (!expect(sparseUartResult.analysis.value(QStringLiteral("uart_tx_text")).toString()
                        .contains(QStringLiteral("PROGRAM_RUN_DONE")) &&
                    sparseUartResult.analysis.value(
                        QStringLiteral("program_done_marker_detected")).toBool(),
                QStringLiteral("protocol analysis exposes the UART program completion marker"))) return 1;

    QTemporaryDir sparseUartNegativeTask(
        lockstepTestTemporaryTemplate(QStringLiteral("sparse_uart_negative")));
    if (!expect(sparseUartNegativeTask.isValid() &&
                    writeWideProtocolFixture(sparseUartNegativeTask.path()) &&
                    writeSparseUartNegativeFixture(sparseUartNegativeTask.path()),
                QStringLiteral("sparse UART negative fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest sparseUartNegativeRequest;
    sparseUartNegativeRequest.taskRootPath = sparseUartNegativeTask.path();
    sparseUartNegativeRequest.taskId = QStringLiteral("sparse_uart_negative_test");
    sparseUartNegativeRequest.reportDiagnosticsToErrorRegistry = false;
    const auto sparseUartNegativeResult = analyzer.analyzeTask(sparseUartNegativeRequest);
    QSet<QString> sparseUartBehaviors;
    for (const QJsonValue& value :
         sparseUartNegativeResult.analysis.value(QStringLiteral("protocol_events")).toArray()) {
        const QJsonObject event = value.toObject();
        if (event.value(QStringLiteral("type")).toString() != QStringLiteral("uart_frame")) continue;
        const QJsonObject fields = event.value(QStringLiteral("fields")).toObject();
        const QString direction = fields.value(QStringLiteral("direction")).toString();
        const QString data = fields.value(QStringLiteral("data")).toString();
        if (direction == QStringLiteral("TX") && data == QStringLiteral("0x33") &&
            fields.value(QStringLiteral("frame_error")).toBool()) {
            sparseUartBehaviors.insert(QStringLiteral("framing"));
        }
        if (direction == QStringLiteral("TX") && fields.value(QStringLiteral("break")).toBool()) {
            sparseUartBehaviors.insert(QStringLiteral("break"));
        }
        if (direction == QStringLiteral("TX") && data == QStringLiteral("0xA6") &&
            !fields.value(QStringLiteral("frame_error")).toBool()) {
            sparseUartBehaviors.insert(QStringLiteral("recovery"));
        }
        if (direction == QStringLiteral("RX") && data == QStringLiteral("0x5A") &&
            !fields.value(QStringLiteral("frame_error")).toBool()) {
            sparseUartBehaviors.insert(QStringLiteral("rx"));
        }
        if (direction == QStringLiteral("TX") && fields.value(QStringLiteral("truncated")).toBool() &&
            !fields.value(QStringLiteral("complete")).toBool(true)) {
            sparseUartBehaviors.insert(QStringLiteral("truncated"));
        }
    }
    if (!expect(sparseUartNegativeResult.success && sparseUartBehaviors ==
                    QSet<QString>({QStringLiteral("framing"), QStringLiteral("break"),
                                   QStringLiteral("recovery"), QStringLiteral("rx"),
                                   QStringLiteral("truncated")}),
                QStringLiteral("sparse UART matches raw error, recovery, RX, and EOF semantics"))) return 1;

    QTemporaryDir sparseSpiTask(lockstepTestTemporaryTemplate(QStringLiteral("sparse_spi")));
    if (!expect(sparseSpiTask.isValid() && writeWideProtocolFixture(sparseSpiTask.path()) &&
                    writeSparseSpiFixture(sparseSpiTask.path()),
                QStringLiteral("sparse SPI fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest sparseSpiRequest;
    sparseSpiRequest.taskRootPath = sparseSpiTask.path();
    sparseSpiRequest.taskId = QStringLiteral("sparse_spi_test");
    sparseSpiRequest.reportDiagnosticsToErrorRegistry = false;
    const auto sparseSpiResult = analyzer.analyzeTask(sparseSpiRequest);
    bool sparseSpiDecoded = false;
    for (const QJsonValue& value : sparseSpiResult.analysis.value(QStringLiteral("protocol_events")).toArray()) {
        const QJsonObject event = value.toObject();
        const QJsonObject fields = event.value(QStringLiteral("fields")).toObject();
        sparseSpiDecoded = sparseSpiDecoded ||
            (event.value(QStringLiteral("group_id")).toString() == QStringLiteral("spi") &&
             event.value(QStringLiteral("type")).toString() == QStringLiteral("spi_transfer") &&
             fields.value(QStringLiteral("source")).toString() == QStringLiteral("sparse_rising_edges") &&
             fields.value(QStringLiteral("bit_count")).toInt() == 32 &&
             fields.value(QStringLiteral("tx")).toString() == QStringLiteral("0xA5C33C5A") &&
             fields.value(QStringLiteral("rx")).toString() == QStringLiteral("0x3CC3A55A") &&
             fields.value(QStringLiteral("tx_data")).toString() == QStringLiteral("0xA5C33C5A") &&
             fields.value(QStringLiteral("rx_data")).toString() == QStringLiteral("0x3CC3A55A"));
    }
    if (!expect(sparseSpiResult.success && sparseSpiDecoded,
                QStringLiteral("sparse SPI rising edges reconstruct a 32-bit transfer"))) return 1;

    QTemporaryDir spiModesTask(lockstepTestTemporaryTemplate(QStringLiteral("spi_modes")));
    if (!expect(spiModesTask.isValid() && writeObservableSpiModesFixture(spiModesTask.path()),
                QStringLiteral("observable SPI mode fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest spiModesRequest;
    spiModesRequest.taskRootPath = spiModesTask.path();
    spiModesRequest.taskId = QStringLiteral("spi_modes_test");
    spiModesRequest.reportDiagnosticsToErrorRegistry = false;
    const auto spiModesResult = analyzer.analyzeTask(spiModesRequest);
    QSet<int> decodedSpiModes;
    for (const QJsonValue& value : spiModesResult.analysis.value(QStringLiteral("protocol_events")).toArray()) {
        const QJsonObject event = value.toObject();
        const QJsonObject fields = event.value(QStringLiteral("fields")).toObject();
        if (event.value(QStringLiteral("type")).toString() == QStringLiteral("spi_transfer") &&
            fields.value(QStringLiteral("mode_available")).toBool() &&
            fields.value(QStringLiteral("bit_count")).toInt() == 8) {
            const int mode = fields.value(QStringLiteral("mode")).toInt(-1);
            if (fields.value(QStringLiteral("tx")).toString() ==
                    QStringLiteral("0x%1").arg(0xa0 + mode, 2, 16, QLatin1Char('0')).toUpper().replace(
                        QStringLiteral("0X"), QStringLiteral("0x")) &&
                fields.value(QStringLiteral("rx")).toString() ==
                    QStringLiteral("0x%1").arg(0x50 + mode, 2, 16, QLatin1Char('0')).toUpper().replace(
                        QStringLiteral("0X"), QStringLiteral("0x"))) {
                decodedSpiModes.insert(mode);
            }
        }
    }
    if (!expect(spiModesResult.success && decodedSpiModes == QSet<int>({0, 1, 2, 3}),
                QStringLiteral("observable SPI modes 0-3 select their actual CPOL/CPHA edge"))) {
        QTextStream(stderr) << QJsonDocument(spiModesResult.analysis).toJson(QJsonDocument::Compact) << '\n';
        return 1;
    }

    QTemporaryDir mergedSpiTask(lockstepTestTemporaryTemplate(QStringLiteral("merged_sparse_spi")));
    if (!expect(mergedSpiTask.isValid() && writeSparseMergedSpiFixture(mergedSpiTask.path()),
                QStringLiteral("merged sparse SPI fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest mergedSpiRequest;
    mergedSpiRequest.taskRootPath = mergedSpiTask.path();
    mergedSpiRequest.taskId = QStringLiteral("merged_sparse_spi_test");
    mergedSpiRequest.reportDiagnosticsToErrorRegistry = false;
    const auto mergedSpiResult = analyzer.analyzeTask(mergedSpiRequest);
    int vcdSpiTransfers = 0;
    int sparseSpiTransfers = 0;
    bool normalizedSparseSpi = false;
    for (const QJsonValue& value : mergedSpiResult.analysis.value(QStringLiteral("protocol_events")).toArray()) {
        const QJsonObject event = value.toObject();
        if (event.value(QStringLiteral("type")).toString() != QStringLiteral("spi_transfer")) continue;
        const QJsonObject fields = event.value(QStringLiteral("fields")).toObject();
        if (fields.value(QStringLiteral("source")).toString() == QStringLiteral("sparse_rising_edges")) {
            ++sparseSpiTransfers;
            normalizedSparseSpi = fields.contains(QStringLiteral("tx")) &&
                fields.contains(QStringLiteral("rx")) && fields.value(QStringLiteral("complete")).toBool();
        } else {
            ++vcdSpiTransfers;
        }
    }
    if (!expect(mergedSpiResult.success && vcdSpiTransfers == 4 && sparseSpiTransfers == 1 &&
                    normalizedSparseSpi,
                QStringLiteral("merged sparse SPI cannot replace four independent CS frames"))) return 1;

    QTemporaryDir sparseI2cTask(lockstepTestTemporaryTemplate(QStringLiteral("sparse_i2c")));
    if (!expect(sparseI2cTask.isValid() && writeWideProtocolFixture(sparseI2cTask.path()) &&
                    writeSparseI2cFixture(sparseI2cTask.path()),
                QStringLiteral("sparse I2C fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest sparseI2cRequest;
    sparseI2cRequest.taskRootPath = sparseI2cTask.path();
    sparseI2cRequest.taskId = QStringLiteral("sparse_i2c_test");
    sparseI2cRequest.reportDiagnosticsToErrorRegistry = false;
    const auto sparseI2cResult = analyzer.analyzeTask(sparseI2cRequest);
    bool sparseI2cDecoded = false;
    for (const QJsonValue& value : sparseI2cResult.analysis.value(QStringLiteral("protocol_events")).toArray()) {
        const QJsonObject event = value.toObject();
        const QJsonObject fields = event.value(QStringLiteral("fields")).toObject();
        const QJsonArray bytes = fields.value(QStringLiteral("bytes")).toArray();
        const QJsonArray acks = fields.value(QStringLiteral("acks")).toArray();
        sparseI2cDecoded = sparseI2cDecoded ||
            (event.value(QStringLiteral("group_id")).toString() == QStringLiteral("i2c") &&
             event.value(QStringLiteral("type")).toString() == QStringLiteral("i2c_transfer") &&
             fields.value(QStringLiteral("source")).toString() == QStringLiteral("sparse_scl_rising_edges") &&
             fields.value(QStringLiteral("address")).toString() == QStringLiteral("0x50") &&
             fields.value(QStringLiteral("operation")).toString() == QStringLiteral("write") &&
             fields.value(QStringLiteral("data")).toString() == QStringLiteral("0x5A") &&
             fields.value(QStringLiteral("ack_count")).toInt() == 2 &&
             fields.value(QStringLiteral("complete")).toBool() &&
             bytes.size() == 2 && bytes.at(0).toString() == QStringLiteral("0xA0") &&
             bytes.at(1).toString() == QStringLiteral("0x5A") &&
             acks.size() == 2 && acks.at(0).toString() == QStringLiteral("NACK") &&
             acks.at(1).toString() == QStringLiteral("NACK"));
    }
    if (!expect(sparseI2cResult.success && sparseI2cDecoded,
                QStringLiteral("sparse I2C edges reconstruct bytes and real NACKs"))) return 1;

    QTemporaryDir sparseJtagTask(lockstepTestTemporaryTemplate(QStringLiteral("sparse_jtag")));
    if (!expect(sparseJtagTask.isValid() && writeWideProtocolFixture(sparseJtagTask.path()) &&
                    writeSparseJtagFixture(sparseJtagTask.path()),
                QStringLiteral("sparse JTAG fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest sparseJtagRequest;
    sparseJtagRequest.taskRootPath = sparseJtagTask.path();
    sparseJtagRequest.taskId = QStringLiteral("sparse_jtag_test");
    sparseJtagRequest.reportDiagnosticsToErrorRegistry = false;
    const auto sparseJtagResult = analyzer.analyzeTask(sparseJtagRequest);
    bool sparseJtagDecoded = false;
    for (const QJsonValue& value : sparseJtagResult.analysis.value(QStringLiteral("protocol_events")).toArray()) {
        const QJsonObject event = value.toObject();
        const QJsonObject fields = event.value(QStringLiteral("fields")).toObject();
        sparseJtagDecoded = sparseJtagDecoded ||
            (event.value(QStringLiteral("group_id")).toString() == QStringLiteral("jtag") &&
             event.value(QStringLiteral("type")).toString() == QStringLiteral("jtag_scan") &&
             fields.value(QStringLiteral("source")).toString() == QStringLiteral("sparse_tap_cycles") &&
             fields.value(QStringLiteral("register")).toString() == QStringLiteral("dr") &&
             fields.value(QStringLiteral("bit_count")).toInt() == 8 &&
             fields.value(QStringLiteral("tdi_bits")).toString() == QStringLiteral("10100101") &&
             fields.value(QStringLiteral("tdo_bits")).toString() == QStringLiteral("00111100"));
    }
    if (!expect(sparseJtagResult.success && sparseJtagDecoded,
                QStringLiteral("sparse JTAG TAP cycles reconstruct a complete DR scan"))) return 1;

    QTemporaryDir vcdJtagBoundaryTask(lockstepTestTemporaryTemplate(QStringLiteral("vcd_jtag_boundary")));
    QTemporaryDir sparseJtagBoundaryTask(lockstepTestTemporaryTemplate(QStringLiteral("sparse_jtag_boundary")));
    if (!expect(vcdJtagBoundaryTask.isValid() && sparseJtagBoundaryTask.isValid() &&
                    writeVcdJtagBoundaryFixture(vcdJtagBoundaryTask.path()) &&
                    writeSparseJtagBoundaryFixture(sparseJtagBoundaryTask.path()),
                QStringLiteral("JTAG boundary fixtures are writable"))) return 1;
    const auto analyzeJtagBoundary = [&analyzer](const QString& path, const QString& id) {
        lockstep::protocol_analyzer::ProtocolAnalysisRequest request;
        request.taskRootPath = path;
        request.taskId = id;
        request.reportDiagnosticsToErrorRegistry = false;
        return analyzer.analyzeTask(request);
    };
    const auto vcdJtagBoundaryResult = analyzeJtagBoundary(
        vcdJtagBoundaryTask.path(), QStringLiteral("vcd_jtag_boundary_test"));
    const auto sparseJtagBoundaryResult = analyzeJtagBoundary(
        sparseJtagBoundaryTask.path(), QStringLiteral("sparse_jtag_boundary_test"));
    const auto scanSignatures = [](const QJsonObject& analysis) {
        QSet<QString> signatures;
        for (const QJsonValue& value : analysis.value(QStringLiteral("protocol_events")).toArray()) {
            const QJsonObject event = value.toObject();
            if (event.value(QStringLiteral("type")).toString() != QStringLiteral("jtag_scan")) continue;
            const QJsonObject fields = event.value(QStringLiteral("fields")).toObject();
            signatures.insert(QStringLiteral("%1:%2:%3:%4:%5")
                .arg(fields.value(QStringLiteral("register")).toString())
                .arg(fields.value(QStringLiteral("bit_count")).toInt())
                .arg(fields.value(QStringLiteral("tdi_bits")).toString())
                .arg(fields.value(QStringLiteral("complete")).toBool() ? 1 : 0)
                .arg(fields.value(QStringLiteral("truncated")).toBool() ? 1 : 0));
        }
        return signatures;
    };
    const QSet<QString> expectedJtagScans = {
        QStringLiteral("ir:1:1:1:0"), QStringLiteral("dr:3:101:1:0"),
        QStringLiteral("dr:1:0:1:0"), QStringLiteral("ir:1:1:0:1")};
    if (!expect(vcdJtagBoundaryResult.success && sparseJtagBoundaryResult.success &&
                    scanSignatures(vcdJtagBoundaryResult.analysis) == expectedJtagScans &&
                    scanSignatures(sparseJtagBoundaryResult.analysis) == expectedJtagScans,
                QStringLiteral("VCD and sparse JTAG agree on 1-bit, pause/resume, and EOF truncation"))) return 1;

    QTemporaryDir invalidEventTask(lockstepTestTemporaryTemplate(QStringLiteral("invalid_event")));
    const QString invalidEvidence = QDir(invalidEventTask.path()).filePath(QStringLiteral("evidence"));
    if (!expect(invalidEventTask.isValid() && writeWideProtocolFixture(invalidEventTask.path()) &&
                    QDir().mkpath(invalidEvidence) &&
                    writeTextFile(QDir(invalidEvidence).filePath(QStringLiteral("capture_sidecar.json")),
                                  QStringLiteral("{\"sample_rate_hz\":1000000000,"
                                                 "\"protocol_events\":\"evidence/missing.json\"}")),
                QStringLiteral("invalid sparse event fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest invalidEventRequest;
    invalidEventRequest.taskRootPath = invalidEventTask.path();
    invalidEventRequest.taskId = QStringLiteral("invalid_event_test");
    invalidEventRequest.reportDiagnosticsToErrorRegistry = false;
    const auto invalidEventResult = analyzer.analyzeTask(invalidEventRequest);
    if (!expect(!invalidEventResult.success && invalidEventResult.wroteAnalysis &&
                    invalidEventResult.status == QStringLiteral("failed") &&
                    invalidEventResult.analysis.value(QStringLiteral("status")).toString() ==
                        QStringLiteral("failed"),
                QStringLiteral("missing sparse event evidence fails protocol analysis"))) return 1;

    QTemporaryDir trustedOrderTask(lockstepTestTemporaryTemplate(QStringLiteral("trusted_scalar")));
    if (!expect(trustedOrderTask.isValid() &&
                    writeTrustedScalarOrderFixture(trustedOrderTask.path()),
                QStringLiteral("trusted product scalar-order fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest trustedOrderRequest;
    trustedOrderRequest.taskRootPath = trustedOrderTask.path();
    trustedOrderRequest.taskId = QStringLiteral("trusted_scalar_order_test");
    trustedOrderRequest.reportDiagnosticsToErrorRegistry = false;
    const auto trustedOrderResult = analyzer.analyzeTask(trustedOrderRequest);
    if (!expect(trustedOrderResult.success &&
                    trustedOrderResult.analysis.value(QStringLiteral("key_behaviors"))
                        .toArray().size() == 1,
                QStringLiteral("verified product generator anchors allow declaration-order mapping"))) {
        return 1;
    }

    QTextStream(stdout) << "PASS lockstep_trace_analysis_test\n";
    return 0;
}
