// SPDX-License-Identifier: Apache-2.0
pragma Singleton
import QtQuick

// Registre léger des DockPanel vivants (une instance par panneau permanent des 4 espaces).
// Chaque DockPanel s'inscrit à Component.onCompleted et se désinscrit à Component.onDestruction ;
// le menu « Panneaux » (Main.qml) l'utilise pour lister/rouvrir les panneaux masqués ou détachés
// de l'espace actif sans avoir à les retrouver dans l'arbre QML.
QtObject {
    id: registry
    // panelId -> { title, spaceId, panel: référence vivante du DockPanel }. Objet JS simple (pas
    // un modèle Qt) : `changed()` est émis à chaque (dé)inscription pour que les vues se reconstruisent.
    property var entries: ({})
    signal changed

    function register(panelId, title, spaceId, panel) {
        if (!panelId) return
        entries[panelId] = { title: title, spaceId: spaceId, panel: panel }
        registry.entries = registry.entries
        registry.changed()
    }

    function unregister(panelId) {
        if (!(panelId in entries)) return
        delete entries[panelId]
        registry.entries = registry.entries
        registry.changed()
    }

    // Liste (triée par titre) des panneaux d'un espace, chacun enrichi de son id.
    function panelsForSpace(spaceId) {
        const result = []
        for (const id in registry.entries) {
            const entry = registry.entries[id]
            if (entry.spaceId === spaceId)
                result.push({ id: id, title: entry.title, spaceId: entry.spaceId, panel: entry.panel })
        }
        result.sort((a, b) => a.title.localeCompare(b.title))
        return result
    }
}
