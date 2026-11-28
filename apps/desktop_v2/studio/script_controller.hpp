// SPDX-License-Identifier: Apache-2.0
#pragma once

// Studio -- SCRIPT-01 : script Python sur la multi-sélection. Adaptateur SANS logique métier
// au-dessus de `scripting::apply_selection_script` : le script tourne hors du fil d'interface sur
// un instantané (aperçu = simulation du lot), puis « Appliquer » pousse le lot via
// `DocumentSession::applyBatch` : UNE étape d'annulation. Un aperçu porte la révision du document,
// la sélection et le texte du script ; s'ils changent, il est périmé et « Appliquer » le relance.

#include <QObject>
#include <QString>
#include <QVariantList>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "studio_controller.hpp"
#include "task_manager.hpp"

namespace openstitch::desktop_v2::studio {

class ScriptController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY messageChanged)
    // Aperçu : [{object, name, key, before, after}] et sortie du script (print).
    Q_PROPERTY(QVariantList changes READ changes NOTIFY previewChanged)
    Q_PROPERTY(QString output READ output NOTIFY previewChanged)
    Q_PROPERTY(bool previewReady READ previewReady NOTIFY previewChanged)
    Q_PROPERTY(bool previewStale READ previewStale NOTIFY stateChanged)
    // Exemples prêts à adapter : [{title, code}] ; l'exemple chargé en dernier, et si le texte
    // en diffère (modifié à la main).
    Q_PROPERTY(QVariantList examples READ examples CONSTANT)
    Q_PROPERTY(int exampleIndex READ exampleIndex NOTIFY sourceChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY sourceChanged)
    // Vrai après le chargement d'un exemple par-dessus un script modifié : « Restaurer » le rend.
    Q_PROPERTY(bool canRestore READ canRestore NOTIFY sourceChanged)
public:
    explicit ScriptController(StudioController& studio, QObject* parent = nullptr);
    ~ScriptController() override;

    [[nodiscard]] QString source() const { return source_; }
    void setSource(const QString& source);
    [[nodiscard]] int selectionCount() const;
    [[nodiscard]] bool busy() const { return running_; }
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] bool lastOk() const { return ok_; }
    [[nodiscard]] QVariantList changes() const;
    [[nodiscard]] QString output() const { return output_; }
    [[nodiscard]] bool previewReady() const { return preview_.has_value(); }
    [[nodiscard]] bool previewStale() const;
    [[nodiscard]] QVariantList examples() const;
    [[nodiscard]] int exampleIndex() const { return exampleIndex_; }
    [[nodiscard]] bool modified() const;
    [[nodiscard]] bool canRestore() const { return !savedSource_.isEmpty(); }

    // Remplace le script par l'exemple `index`. Un script modifié à la main est gardé de côté
    // (`restoreSource`), jamais perdu sans retour.
    Q_INVOKABLE void loadExample(int index);
    Q_INVOKABLE void restoreSource();
    // Exécute le script sur la sélection et simule son lot (rien n'est modifié).
    Q_INVOKABLE void preview();
    // Applique l'aperçu s'il est à jour (UNE étape d'annulation) ; sinon relance l'aperçu et
    // applique dès qu'il est prêt et valide.
    Q_INVOKABLE void apply();
    Q_INVOKABLE void cancel();
signals:
    void sourceChanged();
    void stateChanged();
    void busyChanged();
    void messageChanged();
    void previewChanged();
    // Émis après une application réussie (tests, retour visuel).
    void applied(int changeCount);

private:
    struct Preview {
        std::uint64_t revision{0};
        std::vector<ObjectId> selection;
        QString source;
        std::string batch;
        QVariantList changes;
        std::size_t operations{0};
    };
    [[nodiscard]] std::vector<ObjectId> selectedIds() const;
    void run(bool applyAfter);
    bool applyPreview();
    void say(bool ok, const QString& text);
    void setRunning(bool running);

    StudioController& studio_;
    QString source_;
    QString savedSource_; // script modifié remplacé par un exemple
    int exampleIndex_{0};
    QString message_;
    QString output_;
    bool ok_{true};
    bool running_{false};
    std::optional<Preview> preview_;
    TaskManager tasks_;
};

} // namespace openstitch::desktop_v2::studio
