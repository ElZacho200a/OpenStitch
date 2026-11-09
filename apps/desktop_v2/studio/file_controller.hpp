// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "hoop_store.hpp"
#include "studio_controller.hpp"
#include "task_manager.hpp"
#include "workspace_state.hpp"

namespace openstitch::desktop_v2::studio {

class ActionController;

// Famille A « Fichiers, imports, exports » : adaptateur Qt au-dessus des services de
// `desktop_v2/core` (file_io, export_settings, production_sheet, hoop_store, workspace_state,
// digitize_pipeline). Aucune logique métier ici : il enchaîne contrôle des modifications non
// enregistrées, récents, glisser-déposer, imports, exports et sauvegarde automatique. Toute
// mutation du document passe par DocumentSession ; les calculs longs par son propre TaskManager.
class FileController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList recent READ recent NOTIFY recentChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString busyLabel READ busyLabel NOTIFY busyChanged)
    Q_PROPERTY(QString pendingReason READ pendingReason NOTIFY guardChanged)
    Q_PROPERTY(bool hasRecovery READ hasRecovery NOTIFY recoveryChanged)
    Q_PROPERTY(bool hasLastExport READ hasLastExport NOTIFY exportChanged)
    Q_PROPERTY(QString lastExportName READ lastExportName NOTIFY exportChanged)
    Q_PROPERTY(QVariantList machineFormats READ machineFormats CONSTANT)
    Q_PROPERTY(QVariantList readableFilters READ readableFilters CONSTANT)
    Q_PROPERTY(QVariantList hoops READ hoops CONSTANT)
    Q_PROPERTY(QVariantList machines READ machines CONSTANT)
    Q_PROPERTY(QVariantList userHoops READ userHoops NOTIFY userHoopsChanged)
public:
    // `stateDir` : dossier des préférences hors document (récents, sauvegarde automatique, cadres
    // personnels). Vide : rien n'est persisté (tests, mode sans état).
    FileController(StudioController& studio, ActionController* actions,
                   const QString& stateDir = {}, QObject* parent = nullptr);
    ~FileController() override;

    [[nodiscard]] QVariantList recent() const;
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] bool messageIsError() const { return messageIsError_; }
    [[nodiscard]] bool busy() const { return busy_; }
    [[nodiscard]] QString busyLabel() const { return busyLabel_; }
    [[nodiscard]] QString pendingReason() const { return pendingReason_; }
    [[nodiscard]] bool hasRecovery() const { return recoveryPending_; }
    [[nodiscard]] bool hasLastExport() const { return !lastExport_.empty(); }
    [[nodiscard]] QString lastExportName() const;
    [[nodiscard]] QVariantList machineFormats() const;
    [[nodiscard]] QVariantList readableFilters() const;
    [[nodiscard]] QVariantList hoops() const;
    [[nodiscard]] QVariantList machines() const;
    [[nodiscard]] QVariantList userHoops() const;

    // --- Modifications non enregistrées -----------------------------------------------------
    // Chaque demande qui remplace le document passe par ce contrôle : document propre -> exécutée
    // tout de suite ; modifié -> `pendingReason` se remplit et la boîte « Enregistrer / Ignorer /
    // Annuler » doit répondre par resolveGuard("save" | "discard" | "cancel").
    Q_INVOKABLE void requestNew();
    Q_INVOKABLE void requestOpenRecent(int index);
    // Ouvre selon l'extension : .osp, broderie machine, image (source seule), SVG, DXF.
    Q_INVOKABLE void openPath(const QString& path);
    Q_INVOKABLE void openUrl(const QUrl& url);
    // Glisser-déposer : traite le premier fichier local de la liste.
    Q_INVOKABLE void openDropped(const QVariantList& urls);
    // Fermeture de la fenêtre : vrai si on peut quitter immédiatement ; sinon la boîte s'ouvre et
    // quitApproved() sera émis après « Enregistrer » ou « Ignorer ».
    Q_INVOKABLE bool requestQuit();
    Q_INVOKABLE void resolveGuard(const QString& choice);
    // Suite d'« Enregistrer » quand le projet n'a pas encore de fichier.
    Q_INVOKABLE void guardSaveAs(const QUrl& url);
    Q_INVOKABLE void clearRecent();
    Q_INVOKABLE void revealFolder();
    Q_INVOKABLE [[nodiscard]] QString currentFolder() const;

    // --- Imports ----------------------------------------------------------------------------
    Q_INVOKABLE [[nodiscard]] QVariantMap imageImportDefaults() const;
    Q_INVOKABLE void importImage(const QString& path, const QVariantMap& options);
    Q_INVOKABLE void importVector(const QString& path, const QString& stitchType = {});

    // --- Exports ----------------------------------------------------------------------------
    Q_INVOKABLE [[nodiscard]] QVariantMap exportOptions() const;
    // Applique les réglages d'export au document (UNE commande annulable) ; "" ou refus lisible.
    Q_INVOKABLE QString setExportOptions(const QVariantMap& values);
    Q_INVOKABLE [[nodiscard]] QString exportCheck(const QString& formatExtension) const;
    Q_INVOKABLE void exportMachine(const QUrl& url, const QVariantMap& options = {});
    // Explicit acknowledgement is required when the design exceeds a hoop/machine limit.
    Q_INVOKABLE void confirmMachineExport(bool proceed);
    Q_INVOKABLE void exportAgain();
    Q_INVOKABLE void exportThreadList(const QUrl& url);
    Q_INVOKABLE void exportDxf(const QUrl& url);
    Q_INVOKABLE void exportSvg(const QUrl& url);
    Q_INVOKABLE void exportImage(const QUrl& url, double pxPerMm = 6.0, int jpegQuality = 92);
    Q_INVOKABLE void exportProductionSheet(const QUrl& url);
    Q_INVOKABLE void exportPlacementTemplate(const QUrl& url, const QVariantMap& options = {});
    Q_INVOKABLE void cancelTask();

    // --- Cadre et machine -------------------------------------------------------------------
    Q_INVOKABLE [[nodiscard]] QVariantMap currentHoop() const;
    Q_INVOKABLE QString applyHoop(const QString& hoopId, double marginMm);
    Q_INVOKABLE QString applyCustomHoop(double widthMm, double heightMm, double marginMm);
    Q_INVOKABLE QString applyMachine(const QString& machineId);
    Q_INVOKABLE QString saveCurrentHoopAs(const QString& name);
    Q_INVOKABLE QString applyUserHoop(const QString& name);
    Q_INVOKABLE QString removeUserHoop(const QString& name);

    // --- Sauvegarde automatique -------------------------------------------------------------
    Q_INVOKABLE bool autosaveNow();
    Q_INVOKABLE void recoverAutosave();
    Q_INVOKABLE void discardRecovery();
    void setAutosaveIntervalMs(int ms);

signals:
    void recentChanged();
    void messageChanged();
    void busyChanged();
    void guardChanged();
    void recoveryChanged();
    void exportChanged();
    void exportConfirmationRequested(const QString& warning);
    void userHoopsChanged();
    // QML doit ouvrir « Enregistrer sous » pour poursuivre (document sans fichier).
    void saveAsRequested();
    void quitApproved();
    // Image chargée et adoptée : QML active l'espace Image, sans lancer la segmentation.
    void imageImported();

private:
    void setMessage(const QString& text, bool error = false);
    void setBusy(bool busy, const QString& label = {});
    // Exécute `action` tout de suite (document propre) ou après la réponse de la boîte.
    void guarded(const QString& reason, std::function<void()> action);
    void finishGuard(bool proceed);
    void doOpenOsp(const QString& path);
    void doOpenMachine(const QString& path);
    void doNew();
    void noteRecentFromStudio();
    void persistRecent();
    void clearAutosaveFile();
    bool ensureCanStartTask();
    template <typename T>
    void runJob(const QString& label, std::function<Result<T>(const TaskContext&)> work,
                std::function<void(T&&)> onOk, const QString& errorPrefix);
    void runMachineExport(const std::filesystem::path& path, bool confirmed = false);
    [[nodiscard]] std::filesystem::path pathOf(const QUrl& url,
                                               const char* defaultSuffix = nullptr) const;

    StudioController& studio_;
    ActionController* actions_;
    std::filesystem::path stateDir_;
    std::vector<std::string> recent_;
    std::string lastNotedPath_;
    std::unique_ptr<AutosaveStore> autosave_;
    std::vector<UserHoop> userHoops_;
    QTimer autosaveTimer_;
    QTimer pump_;
    QString message_;
    bool messageIsError_{false};
    bool busy_{false};
    QString busyLabel_;
    QString pendingReason_;
    std::function<void()> pending_;
    bool pendingSaveAs_{false};
    bool recoveryPending_{false};
    bool wasDirty_{false};
    std::filesystem::path lastExport_;
    std::optional<std::filesystem::path> pendingExport_;
    std::uint64_t pendingExportRevision_{0};
    std::uint64_t loadGeneration_{0};
    TaskId currentTask_{0};
    // Détruit avant les membres ci-dessus : ses rappels les utilisent.
    TaskManager tasks_;
};

} // namespace openstitch::desktop_v2::studio
