// SPDX-License-Identifier: Apache-2.0
#include <QTest>

#include "wsl_path_converter.hpp"

using openstitch::desktop::WslPathConverter;

class WslPathConverterTest : public QObject {
    Q_OBJECT

private slots:
    void toWslConvertsDriveLetterPath();
    void toWslLeavesAlreadyWslPathUnchanged();
    void toWindowsConvertsMntPath();
    void toWindowsLeavesNonMntPathUnchanged();
    void roundTripPreservesPath();
};

void WslPathConverterTest::toWslConvertsDriveLetterPath() {
    QCOMPARE(WslPathConverter::toWsl(QStringLiteral(R"(C:\Users\foo\bar)")),
            QStringLiteral("/mnt/c/Users/foo/bar"));
    QCOMPARE(WslPathConverter::toWsl(QStringLiteral(R"(D:\OpenStitch\ai-jobs\job-1)")),
            QStringLiteral("/mnt/d/OpenStitch/ai-jobs/job-1"));
}

void WslPathConverterTest::toWslLeavesAlreadyWslPathUnchanged() {
    QCOMPARE(WslPathConverter::toWsl(QStringLiteral("/mnt/c/already/wsl")),
            QStringLiteral("/mnt/c/already/wsl"));
}

void WslPathConverterTest::toWindowsConvertsMntPath() {
    QCOMPARE(WslPathConverter::toWindows(QStringLiteral("/mnt/c/Users/foo/bar")),
            QStringLiteral(R"(C:\Users\foo\bar)"));
}

void WslPathConverterTest::toWindowsLeavesNonMntPathUnchanged() {
    QCOMPARE(WslPathConverter::toWindows(QStringLiteral("/home/user/venv/bin/python")),
            QStringLiteral("/home/user/venv/bin/python"));
}

void WslPathConverterTest::roundTripPreservesPath() {
    const QString original = QStringLiteral(R"(C:\Users\foo\bar\baz.png)");
    QCOMPARE(WslPathConverter::toWindows(WslPathConverter::toWsl(original)), original);
}

QTEST_MAIN(WslPathConverterTest)
#include "test_wsl_path_converter.moc"
