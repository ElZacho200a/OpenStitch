// SPDX-License-Identifier: Apache-2.0
#include "openstitch/commands/text_commands.hpp"

#include <algorithm>

namespace openstitch::commands {

namespace {

using EmbroideryList = std::vector<std::pair<std::size_t, document::EmbroideryObject>>;
using VectorList = std::vector<std::pair<std::size_t, document::VectorObject>>;

// Retire de `project` les lettres de `owner` (vecteurs `text_owner` et broderies
// qui les suivent) et les renvoie avec leurs index d'origine, en ordre croissant.
void extract_letters(document::Project& project, ObjectId owner, VectorList& vectors,
                     EmbroideryList& embroideries) {
    vectors.clear();
    embroideries.clear();
    std::vector<ObjectId> vectorIds;
    auto& vecs = project.vector_objects;
    for (std::size_t i = 0; i < vecs.size(); ++i) {
        if (vecs[i].text_owner && *vecs[i].text_owner == owner) {
            vectors.emplace_back(i, vecs[i]);
            vectorIds.push_back(vecs[i].id);
        }
    }
    auto& embs = project.embroidery_objects;
    for (std::size_t i = 0; i < embs.size(); ++i) {
        if (std::find(vectorIds.begin(), vectorIds.end(), embs[i].source_vector) !=
            vectorIds.end()) {
            embroideries.emplace_back(i, embs[i]);
        }
    }
    // Retrait en ordre DÉCROISSANT d'index : les index relevés restent valides.
    for (auto it = vectors.rbegin(); it != vectors.rend(); ++it) {
        vecs.erase(vecs.begin() + static_cast<std::ptrdiff_t>(it->first));
    }
    for (auto it = embroideries.rbegin(); it != embroideries.rend(); ++it) {
        embs.erase(embs.begin() + static_cast<std::ptrdiff_t>(it->first));
    }
}

void restore_letters(document::Project& project, const VectorList& vectors,
                     const EmbroideryList& embroideries) {
    // Réinsertion en ordre CROISSANT d'index d'origine : reproduit la disposition.
    for (const auto& [index, object] : vectors) {
        const std::size_t pos = std::min(index, project.vector_objects.size());
        project.vector_objects.insert(
            project.vector_objects.begin() + static_cast<std::ptrdiff_t>(pos), object);
    }
    for (const auto& [index, object] : embroideries) {
        const std::size_t pos = std::min(index, project.embroidery_objects.size());
        project.embroidery_objects.insert(
            project.embroidery_objects.begin() + static_cast<std::ptrdiff_t>(pos), object);
    }
}

} // namespace

SetTextObjectCommand::SetTextObjectCommand(document::TextObject text,
                                           std::vector<document::VectorObject> vectors,
                                           std::vector<document::EmbroideryObject> embroideries,
                                           std::string label)
    : text_(std::move(text)), vectors_(std::move(vectors)), embroideries_(std::move(embroideries)),
      label_(std::move(label)) {}

void SetTextObjectCommand::apply(document::Project& project) {
    oldText_.reset();
    textIndex_.reset();
    for (std::size_t i = 0; i < project.text_objects.size(); ++i) {
        if (project.text_objects[i].id == text_.id) {
            textIndex_ = i;
            oldText_ = project.text_objects[i];
            break;
        }
    }
    extract_letters(project, text_.id, oldVectors_, oldEmbroideries_);

    // Les nouvelles lettres prennent la place des anciennes (ordre de couture et
    // ordre d'affichage conservés) ; un texte neuf s'ajoute à la fin.
    const std::size_t vecAt =
        oldVectors_.empty() ? project.vector_objects.size()
                            : std::min(oldVectors_.front().first, project.vector_objects.size());
    const std::size_t embAt =
        oldEmbroideries_.empty()
            ? project.embroidery_objects.size()
            : std::min(oldEmbroideries_.front().first, project.embroidery_objects.size());
    project.vector_objects.insert(project.vector_objects.begin() +
                                      static_cast<std::ptrdiff_t>(vecAt),
                                  vectors_.begin(), vectors_.end());
    project.embroidery_objects.insert(project.embroidery_objects.begin() +
                                          static_cast<std::ptrdiff_t>(embAt),
                                      embroideries_.begin(), embroideries_.end());
    if (textIndex_) {
        project.text_objects[*textIndex_] = text_;
    } else {
        project.text_objects.push_back(text_);
    }
    applied_ = true;
}

void SetTextObjectCommand::revert(document::Project& project) {
    if (!applied_) {
        return;
    }
    // Retire les lettres neuves (par id) puis rend les anciennes à leurs index.
    for (const auto& e : embroideries_) {
        std::erase_if(project.embroidery_objects,
                      [&](const document::EmbroideryObject& o) { return o.id == e.id; });
    }
    for (const auto& v : vectors_) {
        std::erase_if(project.vector_objects,
                      [&](const document::VectorObject& o) { return o.id == v.id; });
    }
    restore_letters(project, oldVectors_, oldEmbroideries_);
    if (textIndex_ && oldText_) {
        project.text_objects[*textIndex_] = *oldText_;
    } else {
        std::erase_if(project.text_objects,
                      [&](const document::TextObject& t) { return t.id == text_.id; });
    }
    applied_ = false;
}

void RemoveTextObjectCommand::apply(document::Project& project) {
    applied_ = false;
    for (std::size_t i = 0; i < project.text_objects.size(); ++i) {
        if (project.text_objects[i].id == id_) {
            textIndex_ = i;
            oldText_ = project.text_objects[i];
            project.text_objects.erase(project.text_objects.begin() +
                                       static_cast<std::ptrdiff_t>(i));
            extract_letters(project, id_, oldVectors_, oldEmbroideries_);
            applied_ = true;
            return;
        }
    }
}

void RemoveTextObjectCommand::revert(document::Project& project) {
    if (!applied_) {
        return;
    }
    restore_letters(project, oldVectors_, oldEmbroideries_);
    const std::size_t pos = std::min(textIndex_, project.text_objects.size());
    project.text_objects.insert(project.text_objects.begin() + static_cast<std::ptrdiff_t>(pos),
                                oldText_);
    applied_ = false;
}

} // namespace openstitch::commands
