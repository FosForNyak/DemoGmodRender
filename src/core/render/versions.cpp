#include "versions.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "../util/file_util.hpp"
#include "../util/strings.hpp"
#include "../util/log.hpp"
#include "derived.hpp"
#include "jobs.hpp"
#include "../util/i18n.hpp"

namespace gmdr::render {

namespace fs = std::filesystem;

const std::vector<VersionPreset>& version_presets() {
    static const std::vector<VersionPreset> presets = {
        {"discord", tr("Discord (до 10 МБ)"), tr("H.264 до 720p, файл до ~10 МБ — влізе в Discord без Nitro")},
        {"480p", tr("Легка копія 480p"), tr("H.264 480p — швидко переслати або дивитися з телефона")},
        {"vertical", tr("Вертикальне 9:16"), tr("центр кадру, 1080×1920 — для YouTube Shorts, TikTok, Reels")},
        {"master", tr("Для монтажу (ProRes)"), tr("ProRes 422 HQ у MOV, звук без стиснення — для Premiere, DaVinci Resolve")},
        {"thumb", tr("Обкладинка (JPG)"), tr("найвиразніший кадр біля середини відео, до 1280×720 — для YouTube чи прев'ю"), true},
        {"gif", "GIF", tr("перші 15 с, 480 пікселів завширшки, 15 кадрів/с — для чатів і форумів"), true},
        {"webp", tr("WebP-анімація"), tr("перші 15 с, 640 пікселів, 20 кадрів/с — менша й якісніша за GIF"), true},
    };
    return presets;
}

bool valid_version_ids(const std::string& ids, std::string* unknown) {
    for (const auto& raw : split(ids, ',')) {
        const std::string id = trim(raw);
        if (id.empty()) continue;
        const auto& p = version_presets();
        if (std::none_of(p.begin(), p.end(), [&](const VersionPreset& v) { return v.id == id; })) {
            if (unknown) *unknown = id;
            return false;
        }
    }
    return true;
}

namespace {
int even(double v) { return std::max(2, static_cast<int>(std::lround(v / 2.0)) * 2); }

// Файл версії поруч з основним: відео.mp4 -> відео_discord.mp4
std::string version_path(const std::string& main_path, const std::string& id, const std::string& ext) {
    fs::path p = path_from_utf8(main_path);
    std::string stem = path_to_utf8(p.stem());
    // Основний вихід — послідовність зображень (кадр_%05d.png): версії називаємо "video_*"
    if (stem.find('%') != std::string::npos) stem = "video";
    return path_to_utf8(p.parent_path() / path_from_utf8(stem + "_" + id + "." + ext));
}

media::VideoEncoderSettings h264(const EncodeSettings& main, int w, int h, const char* preset) {
    media::VideoEncoderSettings v;
    v.codec = "libx264";
    v.width = w;
    v.height = h;
    v.fps = main.video.fps;
    v.preset = preset;
    v.scaler = main.video.scaler;
    v.threads = main.video.threads;
    v.gop_seconds = 2;
    return v;
}

media::AudioEncoderSettings aac(int64_t bitrate) {
    media::AudioEncoderSettings a;
    a.codec = "aac";
    a.bitrate = bitrate;
    return a;
}
} // namespace

std::vector<ExtraOutput> make_extra_outputs(const std::string& ids, const EncodeSettings& main, double seconds) {
    std::vector<ExtraOutput> out;
    const int W = main.video.width > 0 ? main.video.width : 1920;
    const int H = main.video.height > 0 ? main.video.height : 1080;
    for (const auto& raw : split(ids, ',')) {
        const std::string id = trim(raw);
        ExtraOutput x;
        if (id == "discord") {
            const int h = even(std::min(720, H));
            x.label = tr("Discord (до 10 МБ)");
            x.video = h264(main, even(static_cast<double>(h) * W / H), h, "medium");
            x.audio = aac(128000);
            // 9.5 МБ, а не 10: запас на контейнер і на неточність бітрейту кодека
            if (seconds > 0) x.video.bitrate = bitrate_for_target_size(9.5, seconds, x.audio.bitrate);
            else x.video.quality = 26;
            x.output_path = version_path(main.output_path, id, "mp4");
        } else if (id == "480p") {
            const int h = even(std::min(480, H));
            x.label = tr("Легка копія 480p");
            x.video = h264(main, even(static_cast<double>(h) * W / H), h, "fast");
            x.video.quality = 24;
            x.audio = aac(160000);
            x.output_path = version_path(main.output_path, id, "mp4");
        } else if (id == "vertical") {
            const int h = H >= 1080 ? 1920 : 1280;
            x.label = tr("Вертикальне 9:16");
            x.video = h264(main, h * 9 / 16, h, "medium");
            x.video.crop_aspect = 9.0 / 16.0;
            x.video.quality = 20;
            x.audio = aac(192000);
            x.output_path = version_path(main.output_path, id, "mp4");
        } else if (id == "master") {
            x.label = tr("Для монтажу (ProRes)");
            x.video.codec = "prores_ks";
            x.video.width = W;
            x.video.height = H;
            x.video.fps = main.video.fps;
            x.video.quality = 3;   // профіль HQ
            x.video.bit_depth = 10;
            x.video.chroma = 422;
            x.video.scaler = main.video.scaler;
            x.video.threads = main.video.threads;
            x.video.gop_seconds = 0;
            x.audio.codec = "pcm_s16le";
            x.audio.bitrate = 0;
            x.output_path = version_path(main.output_path, id, "mov");
        } else {
            continue;
        }
        out.push_back(std::move(x));
    }
    return out;
}

std::vector<std::string> make_post_versions(const std::string& ids, const std::string& main_path) {
    std::vector<std::string> made;
    fs::path p = path_from_utf8(main_path);
    const std::string stem = path_to_utf8(p.stem());
    auto sibling = [&](const char* ext) { return path_to_utf8(p.parent_path() / path_from_utf8(stem + ext)); };
    for (const auto& raw : split(ids, ',')) {
        const std::string id = trim(raw);
        if (id != "thumb" && id != "gif" && id != "webp") continue;
        if (stem.find('%') != std::string::npos) {
            log_warn("{}", trf("Обкладинка й анімації — лише для відеофайлу, не для послідовності зображень"));
            break;
        }
        std::string out, err;
        bool ok = false;
        if (id == "thumb") {
            out = sibling(".jpg");
            ok = make_thumbnail(main_path, out, -1, 1280, 720, &err);
        } else if (id == "gif") {
            out = sibling(".gif");
            ok = make_animation(main_path, out, AnimFormat::Gif, 15.0, 480, 15, &err);
        } else {
            out = sibling(".webp");
            ok = make_animation(main_path, out, AnimFormat::WebP, 15.0, 640, 20, &err);
        }
        if (ok) {
            made.push_back(out);
        } else {
            std::error_code ec;
            fs::remove(path_from_utf8(out), ec);
            log_warn("{}", trf("Не вдалося зробити {}: {}", out, err));
        }
    }
    return made;
}

} // namespace gmdr::render
