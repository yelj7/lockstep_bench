/**********************************************************
* 文件名: report_generator_test.cpp
* 日期: 2026-07-14
* 版本: v1.0
* 更新记录: 初版创建
* 描述: 验证测试报告结论、归档文件和可追溯性行为
**********************************************************/

#include <iostream>

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>

#include "report_generator.h"
#include "test_temp_directory.h"

namespace {

bool expect(const bool condition, const char* const message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

bool writeJson(const QString& path, const QJsonObject& object)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QSaveFile file(path);
    const QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Indented);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

bool writeBytes(const QString& path, const QByteArray& data)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

lockstep::reporting::ReportDocumentModel passingModel()
{
    using lockstep::reporting::EvidenceState;
    lockstep::reporting::ReportDocumentModel model;
    model.reportId = QStringLiteral("report_test_0001");
    model.taskId = QStringLiteral("task_0001");
    model.taskName = QStringLiteral("锁步验证任务");
    model.mode = QStringLiteral("test");
    model.requiredEvidence.programWrite.state = EvidenceState::Passed;
    model.requiredEvidence.readbackVerify.state = EvidenceState::Passed;
    model.requiredEvidence.runControl.state = EvidenceState::Passed;
    return model;
}

bool conclusionDistinguishesFailureFromMissing()
{
    using namespace lockstep::reporting;
    const ReportGenerator generator;
    QStringList reasons;

    ReportDocumentModel model = passingModel();
    ReportDiagnostic warning;
    warning.code = QStringLiteral("WARN_NON_BLOCKING");
    model.warnings.append(warning);
    if (!expect(generator.calculateConclusion(model, &reasons) == ReportConclusion::Pass,
                "warnings and missing optional records should not block pass")) {
        return false;
    }

    model.requiredEvidence.readbackVerify.state = EvidenceState::Failed;
    model.blockingErrors.clear();
    if (!expect(generator.calculateConclusion(model, &reasons) == ReportConclusion::Fail,
                "executed evidence failure should fail")) {
        return false;
    }

    model.requiredEvidence.readbackVerify.state = EvidenceState::Missing;
    if (!expect(generator.calculateConclusion(model, &reasons) == ReportConclusion::Incomplete,
                "missing evidence should be incomplete")) {
        return false;
    }

    ReportDiagnostic blocking;
    blocking.id = QStringLiteral("err_1");
    model.blockingErrors.append(blocking);
    model.requiredEvidence.readbackVerify.state = EvidenceState::Failed;
    return expect(generator.calculateConclusion(model, &reasons) == ReportConclusion::Blocked,
                  "blocking errors should take precedence") &&
        expect(reasons.contains(QStringLiteral("回读校验失败")),
               "blocked reports should retain evidence failure reasons");
}

bool inputDigestTracksFactsOnly()
{
    using namespace lockstep::reporting;
    const ReportGenerator generator;
    ReportDocumentModel model = passingModel();
    const QString digest = generator.calculateInputDigest(model);
    model.reportId = QStringLiteral("another_report");
    model.revision = 9;
    model.generatedAt = QStringLiteral("2027-01-01T00:00:00.000Z");
    if (!expect(generator.calculateInputDigest(model) == digest,
                "report identity should not change the input digest")) {
        return false;
    }
    model.requiredEvidence.runControl.state = EvidenceState::Failed;
    return expect(generator.calculateInputDigest(model) != digest,
                  "evidence changes should change the input digest");
}

bool diskModelBuilderUsesPersistedFacts()
{
    using namespace lockstep::reporting;
    QTemporaryDir taskRoot(lockstepTestTemporaryTemplate(QStringLiteral("report_generator")));
    if (!expect(taskRoot.isValid(), "disk model task directory should be available")) {
        return false;
    }
    QJsonArray artifacts;
    const auto artifact = [](const QString& id, const QString& path) {
        QJsonObject item;
        item.insert(QStringLiteral("artifact_id"), id);
        item.insert(QStringLiteral("kind"), QStringLiteral("evidence"));
        item.insert(QStringLiteral("relative_path"), path);
        item.insert(QStringLiteral("created_at"), QStringLiteral("2026-07-14T08:00:00.000Z"));
        return item;
    };
    artifacts.append(artifact(QStringLiteral("write"), QStringLiteral("evidence/program_write_record.json")));
    artifacts.append(artifact(QStringLiteral("verify"), QStringLiteral("evidence/readback_verify_record.json")));
    artifacts.append(artifact(QStringLiteral("run"), QStringLiteral("evidence/run_control_record.json")));
    artifacts.append(artifact(QStringLiteral("unsafe"), QStringLiteral("C:/private/secret.bin")));
    QJsonObject index;
    index.insert(QStringLiteral("artifacts"), artifacts);
    if (!writeJson(taskRoot.filePath(QStringLiteral("evidence/artifacts.json")), index)) {
        return expect(false, "artifact index should be writable");
    }

    QJsonObject write;
    write.insert(QStringLiteral("success"), true);
    write.insert(QStringLiteral("created_at"), QStringLiteral("2026-07-14T08:00:00.000Z"));
    QJsonArray segments;
    QJsonObject firstSegment;
    firstSegment.insert(QStringLiteral("length"), QStringLiteral("4"));
    QJsonObject secondSegment;
    secondSegment.insert(QStringLiteral("length"), QStringLiteral("6"));
    segments.append(firstSegment);
    segments.append(secondSegment);
    write.insert(QStringLiteral("segments"), segments);
    QJsonObject verify;
    verify.insert(QStringLiteral("state"), QStringLiteral("passed"));
    verify.insert(QStringLiteral("expected_length"), QStringLiteral("10"));
    verify.insert(QStringLiteral("actual_length"), QStringLiteral("10"));
    verify.insert(QStringLiteral("diff_count"), QStringLiteral("0"));
    QJsonObject run;
    run.insert(QStringLiteral("state"), QStringLiteral("running"));
    run.insert(QStringLiteral("entry_address"), QStringLiteral("0x80000000"));
    run.insert(QStringLiteral("raw_return"), QStringLiteral("running"));
    if (!writeJson(taskRoot.filePath(QStringLiteral("evidence/program_write_record.json")), write) ||
        !writeJson(taskRoot.filePath(QStringLiteral("evidence/readback_verify_record.json")), verify) ||
        !writeJson(taskRoot.filePath(QStringLiteral("evidence/run_control_record.json")), run)) {
        return expect(false, "persisted evidence should be writable");
    }

    ReportDocumentModel context;
    context.taskId = QStringLiteral("task_disk");
    context.programRelativePath = QStringLiteral("inputs/program/test.bin");
    context.resourceSnapshot.insert(QStringLiteral("profile_id"), QStringLiteral("board_1"));
    context.resourceSnapshot.insert(QStringLiteral("profile_path"), QStringLiteral("C:/private/profile.json"));
    writeBytes(taskRoot.filePath(context.programRelativePath), QByteArrayLiteral("first-image"));
    const ReportGenerator generator;
    const ReportDocumentModel loaded = generator.buildModelFromTask(taskRoot.path(), context);
    QStringList reasons;
    if (!expect(generator.calculateConclusion(loaded, &reasons) == ReportConclusion::Pass,
                "persisted successful evidence should pass after reload") ||
        !expect(loaded.requiredEvidence.programWrite.metrics.value(QStringLiteral("size_bytes")).toInt() == 10,
                "write metrics should come from persisted segments") ||
        !expect(loaded.artifacts.size() == 3, "unsafe artifacts should be rejected") ||
        !expect(!loaded.resourceSnapshot.contains(QStringLiteral("profile_path")),
                "resource physical paths should be rejected")) {
        return false;
    }

    const QString originalDigest = generator.calculateInputDigest(loaded);
    writeBytes(taskRoot.filePath(context.programRelativePath), QByteArrayLiteral("replaced-image"));
    const ReportDocumentModel imageChanged = generator.buildModelFromTask(taskRoot.path(), context);
    if (!expect(imageChanged.programSha256 != loaded.programSha256 &&
                    generator.calculateInputDigest(imageChanged) != originalDigest,
                "replacing the imported program image should make the report stale")) {
        return false;
    }
    verify.insert(QStringLiteral("state"), QStringLiteral("mismatch"));
    verify.insert(QStringLiteral("diff_count"), QStringLiteral("1"));
    writeJson(taskRoot.filePath(QStringLiteral("evidence/readback_verify_record.json")), verify);
    const ReportDocumentModel failed = generator.buildModelFromTask(taskRoot.path(), context);
    if (!expect(generator.calculateConclusion(failed, &reasons) == ReportConclusion::Fail,
                "external persisted mismatch should fail") ||
        !expect(generator.calculateInputDigest(failed) != originalDigest,
                "external evidence changes should make the input stale")) {
        return false;
    }
    verify.insert(QStringLiteral("state"), QStringLiteral("not_run"));
    run.insert(QStringLiteral("state"), QStringLiteral("not_run"));
    run.insert(QStringLiteral("raw_return"), QString());
    writeJson(taskRoot.filePath(QStringLiteral("evidence/readback_verify_record.json")), verify);
    writeJson(taskRoot.filePath(QStringLiteral("evidence/run_control_record.json")), run);
    const ReportDocumentModel notRun = generator.buildModelFromTask(taskRoot.path(), context);
    if (!expect(notRun.requiredEvidence.readbackVerify.state == EvidenceState::NotRun &&
                    notRun.requiredEvidence.runControl.state == EvidenceState::NotRun,
                "persisted not-run states should remain not-run") ||
        !expect(notRun.requiredEvidence.readbackVerify.summary.contains(QStringLiteral("尚未执行")) &&
                    notRun.requiredEvidence.runControl.summary.contains(QStringLiteral("尚未执行")),
                "not-run evidence summaries should not describe failures") ||
        !expect(generator.calculateConclusion(notRun, &reasons) == ReportConclusion::Incomplete,
                "not-run required evidence should be incomplete")) {
        return false;
    }
    QFile::remove(taskRoot.filePath(QStringLiteral("evidence/run_control_record.json")));
    const ReportDocumentModel missing = generator.buildModelFromTask(taskRoot.path(), context);
    return expect(missing.requiredEvidence.runControl.state == EvidenceState::Missing,
                  "registered but missing evidence should be missing, not not-run");
}

bool generatesVersionedJsonAndHtmlArchive()
{
    using namespace lockstep::reporting;
    QTemporaryDir taskRoot(lockstepTestTemporaryTemplate(QStringLiteral("report_persisted")));
    if (!expect(taskRoot.isValid(), "temporary task directory should be available")) {
        return false;
    }

    ReportDocumentModel first = passingModel();
    first.generatedAt = QStringLiteral("2026-07-14T08:00:00.000Z");
    first.requiredEvidence.programWrite.recordPath = QStringLiteral("evidence/program_write_record.json");
    first.requiredEvidence.readbackVerify.recordPath = QStringLiteral("evidence/readback_verify_record.json");
    first.requiredEvidence.runControl.recordPath = QStringLiteral("evidence/run_control_record.json");
    ReportDiagnostic warning;
    warning.code = QStringLiteral("WARN_HTML");
    warning.severity = QStringLiteral("warning");
    warning.source = QStringLiteral("test");
    warning.message = QStringLiteral("<script>alert('unsafe')</script>");
    warning.suggestion = QStringLiteral("review warning evidence");
    first.warnings.append(warning);
    QJsonObject unsafeArtifact;
    unsafeArtifact.insert(QStringLiteral("relative_path"), QStringLiteral("../private/secret.bin"));
    first.artifacts.append(unsafeArtifact);
    QJsonObject safeArtifact;
    safeArtifact.insert(QStringLiteral("artifact_id"), QStringLiteral("safe"));
    safeArtifact.insert(QStringLiteral("kind"), QStringLiteral("evidence"));
    safeArtifact.insert(QStringLiteral("relative_path"), QStringLiteral("evidence/safe.bin"));
    safeArtifact.insert(QStringLiteral("size_bytes"), 4);
    safeArtifact.insert(QStringLiteral("sha256"), QStringLiteral("abcd"));
    first.artifacts.append(safeArtifact);
    first.environment.insert(QStringLiteral("os"), QStringLiteral("TestOS"));
    first.resourceSnapshot.insert(QStringLiteral("profile_id"), QStringLiteral("board_1"));
    first.resourceSnapshot.insert(QStringLiteral("profile_path"), QStringLiteral("C:/private/profile.json"));

    const ReportGenerator generator;
    const ReportResult firstResult = generator.generateReport(taskRoot.path(), first);
    if (!expect(firstResult.success, "first report generation should succeed") ||
        !expect(QFileInfo::exists(taskRoot.filePath(QStringLiteral("reports/report.json"))),
                "latest json should exist") ||
        !expect(QFileInfo::exists(taskRoot.filePath(QStringLiteral("reports/report.html"))),
                "latest html should exist") ||
        !expect(QFileInfo::exists(taskRoot.filePath(QStringLiteral("reports/latest.json"))),
                "latest pointer should exist") ||
        !expect(QFileInfo::exists(taskRoot.filePath(
                    QStringLiteral("reports/versions/report_test_0001/manifest.json"))),
                "version manifest should exist")) {
        return false;
    }

    QFile jsonFile(taskRoot.filePath(QStringLiteral("reports/report.json")));
    if (!expect(jsonFile.open(QIODevice::ReadOnly), "latest json should be readable")) {
        return false;
    }
    const QJsonObject json = QJsonDocument::fromJson(jsonFile.readAll()).object();
    jsonFile.close();
    if (!expect(json.value(QStringLiteral("schema_version")).toString() == QStringLiteral("2.0"),
                "json should use schema 2.0") ||
        !expect(json.value(QStringLiteral("conclusion")).toString() == QStringLiteral("pass"),
                "json and calculated conclusion should match") ||
        !expect(!json.value(QStringLiteral("input_digest")).toString().isEmpty(),
                "json should contain an input digest") ||
        !expect(json.value(QStringLiteral("revision")).isDouble(),
                "revision should remain numeric") ||
        !expect(json.value(QStringLiteral("artifacts")).toArray().size() == 1 &&
                    json.value(QStringLiteral("artifacts")).toArray().first().toObject()
                        .value(QStringLiteral("relative_path")).toString() == QStringLiteral("evidence/safe.bin"),
                "unsafe public artifacts should be removed while safe artifacts remain") ||
        !expect(!json.value(QStringLiteral("resource_snapshot")).toObject().contains(QStringLiteral("profile_path")),
                "unsafe public resource fields should be removed")) {
        return false;
    }

    QFile htmlFile(taskRoot.filePath(QStringLiteral("reports/report.html")));
    if (!expect(htmlFile.open(QIODevice::ReadOnly), "latest html should be readable")) {
        return false;
    }
    const QByteArray html = htmlFile.readAll();
    htmlFile.close();
    if (!expect(html.contains("&lt;script&gt;"), "dynamic html should be escaped") ||
        !expect(!html.contains("https://") && !html.contains("http://"),
                "html should not use network resources") ||
        !expect(html.contains("TestOS") && html.contains("board_1") &&
                    html.contains("evidence/safe.bin") && html.contains("warning") &&
                    html.contains("review warning evidence"),
                "html should include environment, resources, artifacts and diagnostic detail")) {
        return false;
    }

    ReportDocumentModel second = first;
    second.reportId = QStringLiteral("report_test_0002");
    second.revision = 2;
    second.requiredEvidence.runControl.recordPath = QStringLiteral("C:/private/run.json");
    const ReportResult secondResult = generator.generateReport(taskRoot.path(), second);
    ReportDocumentModel loaded;
    ReportConclusion loadedConclusion = ReportConclusion::Incomplete;
    QStringList loadedReasons;
    QString loadError;
    const bool loadedOk = generator.loadLatestReport(
        taskRoot.path(), &loaded, &loadedConclusion, &loadedReasons, &loadError);
    QFile secondJson(taskRoot.filePath(QStringLiteral("reports/report.json")));
    if (!secondJson.open(QIODevice::ReadOnly)) {
        return expect(false, "second latest json should be readable");
    }
    const QJsonObject secondObject = QJsonDocument::fromJson(secondJson.readAll()).object();
    const QString serializedRunPath = secondObject.value(QStringLiteral("required_evidence")).toObject()
        .value(QStringLiteral("run_control")).toObject()
        .value(QStringLiteral("record_path")).toString();
    QFile manifestFile(taskRoot.filePath(QStringLiteral("reports/versions/report_test_0002/manifest.json")));
    if (!expect(manifestFile.open(QIODevice::ReadOnly), "version manifest should be readable")) {
        return false;
    }
    const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();
    const QJsonArray manifestEvidence = manifest.value(QStringLiteral("referenced_evidence")).toArray();
    if (!expect(manifestEvidence.size() == 3 &&
                    manifestEvidence.first().toObject().value(QStringLiteral("size_bytes")).isDouble(),
                "manifest should always contain numeric entries for three required evidence files")) {
        return false;
    }
    QFile latestBeforeFile(taskRoot.filePath(QStringLiteral("reports/report.json")));
    if (!expect(latestBeforeFile.open(QIODevice::ReadOnly), "latest report should be readable before rollback test")) {
        return false;
    }
    const QByteArray latestBefore = latestBeforeFile.readAll();
    latestBeforeFile.close();
    QDir().mkdir(taskRoot.filePath(QStringLiteral("reports/.previous_report.html")));
    ReportDocumentModel third = first;
    third.reportId = QStringLiteral("report_test_0003");
    third.revision = 3;
    const ReportResult failedPublish = generator.generateReport(taskRoot.path(), third);
    QFile latestAfterFile(taskRoot.filePath(QStringLiteral("reports/report.json")));
    if (!expect(latestAfterFile.open(QIODevice::ReadOnly), "latest report should remain readable after failed publish")) {
        return false;
    }
    const QByteArray latestAfter = latestAfterFile.readAll();
    latestAfterFile.close();

    return expect(secondResult.success, "second report generation should succeed") &&
        expect(QFileInfo::exists(taskRoot.filePath(
                   QStringLiteral("reports/versions/report_test_0001/report.json"))),
               "first version should remain after regeneration") &&
        expect(QFileInfo::exists(taskRoot.filePath(
                   QStringLiteral("reports/versions/report_test_0002/report.json"))),
               "second version should be archived") &&
        expect(serializedRunPath.isEmpty(), "absolute evidence paths should not be serialized") &&
        expect(loadedOk && loaded.reportId == QStringLiteral("report_test_0002") &&
                   loaded.revision == 2 && loadedConclusion == ReportConclusion::Pass,
               "latest report should round-trip through the public reader") &&
        expect(!failedPublish.success && latestAfter == latestBefore,
               "failed latest publication should preserve the previous report");
}

}  // namespace

int main()
{
    if (!conclusionDistinguishesFailureFromMissing()) {
        return 1;
    }
    if (!inputDigestTracksFactsOnly()) {
        return 1;
    }
    if (!diskModelBuilderUsesPersistedFacts()) {
        return 1;
    }
    return generatesVersionedJsonAndHtmlArchive() ? 0 : 1;
}
