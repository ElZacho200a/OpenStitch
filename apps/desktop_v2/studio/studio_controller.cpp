// SPDX-License-Identifier: Apache-2.0
#include "studio_controller.hpp"

#include <QColor>
#include <QFileInfo>
#include <algorithm>
#include <filesystem>
#include <limits>

#include "inspector_model.hpp"
#include "object_labels.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "transform_batch.hpp"

namespace openstitch::desktop_v2::studio {

StudioController::StudioController(DocumentSession& session, QObject* parent)
    : QObject(parent), session_(session) {
    documentToken_ = session_.subscribe([this](ChangeKind kind) {
        if (scene_ && scene_->revision == session_.revision()) {
            emit changed(); // markSaved changes no render data, only the dirty flag.
            return;
        }
        if (kind == ChangeKind::Replaced) {
            emit documentReplaced();
        }
        scene_.reset();
        emit sceneChanged();
        rebuildScene();
    });
    selectionToken_ = session_.selection().subscribe([this](const SelectionModel&) {
        emit selectionChanged();
        emit changed();
    });
    pump_.setInterval(8);
    connect(&pump_, &QTimer::timeout, this, [this] { tasks_.pump(); });
    pump_.start();
    if (!session_.project().embroidery_objects.empty() || session_.project().imported_design) {
        rebuildScene();
    }
}

StudioController::~StudioController() {
    pump_.stop();
    session_.unsubscribe(documentToken_);
    session_.selection().unsubscribe(selectionToken_);
    tasks_.cancelAll();
}

void StudioController::openProject(const QUrl& url) {
    if (!url.isLocalFile()) {
        status_ = QStringLiteral("Choisissez un fichier .osp local");
        emit changed();
        emit loadFinished(false);
        return;
    }
    tasks_.cancelAll();
    const auto request = ++request_;
    const auto revision = session_.revision();
    const QString path = url.toLocalFile();
    busy_ = true;
    status_ = QStringLiteral("Ouverture de %1…").arg(QFileInfo(path).fileName());
    emit changed();
    tasks_.submit<document::Project>(
        "studio.open",
        [file = std::filesystem::path(path.toStdWString())](
            const TaskContext& task) -> Result<document::Project> {
            if (task.cancelled()) {
                return cancelled_error();
            }
            auto result = project_io::load_project(file);
            if (task.cancelled()) {
                return cancelled_error();
            }
            return result;
        },
        [this, request, revision, path](Result<document::Project>&& result) {
            if (request != request_ || revision != session_.revision()) {
                return;
            }
            if (!result) {
                busy_ = false;
                status_ = QString::fromStdString(result.error().message);
                emit changed();
                emit loadFinished(false);
                return;
            }
            title_ = QFileInfo(path).fileName();
            path_ = path;
            session_.replaceProject(std::move(*result), true);
        });
}

void StudioController::newProject() {
    tasks_.cancelAll();
    ++request_;
    busy_ = false;
    path_.clear();
    title_ = QStringLiteral("Sans projet");
    status_ = QStringLiteral("Nouveau projet");
    session_.replaceProject(document::Project{}, true);
    emit changed();
}

bool StudioController::writeProject(const QString& path) {
    const auto saved =
        project_io::save_project(std::filesystem::path(path.toStdWString()), session_.project());
    if (!saved) {
        status_ = QStringLiteral("Enregistrement impossible : %1")
                      .arg(QString::fromStdString(saved.error().message));
        emit changed();
        return false;
    }
    path_ = path;
    title_ = QFileInfo(path).fileName();
    status_ = QStringLiteral("Projet enregistré");
    session_.markSaved(); // notifies; the listener emits changed().
    emit changed();
    return true;
}

bool StudioController::saveProject() {
    if (path_.isEmpty()) {
        status_ = QStringLiteral("Choisissez un fichier avec Enregistrer sous");
        emit changed();
        return false;
    }
    return writeProject(path_);
}

bool StudioController::saveProjectAs(const QUrl& url) {
    if (!url.isLocalFile()) {
        status_ = QStringLiteral("Choisissez un fichier .osp local");
        emit changed();
        return false;
    }
    QString path = url.toLocalFile();
    if (QFileInfo(path).suffix().isEmpty()) {
        path += QStringLiteral(".osp");
    }
    return writeProject(path);
}

QString StudioController::undoLabel() const {
    return QString::fromStdString(session_.undoName());
}

QString StudioController::redoLabel() const {
    return QString::fromStdString(session_.redoName());
}

void StudioController::undo() {
    if (session_.undo()) {
        status_ = QStringLiteral("Annulé");
        emit changed();
    }
}

void StudioController::redo() {
    if (session_.redo()) {
        status_ = QStringLiteral("Rétabli");
        emit changed();
    }
}

bool StudioController::moveSelection(double dxMm, double dyMm) {
    std::vector<std::uint64_t> ids;
    for (const auto& target : session_.selection().targets()) {
        if (target.kind == TargetKind::EmbroideryObject) {
            ids.push_back(target.id);
        }
    }
    const auto batch = batchTranslate(ids, dxMm, dyMm);
    if (batch.empty()) {
        return false; // nothing selected, zero or out-of-range offset
    }
    const auto result = session_.applyBatch(batch, "Déplacer");
    if (!result.committed) {
        status_ = QStringLiteral("Déplacement refusé");
        emit changed();
        return false;
    }
    return true;
}

void StudioController::rebuildScene() {
    tasks_.cancelAll();
    const auto request = ++request_;
    const auto revision = session_.revision();
    auto snapshot = session_.snapshot();
    busy_ = true;
    status_ = QStringLiteral("Préparation des segments…");
    emit changed();
    tasks_.submit<std::shared_ptr<const StudioSceneSnapshot>>(
        "studio.geometry",
        [snapshot, revision](const TaskContext& task) {
            return build_studio_scene(snapshot, revision, task);
        },
        [this, request, revision](Result<std::shared_ptr<const StudioSceneSnapshot>>&& result) {
            if (request != request_ || revision != session_.revision()) {
                return;
            }
            busy_ = false;
            if (!result) {
                // Never leave geometry from an older document on screen after an edit.
                scene_.reset();
                status_ = QString::fromStdString(result.error().message);
                emit sceneChanged();
                emit changed();
                emit loadFinished(false);
                return;
            }
            scene_ = std::move(*result);
            status_ =
                QStringLiteral("%1 segments · coordonnées en mm").arg(scene_->segments.size());
            emit sceneChanged();
            emit changed();
            emit loadFinished(true);
        });
}

QVariantList StudioController::objects() const {
    QVariantList list;
    for (const auto& object : session_.project().embroidery_objects) {
        const Target target{TargetKind::EmbroideryObject, object.id.value};
        list.push_back(QVariantMap{
            {QStringLiteral("id"), QString::number(object.id.value)},
            {QStringLiteral("label"),
             QString::fromStdString(object.name.empty() ? "Objet " + std::to_string(object.id.value)
                                                        : object.name)},
            {QStringLiteral("color"), QColor(object.rgb[0], object.rgb[1], object.rgb[2])},
            {QStringLiteral("selected"), session_.selection().contains(target)},
            {QStringLiteral("visible"), object.visible && session_.project().calqueVisible(object)},
            {QStringLiteral("type"),
             QString::fromUtf8(stitchTypeId(object).data(),
                               static_cast<qsizetype>(stitchTypeId(object).size()))}});
    }
    return list;
}

QString StudioController::selectionInfo() const {
    const auto state = inspect(session_.project(), session_.selection().targets());
    if (state.count == 0) {
        return QStringLiteral("Cliquez sur un fil ou choisissez un objet dans la liste.\n\n"
                              "Molette : zoom au curseur\nClic milieu / Espace : déplacer\nF : "
                              "ajuster le motif\nCtrl+clic : sélection multiple");
    }
    QString result = QStringLiteral("%1 objet(s) sélectionné(s)\n").arg(state.count);
    result += state.name.mixed ? QStringLiteral("Noms multiples")
                               : QString::fromStdString(state.name.value);
    if (!state.typeId.mixed) {
        result += QStringLiteral("\nPoint : %1").arg(QString::fromStdString(state.typeId.value));
    }
    if (state.count == 1 && scene_) {
        const auto found = scene_->objectBounds.find(state.ids.front());
        if (found != scene_->objectBounds.end()) {
            result += QStringLiteral("\n%1 × %2 mm\nID %3")
                          .arg(found->second.width(), 0, 'f', 2)
                          .arg(found->second.height(), 0, 'f', 2)
                          .arg(state.ids.front());
        }
    }
    return result;
}

int StudioController::commandCount() const {
    return scene_ ? static_cast<int>(std::min<std::size_t>(scene_->commandCount,
                                                           std::numeric_limits<int>::max()))
                  : 0;
}

QVariantMap StudioController::toolInfo(std::string_view id) const {
    const auto* tool = tools_.find(id);
    if (!tool) {
        return {};
    }
    return {{QStringLiteral("label"), QString::fromStdString(tool->label)},
            {QStringLiteral("shortcut"), QString::fromStdString(tool->shortcut)}};
}

void StudioController::selectObject(const QString& id, bool toggle) {
    bool ok = false;
    const auto value = id.toULongLong(&ok);
    const Target target{TargetKind::EmbroideryObject, value};
    if (!ok || !session_.exists(target)) {
        return;
    }
    if (toggle) {
        session_.selection().toggle(target);
    } else {
        session_.selection().replace({target});
    }
}

void StudioController::clearSelection() {
    session_.selection().clear();
}

void StudioController::cancel() {
    tasks_.cancelAll();
    ++request_;
    busy_ = false;
    status_ = QStringLiteral("Chargement annulé");
    emit changed();
}

} // namespace openstitch::desktop_v2::studio
