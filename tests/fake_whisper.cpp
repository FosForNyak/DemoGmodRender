// =============================================================================
//  fake_whisper.cpp — ІМІТАТОР whisper-cli для тестів (без моделі й нейромережі).
//
//  Приймає ті самі параметри (-m -f -l -oj -of -pp), читає WAV 16 кГц і кожну
//  ділянку звуку (між паузами ≥ 0,4 с) "розпізнає" як "фраза N". У тиші після
//  останньої фрази додає типову галюцинацію Whisper ("Субтитры сделал...") —
//  програма має її відкинути. Результат — JSON у форматі whisper-cli -oj.
// =============================================================================
#include "core/audio/wav.hpp"
#include "core/util/file_util.hpp"
#include "core/util/json.hpp"
#include "core/util/strings.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace gmdr;

int main(int argc, char** argv) {
    std::string model, file, out, lang = "en";
    bool json = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "-m") model = next();
        else if (a == "-f") file = next();
        else if (a == "-of") out = next();
        else if (a == "-l") lang = next();
        else if (a == "-oj") json = true;
        else if (a == "-t") next();
    }
    std::error_code ec;
    if (model.empty() || !std::filesystem::exists(path_from_utf8(model), ec)) {
        std::printf("error: failed to open model '%s'\n", model.c_str());
        return 2;
    }
    audio::WavReader wav;
    std::string err;
    if (!wav.open(path_from_utf8(file), false, &err) || wav.sample_rate() != 16000 || wav.channels() != 1) {
        std::printf("error: failed to read audio '%s' (потрібно 16 кГц моно)\n", file.c_str());
        return 3;
    }
    std::vector<float> x(static_cast<size_t>(wav.frames_available()));
    x.resize(wav.read(x.data(), x.size()));
    std::printf("main: processing '%s' (%zu samples, %.1f sec), lang = %s\n", file.c_str(), x.size(), x.size() / 16000.0,
                lang.c_str());
    // Ділянки звуку: вікна по 10 мс з RMS > 0.005, паузи < 0.4 с зливаються
    struct Seg {
        double a, b;
    };
    std::vector<Seg> segs;
    constexpr size_t kWin = 160;
    for (size_t i = 0; i + kWin <= x.size(); i += kWin) {
        double e = 0;
        for (size_t k = 0; k < kWin; ++k) e += x[i + k] * x[i + k];
        if (std::sqrt(e / kWin) < 0.005) continue;
        const double t = i / 16000.0;
        if (!segs.empty() && t - segs.back().b < 0.4) segs.back().b = t + 0.01;
        else segs.push_back({t, t + 0.01});
    }
    json::Value tr = json::Value::array();
    auto add = [&](double a, double b, const std::string& text) {
        json::Value o = json::Value::object();
        json::Value off = json::Value::object();
        off.set("from", json::Value::number(std::round(a * 1000)));
        off.set("to", json::Value::number(std::round(b * 1000)));
        o.set("offsets", off);
        o.set("text", json::Value::string(" " + text));
        tr.push(o);
    };
    for (size_t i = 0; i < segs.size(); ++i) {
        add(segs[i].a, segs[i].b, "фраза " + std::to_string(i + 1));
        std::printf("whisper_print_progress_callback: progress = %3d%%\n", static_cast<int>((i + 1) * 100 / segs.size()));
    }
    const double total = x.size() / 16000.0;
    if (!segs.empty() && total - segs.back().b > 0.5)
        add(segs.back().b + 0.1, std::min(total, segs.back().b + 0.9), "Субтитры сделал DimaTorzok");
    if (!json) return 0;
    json::Value root = json::Value::object();
    json::Value res = json::Value::object();
    res.set("language", json::Value::string(lang == "auto" ? "uk" : lang));
    root.set("result", res);
    root.set("transcription", tr);
    if (!write_file_text(path_from_utf8(out + ".json"), root.dump(), &err)) {
        std::printf("error: %s\n", err.c_str());
        return 4;
    }
    std::printf("output_json: saving output to '%s.json'\n", out.c_str());
    return 0;
}
