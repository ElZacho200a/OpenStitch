// SPDX-License-Identifier: Apache-2.0
#include "qml_dev_loader.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QTemporaryDir>
#include <QWindow>

namespace openstitch::desktop_v2::studio {

namespace {
bool isOff(const QString& v) {
    const auto s = v.trimmed().toLower();
    return s == QLatin1String("0") || s == QLatin1String("off") || s == QLatin1String("false") ||
           s == QLatin1String("no");
}
bool isOn(const QString& v) {
    const auto s = v.trimmed().toLower();
    return s == QLatin1String("1") || s == QLatin1String("on") || s == QLatin1String("true") ||
           s == QLatin1String("yes");
}
QString describe(const QList<QQmlError>& errors) {
    QStringList lines;
    for (const auto& e : errors) {
        lines.push_back(e.toString());
    }
    return lines.join(QLatin1Char('\n'));
}
bool copyTree(const QString& from, const QString& to) {
    if (!QDir().mkpath(to)) {
        return false;
    }
    QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    const QDir src(from);
    while (it.hasNext()) {
        const auto path = it.next();
        const auto target = QDir(to).filePath(src.relativeFilePath(path));
        if (it.fileInfo().isDir()) {
            QDir().mkpath(target);
        } else {
            QFile::remove(target);
            if (!QFile::copy(path, target)) {
                return false;
            }
        }
    }
    return true;
}
} // namespace

QmlSourceDecision decideQmlSource(const QmlSourceInputs& in) {
    QmlSourceDecision d;
    d.mainUrl = QUrl(QStringLiteral("qrc:/studio/qml/Main.qml"));
    if (in.releaseBuild) {
        d.reason = QStringLiteral("build Release : QML embarque (garde-fou)");
        return d;
    }
    if (!in.capabilityCompiled) {
        d.reason = QStringLiteral("QML externe non compile (OPENSTITCH_STUDIO_DEV_QML=OFF)");
        return d;
    }
    const auto env = in.envValue.trimmed();
    if (env.isEmpty() || isOff(env)) {
        d.reason = env.isEmpty() ? QStringLiteral("QML externe non demande (variable absente)")
                                 : QStringLiteral("QML externe coupe par la variable");
        return d;
    }
    const QString dir = isOn(env) ? in.defaultDir : env;
    if (dir.isEmpty() || !QFileInfo::exists(QDir(dir).filePath(QStringLiteral("Main.qml")))) {
        d.reason =
            QStringLiteral("QML externe : Main.qml introuvable dans '%1', repli embarque").arg(dir);
        return d;
    }
    d.external = true;
    d.dir = QDir(dir).absolutePath();
    d.mainUrl = QUrl::fromLocalFile(QDir(d.dir).filePath(QStringLiteral("Main.qml")));
    d.reason = QStringLiteral("QML externe depuis '%1'").arg(d.dir);
    return d;
}

QmlSourceInputs qmlSourceInputsFromBuild() {
    QmlSourceInputs in;
#if defined(OPENSTITCH_STUDIO_DEV_QML_DIR)
    in.capabilityCompiled = true;
    in.defaultDir = QStringLiteral(OPENSTITCH_STUDIO_DEV_QML_DIR);
#endif
#if defined(NDEBUG)
    in.releaseBuild = true;
#endif
    in.envValue = qEnvironmentVariable(kQmlDevEnv);
    return in;
}

QmlDevReloader::QmlDevReloader(QQmlApplicationEngine& engine, QUrl mainUrl, QString dir,
                               QObject* parent)
    : QObject(parent), m_engine(engine), m_main(std::move(mainUrl)), m_dir(std::move(dir)) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        reloadNow();
        rescan(); // les éditeurs remplacent souvent le fichier : le watcher le perd
    });
    setWatching(true);
}

QmlDevReloader::~QmlDevReloader() = default; // détruit les copies temporaires

void QmlDevReloader::setWatching(bool on) {
    m_watching = on;
    if (!on) {
        m_debounce.stop();
        delete m_watcher;
        m_watcher = nullptr;
        return;
    }
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        const auto trigger = [this] { m_debounce.start(); };
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, trigger);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, trigger);
    }
    rescan();
}

void QmlDevReloader::rescan() {
    if (!m_watcher) {
        return;
    }
    QStringList paths{m_dir};
    QDirIterator files(m_dir, {QStringLiteral("*.qml"), QStringLiteral("qmldir")}, QDir::Files,
                       QDirIterator::Subdirectories);
    while (files.hasNext()) {
        paths.push_back(files.next());
    }
    QDirIterator dirs(m_dir, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (dirs.hasNext()) {
        paths.push_back(dirs.next());
    }
    const auto known = m_watcher->files() + m_watcher->directories();
    QStringList fresh;
    for (const auto& p : paths) {
        if (!known.contains(p)) {
            fresh.push_back(p);
        }
    }
    if (!fresh.isEmpty()) {
        m_watcher->addPaths(fresh);
    }
}

void QmlDevReloader::showErrorOnView(const QString& message) {
    const auto roots = m_engine.rootObjects();
    if (roots.isEmpty()) {
        return;
    }
    if (auto* window = qobject_cast<QWindow*>(roots.last())) {
        QString title = window->title();
        const auto marker = title.indexOf(QStringLiteral(" [QML dev"));
        if (marker >= 0) {
            title.truncate(marker);
        }
        const auto firstLine = message.section(QLatin1Char('\n'), 0, 0);
        window->setTitle(title + QStringLiteral(" [QML dev : erreur] ") + firstLine);
    }
}

bool QmlDevReloader::reloadNow() {
    QStringList warnings;
    const auto conn =
        connect(&m_engine, &QQmlEngine::warnings, this,
                [&warnings](const QList<QQmlError>& errs) { warnings.push_back(describe(errs)); });
    const auto fail = [&](const QString& message) {
        disconnect(conn);
        m_lastError = message;
        qWarning("QML dev : rechargement refusé, ancienne vue conservée :\n%s",
                 qPrintable(message));
        showErrorOnView(message);
        emit reloadFailed(message);
        return false;
    };

    // Le cache de composants de Qt garde l'ancienne version tant que l'ancienne vue est vivante :
    // on charge donc une copie versionnée du dossier (URLs neuves => aucun cache périmé), et
    // l'ancienne vue reste intacte si la nouvelle échoue.
    if (!m_snapshots) {
        m_snapshots = std::make_unique<QTemporaryDir>();
    }
    const QString genDir = m_snapshots->filePath(QStringLiteral("gen%1").arg(++m_generation));
    if (!m_snapshots->isValid() || !copyTree(m_dir, genDir)) {
        return fail(QStringLiteral("copie des sources QML impossible"));
    }
    const QString relMain = QDir(m_dir).relativeFilePath(m_main.toLocalFile());
    const QUrl url = QUrl::fromLocalFile(QDir(genDir).filePath(relMain));
    const auto cleanPaths = [&](QString text) {
        return text.replace(QUrl::fromLocalFile(genDir).toString(),
                            QUrl::fromLocalFile(m_dir).toString());
    };
    {
        QQmlComponent probe(&m_engine, url, QQmlComponent::PreferSynchronous);
        if (probe.isError()) {
            QDir(genDir).removeRecursively();
            return fail(cleanPaths(describe(probe.errors())));
        }
    }
    const auto before = m_engine.rootObjects();
    QRect geometry;
    if (!before.isEmpty()) {
        if (auto* w = qobject_cast<QWindow*>(before.first())) {
            geometry = w->geometry();
        }
    }
    m_engine.load(url);
    const auto after = m_engine.rootObjects();
    if (after.size() <= before.size()) {
        return fail(warnings.isEmpty() ? QStringLiteral("création de la vue impossible")
                                       : warnings.join(QLatin1Char('\n')));
    }
    disconnect(conn);
    if (auto* fresh = qobject_cast<QWindow*>(after.last()); fresh && geometry.isValid()) {
        fresh->setGeometry(geometry);
    }
    for (auto* old : before) {
        if (auto* w = qobject_cast<QWindow*>(old)) {
            w->hide();
        }
        old->deleteLater();
    }
    for (int g = 1; g < m_generation; ++g) {
        QDir(m_snapshots->filePath(QStringLiteral("gen%1").arg(g))).removeRecursively();
    }
    m_lastError.clear();
    ++m_reloads;
    emit reloaded();
    return true;
}

} // namespace openstitch::desktop_v2::studio
