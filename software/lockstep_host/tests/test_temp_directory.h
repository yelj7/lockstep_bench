/**********************************************************
* 文件名: test_temp_directory.h
* 日期: 2026-07-20
* 版本: v1.0
* 更新记录: 初版，固定 Windows 测试临时目录到 D:\tmp
* 描述: 为上位机 C++ 测试生成仓库外 QTemporaryDir 模板
**********************************************************/

#ifndef LOCKSTEP_HOST_TESTS_TEST_TEMP_DIRECTORY_H_
#define LOCKSTEP_HOST_TESTS_TEST_TEMP_DIRECTORY_H_

#include <QDir>
#include <QString>

inline QString lockstepTestTemporaryTemplate(const QString& name)
{
    const QString root = QStringLiteral("D:/tmp/lockstep/tests");
    QDir().mkpath(root);
    return QDir(root).filePath(name + QStringLiteral("_XXXXXX"));
}

#endif  // LOCKSTEP_HOST_TESTS_TEST_TEMP_DIRECTORY_H_
