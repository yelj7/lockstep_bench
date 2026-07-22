/**********************************************************
* 文件名: target_control_test.cpp
* 日期: 2026-07-22
* 版本: 1.1
* 更新记录: 增加普通运行与复位并运行的命令顺序回归。
* 描述: 验证非破坏性预检及目标启动控制合同。
**********************************************************/

#include <QCoreApplication>

#include <iostream>

#include "target_control.h"

namespace {

using namespace lockstep::target_control;

DebugResult successResult(const QString& operation, const QByteArray& data = QByteArray())
{
    DebugResult result;
    result.success = true;
    result.rawReturn = operation;
    result.data = data;
    return result;
}

class FakeDebugAccess final : public DebugAccess {
public:
    DebugResult connectTarget(const DebugProfile&) override { return successResult(QStringLiteral("connect")); }
    DebugResult disconnectTarget() override { return successResult(QStringLiteral("disconnect")); }
    DebugResult status() override { return successResult(QStringLiteral("status")); }
    DebugResult reset(const QString&) override
    {
        operations.append(QStringLiteral("reset"));
        if (resetSucceeds) return successResult(QStringLiteral("reset"));
        DebugResult result;
        result.errorMessage = QStringLiteral("reset rejected");
        return result;
    }
    DebugResult run(quint64) override
    {
        operations.append(QStringLiteral("run"));
        return successResult(QStringLiteral("run"));
    }
    DebugResult halt() override { return successResult(QStringLiteral("halt")); }

    DebugResult read(const quint64 address, const quint64 length) override
    {
        if (address != baseAddress || length != static_cast<quint64>(memory.size())) {
            DebugResult result;
            result.errorMessage = QStringLiteral("invalid read");
            return result;
        }
        QByteArray data = memory;
        if (corruptReadback && writeSeen) data[0] = static_cast<char>(data.at(0) ^ 1);
        return successResult(QStringLiteral("read"), data);
    }

    DebugResult write(const quint64 address, const QByteArray& data) override
    {
        if (address != baseAddress || data.size() != memory.size()) {
            DebugResult result;
            result.errorMessage = QStringLiteral("invalid write");
            return result;
        }
        writtenData = data;
        memory = data;
        writeSeen = true;
        return successResult(QStringLiteral("write"));
    }

    quint64 baseAddress = 0x40000000U;
    QByteArray memory = QByteArrayLiteral("ABCD");
    QByteArray writtenData;
    bool corruptReadback = false;
    bool writeSeen = false;
    bool resetSucceeds = true;
    QStringList operations;
};

bool expect(const bool condition, const char* const message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    DebugProfile profile;
    profile.ramBaseAddress = 0x40000000U;
    profile.resetStrategy = QStringLiteral("reset-halt");

    TargetConnectionService service;
    FakeDebugAccess passingAccess;
    const QByteArray original = passingAccess.memory;
    const PrecheckRecord passing = service.runPrecheck(passingAccess, profile);
    if (!expect(passing.state == PrecheckState::Passed,
                "non-destructive precheck should pass") ||
        !expect(passingAccess.writtenData == original && passingAccess.memory == original,
                "precheck must write back and preserve the original RAM bytes")) return 1;

    FakeDebugAccess corruptAccess;
    corruptAccess.corruptReadback = true;
    const PrecheckRecord corrupt = service.runPrecheck(corruptAccess, profile);
    if (!expect(corrupt.state == PrecheckState::Failed,
                "precheck must fail when write-back verification differs") ||
        !expect(!corrupt.errorMessage.isEmpty(),
                "failed precheck must report a concrete error")) return 1;

    ProgramController controller;
    ProgramImageInfo image;
    image.entryAddress = 0x40000000U;
    ReadbackVerifyRecord verified;
    verified.state = VerifyState::Passed;

    FakeDebugAccess normalRunAccess;
    const RunControlRecord normalRun = controller.runTarget(
        normalRunAccess, QStringLiteral("normal"), image, verified, false, profile.resetStrategy);
    if (!expect(normalRun.state == RunState::Running &&
                    normalRunAccess.operations == QStringList{QStringLiteral("run")},
                "normal run must not reset the target")) return 1;

    FakeDebugAccess resetRunAccess;
    const RunControlRecord resetRun = controller.runTarget(
        resetRunAccess, QStringLiteral("reset-run"), image, verified, true, profile.resetStrategy);
    if (!expect(resetRun.state == RunState::Running &&
                    resetRunAccess.operations ==
                        QStringList{QStringLiteral("reset"), QStringLiteral("run")},
                "reset-and-run must reset before run")) return 1;

    FakeDebugAccess failedResetAccess;
    failedResetAccess.resetSucceeds = false;
    const RunControlRecord failedReset = controller.runTarget(
        failedResetAccess, QStringLiteral("failed-reset"), image, verified, true, profile.resetStrategy);
    if (!expect(failedReset.state == RunState::Failed &&
                    failedResetAccess.operations == QStringList{QStringLiteral("reset")} &&
                    failedReset.errorMessage.contains(QStringLiteral("reset rejected")),
                "a failed reset must prevent run")) return 1;
    return 0;
}
