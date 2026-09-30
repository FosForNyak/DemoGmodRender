#include "assets/vfs.h"

#include "core/file.h"
#include "core/text.h"

#include <algorithm>
#include <charconv>
#include <chrono>

namespace gmdr::assets {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxVpkDirBytes = 256u << 20;

bool isDir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

bool isFile(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

std::optional<std::uint64_t> parseId(std::string_view s) {
    std::uint64_t v = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || p != s.data() + s.size() || v == 0)
        return std::nullopt;
    return v;
}

// Sorted directory listing (stable order across runs); errors yield an empty list.
std::vector<fs::directory_entry> listDir(const fs::path& dir) {
    std::vector<fs::directory_entry> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        out.push_back(*it);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.path() < b.path(); });
    return out;
}

bool hasSuffix(const fs::path& p, std::string_view suffix) {
    return endsWith(toLowerAscii(pathToUtf8(p.filename())), suffix);
}

} // namespace

const char* sourceKindName(SourceKind kind) {
    switch (kind) {
    case SourceKind::Folder:
        return "folder";
    case SourceKind::AddonFolder:
        return "addon";
    case SourceKind::Gma:
        return "gma";
    case SourceKind::Vpk:
        return "vpk";
    case SourceKind::MountFolder:
        return "mount";
    case SourceKind::MountVpk:
        return "mount-vpk";
    case SourceKind::Download:
        return "download";
    case SourceKind::Pakfile:
        return "pakfile";
    }
    return "unknown";
}

Result<Vfs> Vfs::build(const GmodInstall& g) {
    const auto t0 = std::chrono::steady_clock::now();
    Vfs v;
    auto addFolder = [&](SourceKind kind, const std::string& name, const fs::path& root) {
        if (!isDir(root))
            return;
        Source s{kind, name, root};
        v.sources_.push_back(std::move(s));
    };
    auto addGma = [&](const fs::path& file, std::uint64_t workshopId) {
        auto gma = readGma(file);
        if (!gma) {
            ++v.stats_.badGmas;
            v.warnings_.push_back(pathToUtf8(file.filename()) + ": " + gma.error().message);
            return;
        }
        Source s{SourceKind::Gma, pathToUtf8(file.filename()), file};
        s.workshopId = workshopId;
        s.title = sanitizeUtf8(gma->name);
        ++v.stats_.gmas;
        v.stats_.gmaFiles += gma->files.size();
        if (workshopId) {
            auto& w = v.workshop_[workshopId];
            if (w.state != WorkshopState::Installed)
                w = {WorkshopState::Installed, file, s.title};
        }
        s.gma = std::move(*gma);
        v.sources_.push_back(std::move(s));
    };
    auto addVpk = [&](SourceKind kind, const fs::path& dirFile) {
        if (!isFile(dirFile))
            return;
        auto bytes = readWholeFile(dirFile, kMaxVpkDirBytes);
        if (!bytes) {
            v.warnings_.push_back(pathToUtf8(dirFile.filename()) + ": " + bytes.error().message);
            return;
        }
        auto vpk = parseVpkDirectory(*bytes);
        if (!vpk) {
            v.warnings_.push_back(pathToUtf8(dirFile.filename()) + ": " + vpk.error().message);
            return;
        }
        Source s{kind, pathToUtf8(dirFile.filename()), dirFile};
        ++v.stats_.vpks;
        v.stats_.vpkFiles += vpk->files.size();
        s.vpk = std::move(*vpk);
        v.sources_.push_back(std::move(s));
    };
    auto addDirVpks = [&](SourceKind kind, const fs::path& dir) {
        for (const auto& e : listDir(dir))
            if (e.is_regular_file() && hasSuffix(e.path(), "_dir.vpk"))
                addVpk(kind, e.path());
    };

    // 1. garrysmod/ itself.
    addFolder(SourceKind::Folder, "garrysmod", g.garrysmod);
    // 2. Legacy addons: folders and loose GMAs in garrysmod/addons.
    for (const auto& e : listDir(g.garrysmod / "addons")) {
        if (e.is_directory()) {
            addFolder(SourceKind::AddonFolder, "addons/" + pathToUtf8(e.path().filename()), e.path());
            ++v.stats_.addonFolders;
        } else if (hasSuffix(e.path(), ".gma")) {
            addGma(e.path(), 0);
        }
    }
    // 3. Workshop GMAs: downloaded from servers (cache/workshop/<id>.gma) and subscribed
    // (content/4000/<id>/).
    for (const auto& e : listDir(g.garrysmod / "cache" / "workshop")) {
        if (!e.is_regular_file())
            continue;
        if (hasSuffix(e.path(), ".gma"))
            addGma(e.path(), parseId(pathToUtf8(e.path().stem())).value_or(0));
        else if (hasSuffix(e.path(), ".cache"))
            ++v.stats_.workshopCaches;
    }
    for (const auto& dir : listDir(g.workshopContent)) {
        if (!dir.is_directory())
            continue;
        const auto id = parseId(pathToUtf8(dir.path().filename()));
        for (const auto& e : listDir(dir.path())) {
            if (!e.is_regular_file())
                continue;
            if (hasSuffix(e.path(), ".gma")) {
                addGma(e.path(), id.value_or(0));
            } else if (hasSuffix(e.path(), "_legacy.bin")) {
                ++v.stats_.workshopLegacy;
                if (id && v.workshop_[*id].state != WorkshopState::Installed)
                    v.workshop_[*id] = {WorkshopState::Legacy, e.path(), {}};
            }
        }
    }
    // 4-5. Base game VPKs.
    addVpk(SourceKind::Vpk, g.garrysmod / "garrysmod_dir.vpk");
    addVpk(SourceKind::Vpk, g.garrysmod / "fallbacks_dir.vpk");
    addDirVpks(SourceKind::Vpk, g.gmodRoot / "sourceengine");
    addVpk(SourceKind::Vpk, g.gmodRoot / "platform" / "platform_misc_dir.vpk");
    // 6. Mounted games.
    for (const auto& m : g.mounts) {
        if (!m.found)
            continue;
        ++v.stats_.mounts;
        addFolder(SourceKind::MountFolder, m.name, m.path);
        addDirVpks(SourceKind::MountVpk, m.path);
    }
    // 7. Files downloaded from servers.
    addFolder(SourceKind::Download, "download", g.garrysmod / "download");
    v.stats_.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return v;
}

bool Vfs::contains(const Source& s, const std::string& path) const {
    switch (s.kind) {
    case SourceKind::Folder:
    case SourceKind::AddonFolder:
    case SourceKind::MountFolder:
    case SourceKind::Download:
        return isFile(s.root / pathFromUtf8(path));
    case SourceKind::Gma:
        return s.gma.files.count(path) != 0;
    case SourceKind::Vpk:
    case SourceKind::MountVpk:
        return s.vpk.files.count(path) != 0;
    case SourceKind::Pakfile:
        return false; // per-demo overlay, see ContentOverlay
    }
    return false;
}

std::optional<VfsHit> Vfs::find(std::string_view rawPath) const {
    const std::string path = normalizeGamePath(rawPath);
    if (path.empty() || path.find("..") != std::string::npos || path.find(':') != std::string::npos)
        return std::nullopt; // never resolve outside the search roots
    for (const auto& s : sources_)
        if (contains(s, path))
            return VfsHit{s.kind, s.name, s.workshopId, s.title};
    return std::nullopt;
}

std::optional<FileSlice> Vfs::locate(std::string_view rawPath) const {
    const std::string path = normalizeGamePath(rawPath);
    if (path.empty() || path.find("..") != std::string::npos || path.find(':') != std::string::npos)
        return std::nullopt;
    for (const auto& s : sources_) {
        if (!contains(s, path))
            continue;
        switch (s.kind) {
        case SourceKind::Folder:
        case SourceKind::AddonFolder:
        case SourceKind::MountFolder:
        case SourceKind::Download: {
            const fs::path p = s.root / pathFromUtf8(path);
            std::error_code ec;
            const auto size = fs::file_size(p, ec);
            if (ec)
                return std::nullopt;
            return FileSlice{p, 0, size};
        }
        case SourceKind::Gma: {
            const auto& e = s.gma.files.at(path);
            return FileSlice{s.root, e.offset, e.size};
        }
        case SourceKind::Vpk:
        case SourceKind::MountVpk: {
            const auto& e = s.vpk.files.at(path);
            if (e.preloadSize != 0)
                return std::nullopt; // split between the _dir file and an archive: not needed for maps
            if (e.archiveIndex == 0x7FFF)
                return FileSlice{s.root, static_cast<std::uint64_t>(s.vpk.treeEnd) + e.offset, e.length};
            std::string name = pathToUtf8(s.root.filename());
            // "_dir.vpk" -> "_NNN.vpk" (at least three digits; the index can have up to five).
            std::string num = std::to_string(e.archiveIndex);
            if (num.size() < 3)
                num.insert(0, 3 - num.size(), '0');
            name.replace(name.size() - 8, 8, "_" + num + ".vpk");
            return FileSlice{s.root.parent_path() / pathFromUtf8(name), e.offset, e.length};
        }
        case SourceKind::Pakfile:
            return std::nullopt;
        }
    }
    return std::nullopt;
}

WorkshopItem Vfs::workshopItem(std::uint64_t id) const {
    auto it = workshop_.find(id);
    return it == workshop_.end() ? WorkshopItem{} : it->second;
}

} // namespace gmdr::assets
