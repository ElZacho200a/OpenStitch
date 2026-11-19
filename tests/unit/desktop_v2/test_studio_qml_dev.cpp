// SPDX-License-Identifier: Apache-2.0
// QML externe + rechargement à chaud (BUILD-OPT-01.3). Headless, sans sleep ni pixels.
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "qml_dev_loader.hpp"

using namespace openstitch::desktop_v2::studio;

namespace {
void writeFile(const QString& path, const QString& text) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(text.toUtf8());
}
QString mainQml(const QString& marker) {
    return QStringLiteral("import QtQuick\nQtObject { property string marker: \"%1\" }\n")
        .arg(marker);
}
} // namespace

class QmlDevTest : public QObject {
    Q_OBJECT
private slots:
    void decision_is_embedded_by_default();
    void decision_never_external_in_release();
    void decision_requires_opt_in_and_main_qml();
    void build_flag_matches_ndebug();
    void reload_now_swaps_view_and_keeps_old_on_error();
    void watcher_reloads_after_file_change();
};

void QmlDevTest::decision_is_embedded_by_default() {
    const QmlSourceInputs none{};
    QVERIFY(!decideQmlSource(none).external);
    QCOMPARE(decideQmlSource(none).mainUrl.scheme(), QStringLiteral("qrc"));
    QmlSourceInputs noEnv;
    noEnv.capabilityCompiled = true;
    noEnv.defaultDir = QDir::tempPath();
    QVERIFY(!decideQmlSource(noEnv).external);
}

void QmlDevTest::decision_never_external_in_release() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("Main.qml")), mainQml(QStringLiteral("A")));
    QmlSourceInputs in;
    in.capabilityCompiled = true;
    in.releaseBuild = true;
    in.envValue = dir.path();
    in.defaultDir = dir.path();
    const auto d = decideQmlSource(in);
    QVERIFY(!d.external);
    QCOMPARE(d.mainUrl.scheme(), QStringLiteral("qrc"));
    in.envValue = QStringLiteral("1");
    QVERIFY(!decideQmlSource(in).external);
}

void QmlDevTest::decision_requires_opt_in_and_main_qml() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QmlSourceInputs in;
    in.capabilityCompiled = true;
    in.defaultDir = dir.path();
    in.envValue = QStringLiteral("1");
    QVERIFY(!decideQmlSource(in).external); // pas de Main.qml : repli embarqué
    writeFile(dir.filePath(QStringLiteral("Main.qml")), mainQml(QStringLiteral("A")));
    const auto on = decideQmlSource(in);
    QVERIFY(on.external);
    QVERIFY(on.mainUrl.isLocalFile());
    in.envValue = QStringLiteral("0"); // option pour le couper
    QVERIFY(!decideQmlSource(in).external);
    in.envValue = dir.path(); // un chemin explicite vaut opt-in
    QVERIFY(decideQmlSource(in).external);
    in.capabilityCompiled = false; // option CMake OFF : la variable ne suffit pas
    QVERIFY(!decideQmlSource(in).external);
}

void QmlDevTest::build_flag_matches_ndebug() {
    const auto in = qmlSourceInputsFromBuild();
#if defined(NDEBUG)
    QVERIFY(in.releaseBuild);
#else
    QVERIFY(!in.releaseBuild);
#endif
    // La capacité compilée (option CMake) va de pair avec un répertoire par défaut.
    QCOMPARE(in.capabilityCompiled, !in.defaultDir.isEmpty());
}

void QmlDevTest::reload_now_swaps_view_and_keeps_old_on_error() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto mainPath = dir.filePath(QStringLiteral("Main.qml"));
    writeFile(mainPath, mainQml(QStringLiteral("A")));
    QQmlApplicationEngine engine;
    const auto url = QUrl::fromLocalFile(mainPath);
    engine.load(url);
    QCOMPARE(engine.rootObjects().size(), 1);
    QCOMPARE(engine.rootObjects().first()->property("marker").toString(), QStringLiteral("A"));

    QmlDevReloader reloader(engine, url, dir.path());
    reloader.setWatching(false);

    // Une modification QML seule est visible sans toucher au C++.
    writeFile(mainPath, mainQml(QStringLiteral("B")));
    QVERIFY(reloader.reloadNow());
    QCOMPARE(engine.rootObjects().last()->property("marker").toString(), QStringLiteral("B"));
    QCOMPARE(reloader.reloadCount(), 1);

    // Erreur de syntaxe : l'ancienne vue reste, l'erreur est rapportée.
    QSignalSpy failed(&reloader, &QmlDevReloader::reloadFailed);
    writeFile(mainPath, QStringLiteral("import QtQuick\nQtObject { property string marker: \n"));
    QVERIFY(!reloader.reloadNow());
    QCOMPARE(failed.count(), 1);
    QVERIFY(!reloader.lastError().isEmpty());
    QCOMPARE(engine.rootObjects().last()->property("marker").toString(), QStringLiteral("B"));

    // Correction : retour à la normale.
    writeFile(mainPath, mainQml(QStringLiteral("C")));
    QVERIFY(reloader.reloadNow());
    QVERIFY(reloader.lastError().isEmpty());
    QCOMPARE(engine.rootObjects().last()->property("marker").toString(), QStringLiteral("C"));
}

void QmlDevTest::watcher_reloads_after_file_change() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto mainPath = dir.filePath(QStringLiteral("Main.qml"));
    writeFile(mainPath, mainQml(QStringLiteral("A")));
    QQmlApplicationEngine engine;
    const auto url = QUrl::fromLocalFile(mainPath);
    engine.load(url);
    QmlDevReloader reloader(engine, url, dir.path());
    QVERIFY(reloader.watching());
    QSignalSpy done(&reloader, &QmlDevReloader::reloaded);
    writeFile(mainPath, mainQml(QStringLiteral("W")));
    QVERIFY(done.wait(10000));
    QCOMPARE(engine.rootObjects().last()->property("marker").toString(), QStringLiteral("W"));
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QmlDevTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_studio_qml_dev.moc"
