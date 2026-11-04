// SPDX-License-Identifier: Apache-2.0
#include "workspace_state.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>

#include "openstitch/project_io/project_io.hpp"

namespace openstitch::desktop_v2 {

std::vector<std::string> pushRecent(std::vector<std::string> list, const std::string& path,
                                    std::size_t maxCount) {
    if (path.empty() || maxCount == 0) {
        return list;
    }
    list.erase(std::remove(list.begin(), list.end(), path), list.end());
    list.insert(list.begin(), path);
    if (list.size() > maxCount) {
        list.resize(maxCount);
    }
    return list;
}

std::vector<std::string> pruneMissingRecent(const std::vector<std::string>& list) {
    std::vector<std::string> kept;
    for (const auto& entry : list) {
        std::error_code ec;
        if (std::filesystem::exists(
                std::filesystem::path(std::u8string(entry.begin(), entry.end())), ec) &&
            !ec) {
            kept.push_back(entry);
        }
    }
    return kept;
}

std::vector<std::string> loadRecentList(const std::filesystem::path& file) {
    std::vector<std::string> list;
    std::ifstream in(file, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == 0x0D) {
            line.pop_back();
        }
        if (!line.empty()) {
            list = pushRecent(std::move(list), line, kMaxRecentFiles + 1); // garde l'ordre
        }
    }
    // pushRecent met en tête : on remet l'ordre du fichier.
    std::reverse(list.begin(), list.end());
    if (list.size() > kMaxRecentFiles) {
        list.resize(kMaxRecentFiles);
    }
    return list;
}

bool saveRecentList(const std::filesystem::path& file, const std::vector<std::string>& list) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    for (const auto& entry : list) {
        out << entry << '\n';
    }
    return static_cast<bool>(out);
}

AutosaveStore::AutosaveStore(std::filesystem::path dir) : file_(std::move(dir) / "autosave.osp") {}

bool AutosaveStore::exists() const {
    std::error_code ec;
    return std::filesystem::is_regular_file(file_, ec) && !ec;
}

Result<void> AutosaveStore::write(const document::Project& project) const {
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    if (ec) {
        return std::unexpected(Error{ErrorCategory::OperationImpossible,
                                     "Dossier de sauvegarde inaccessible.", ec.message(), "", ""});
    }
    auto tmp = file_;
    tmp += ".tmp";
    if (auto saved = project_io::save_project(tmp, project); !saved) {
        std::filesystem::remove(tmp, ec);
        return saved;
    }
    std::filesystem::rename(tmp, file_, ec); // remplace atomiquement la précédente
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return std::unexpected(Error{ErrorCategory::OperationImpossible,
                                     "Remplacement de la sauvegarde impossible.", "", "", ""});
    }
    return {};
}

Result<document::Project> AutosaveStore::load() const {
    return project_io::load_project(file_);
}

void AutosaveStore::clear() const {
    std::error_code ec;
    std::filesystem::remove(file_, ec);
}

} // namespace openstitch::desktop_v2
