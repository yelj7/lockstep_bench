/**********************************************************
* 文件名: ft601_driver_bootstrap.h
* 日期: 2026-07-20
* 版本: 1.0
* 更新记录: 新增 Windows FT601 libusbK 启动绑定接口。
* 描述: 定义上位机启动阶段驱动绑定与产品 USB 自检结果。
**********************************************************/

#pragma once

#include <QString>

namespace lockstep::apps {

struct Ft601DriverBootstrapResult {
    bool success = false;
    bool connected = false;
    bool changed = false;
    QString status;
    QString message;
};

Ft601DriverBootstrapResult ensureFt601LibusbK(const QString& productExecutable);
int runElevatedFt601LibusbKBootstrap();

}  // namespace lockstep::apps
