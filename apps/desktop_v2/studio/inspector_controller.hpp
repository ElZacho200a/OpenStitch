// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "document_session.hpp"
#include "param_spec.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

// Qt adapter of the contextual inspector. The fields, ranges, levels, defaults and mixed values
// come from `param_spec` / `inspector_model`; every mutation goes through DocumentSession as ONE
// undo step (a burst of edits of the same field of the same objects is merged by the undo stack).
// A refused edit never changes the document and never silently corrects the value: the reason
// is returned (and kept in `message`).
class InspectorController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    // Couleur COMMUNE de la sélection (« #rrggbb »), vide si elle varie ou si rien n'est
    // sélectionné.
    Q_PROPERTY(QString colour READ colour NOTIFY changed)
    Q_PROPERTY(bool colourMixed READ colourMixed NOTIFY changed)
    Q_PROPERTY(QString typeId READ typeId NOTIFY changed)
    Q_PROPERTY(QString typeLabel READ typeLabel NOTIFY changed)
    Q_PROPERTY(bool typeMixed READ typeMixed NOTIFY changed)
    Q_PROPERTY(QVariantList types READ types NOTIFY changed)
    Q_PROPERTY(QVariantList fields READ fields NOTIFY changed)
    Q_PROPERTY(QVariantList sections READ sections NOTIFY changed)
    Q_PROPERTY(int totalFieldCount READ totalFieldCount NOTIFY changed)
    Q_PROPERTY(int hiddenAdvancedCount READ hiddenAdvancedCount NOTIFY changed)
    Q_PROPERTY(int hiddenExpertCount READ hiddenExpertCount NOTIFY changed)
    Q_PROPERTY(bool showExpert READ showExpert WRITE setShowExpert NOTIFY changed)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY changed)
    Q_PROPERTY(bool showAdvanced READ showAdvanced WRITE setShowAdvanced NOTIFY changed)
    Q_PROPERTY(bool allPresets READ allPresets WRITE setAllPresets NOTIFY changed)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool messageOk READ messageOk NOTIFY messageChanged)
public:
    explicit InspectorController(StudioController& studio, QObject* parent = nullptr);
    ~InspectorController() override;

    [[nodiscard]] int count() const;
    // Incremented on every change: lets QML re-read `field(key)` without rebuilding its delegates
    // (a slider being dragged must survive the refresh its own edit triggers).
    [[nodiscard]] int revision() const { return revision_; }
    Q_INVOKABLE [[nodiscard]] QVariantMap field(const QString& key) const;
    [[nodiscard]] QString colour() const;
    [[nodiscard]] bool colourMixed() const;
    [[nodiscard]] QString typeId() const;
    [[nodiscard]] QString typeLabel() const;
    [[nodiscard]] bool typeMixed() const;
    // [{id, label, current}] : stitch types a selection can be converted to.
    [[nodiscard]] QVariantList types() const;
    // Flat list of the fields currently shown (level / search applied):
    // [{key, label, kind(number|integer|bool|choice), min, max, step, unit, level(essential|
    //   advanced), section, value, text, mixed, isDefault, defaultValue, choices:[{value,label}]}]
    [[nodiscard]] QVariantList fields() const;
    // The same fields grouped: [{title, fields:[...]}]
    [[nodiscard]] QVariantList sections() const;
    // Every field of the type (ignoring level / search) and how many are hidden by the level.
    [[nodiscard]] int totalFieldCount() const;
    [[nodiscard]] int hiddenAdvancedCount() const;
    [[nodiscard]] int hiddenExpertCount() const;
    // Expert level: also shows the expert fields (seeds, thresholds, points, guide lists).
    [[nodiscard]] bool showExpert() const { return showExpert_; }
    void setShowExpert(bool show);
    [[nodiscard]] QString query() const { return query_; }
    void setQuery(const QString& query);
    [[nodiscard]] bool showAdvanced() const { return showAdvanced_; }
    void setShowAdvanced(bool show);
    [[nodiscard]] bool allPresets() const { return allPresets_; }
    void setAllPresets(bool all);
    // [{index, name, typeId, typeLabel, matches}] ; only the presets of the selected type unless
    // `allPresets`.
    [[nodiscard]] QVariantList presets() const;
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] bool messageOk() const { return ok_; }

    // Every call returns "" on success, otherwise the readable refusal (document untouched).
    // Change la couleur de TOUTE la sélection (types de points différents compris) : une étape.
    Q_INVOKABLE QString setColour(const QString& hex);
    Q_INVOKABLE QString setParam(const QString& key, double value);
    // Typed entry: accepts "0,35" and "0.35"; text that is not a number is refused, not guessed.
    Q_INVOKABLE QString setParamText(const QString& key, const QString& text);
    // Optional point [x, y] in mm (entry / exit / centre) and clearing of an optional setting or a
    // list (guides, break lines, rungs, gradient, sweep angle): one undo step.
    Q_INVOKABLE QString setPoint(const QString& key, double xMm, double yMm);
    Q_INVOKABLE QString clearParam(const QString& key);
    Q_INVOKABLE QString resetParam(const QString& key);
    Q_INVOKABLE QString resetAll();
    // Conversion of the selected objects to another stitch type (one undo step).
    Q_INVOKABLE QString setStitchType(const QString& id);
    Q_INVOKABLE QString savePreset(const QString& name);
    Q_INVOKABLE QString applyPreset(int index);
    Q_INVOKABLE QString renamePreset(int index, const QString& name);
    Q_INVOKABLE QString removePreset(int index);
signals:
    void changed();
    void messageChanged();

private:
    struct Selection {
        std::vector<std::uint64_t> ids;
        std::string typeId; // common type id ("" when mixed or empty)
        bool typeMixed{false};
        // Objects grouped by stitch type (one group when the type is common, several when mixed).
        std::vector<std::pair<std::string, std::vector<std::uint64_t>>> groups;
    };
    [[nodiscard]] Selection selection() const;
    // Settings of the selection: the type's own table, or - for mixed types - the scalar
    // settings every type has in common (same key, kind and options; range intersected).
    [[nodiscard]] std::vector<ParamSpec> specsOf(const Selection& selection) const;
    [[nodiscard]] QVariantMap fieldMap(const Selection& selection, const ParamSpec& spec) const;
    [[nodiscard]] std::vector<ParamSpec> visibleSpecs(const Selection& selection) const;
    QString applyPairs(const Selection& selection,
                       const std::vector<std::pair<std::string, double>>& pairs,
                       const QString& label, const std::string& mergeKey);
    QString commitBatches(const Selection& selection, const std::vector<std::string>& batches,
                          const QString& label, const std::string& mergeKey);
    void bump();
    QString finish(const QString& refusal, const QString& success = {});
    DocumentSession& session_;
    DocumentSession::Token documentToken_{};
    SelectionModel::Token selectionToken_{};
    QString query_;
    bool showAdvanced_{false};
    bool showExpert_{false};
    bool allPresets_{false};
    QString message_;
    bool ok_{true};
    int revision_{0};
};

} // namespace openstitch::desktop_v2::studio
