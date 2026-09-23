#include "edit_package.hpp"

#include <cmath>
#include <format>

#include "../util/strings.hpp"

namespace gmdr::render {

namespace {
std::string xml_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        default: o += c;
        }
    }
    return o;
}

// Частота кадрів у xmeml: ціле timebase + ntsc (29.97 = 30 NTSC, 59.94 = 60 NTSC)
std::string rate_xml(int num, int den) {
    const bool ntsc = den == 1001;
    const int timebase = ntsc ? static_cast<int>(std::lround(num / 1000.0))
                              : static_cast<int>(std::lround(static_cast<double>(num) / std::max(1, den)));
    return std::format("<rate><timebase>{}</timebase><ntsc>{}</ntsc></rate>", timebase, ntsc ? "TRUE" : "FALSE");
}

std::string file_name(const std::string& path) {
    const size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}
} // namespace

std::string fcp_path_url(const std::string& path) {
    std::string out = "file://localhost/";
    static const char* hex = "0123456789abcdef";
    for (unsigned char c : path) {
        if (c == '\\') c = '/';
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' ||
                           c == '-' || c == '_' || c == '.' || c == '~';
        if (plain) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string safe_file_name(const std::string& title) {
    std::string o;
    for (char c : title) o += std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos || (c >= 0 && c < 32) ? '_' : c;
    o = trim(o);
    while (!o.empty() && (o.back() == '.' || o.back() == ' ')) o.pop_back();   // Windows не любить крапку в кінці
    return o.empty() ? "доріжка" : o;
}

std::string make_fcp7_xml(const EditProject& p) {
    const std::string rate = rate_xml(p.fps_num, p.fps_den);
    const int64_t n = std::max<int64_t>(1, p.frames);
    const double fps = static_cast<double>(p.fps_num) / std::max(1, p.fps_den);
    std::string x;
    x += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE xmeml>\n<xmeml version=\"5\">\n";
    x += std::format("<sequence id=\"sequence-1\">\n<name>{}</name>\n<duration>{}</duration>\n{}\n", xml_escape(p.name), n, rate);
    x += "<timecode>" + rate + "<string>00:00:00:00</string><frame>0</frame><displayformat>NDF</displayformat></timecode>\n";
    for (size_t i = 0; i < p.markers.size(); ++i)
        x += std::format("<marker><name>{}</name><comment></comment><in>{}</in><out>-1</out></marker>\n",
                         xml_escape(p.markers[i].title), static_cast<int64_t>(std::llround(p.markers[i].start * fps)));
    x += "<media>\n<video>\n<format><samplecharacteristics>" + rate +
         std::format("<width>{}</width><height>{}</height><pixelaspectratio>square</pixelaspectratio>"
                     "<fielddominance>none</fielddominance></samplecharacteristics></format>\n",
                     p.width, p.height);
    // V1: відео
    const std::string vname = file_name(p.video_path);
    x += "<track>\n<clipitem id=\"clipitem-video\">\n";
    x += std::format("<name>{}</name><enabled>TRUE</enabled><duration>{}</duration>{}<start>0</start><end>{}</end><in>0</in><out>{}</out>\n",
                     xml_escape(vname), n, rate, n, n);
    x += std::format("<file id=\"file-video\"><name>{}</name><pathurl>{}</pathurl>{}<duration>{}</duration>", xml_escape(vname),
                     xml_escape(fcp_path_url(p.video_path)), rate, n);
    x += "<media><video><samplecharacteristics>" + rate +
         std::format("<width>{}</width><height>{}</height></samplecharacteristics></video>", p.width, p.height);
    if (p.video_has_audio)
        x += "<audio><samplecharacteristics><depth>16</depth><samplerate>48000</samplerate></samplecharacteristics>"
             "<channelcount>2</channelcount></audio>";
    x += "</media></file>\n</clipitem>\n</track>\n</video>\n<audio>\n";
    auto audio_clip = [&](const std::string& id, const std::string& name, const std::string& file_xml) {
        x += "<track>\n<clipitem id=\"" + id + "\">\n";
        x += std::format("<name>{}</name><enabled>TRUE</enabled><duration>{}</duration>{}<start>0</start><end>{}</end><in>0</in><out>{}</out>\n",
                         xml_escape(name), n, rate, n, n);
        x += file_xml;
        x += "<sourcetrack><mediatype>audio</mediatype><trackindex>1</trackindex></sourcetrack>\n</clipitem>\n</track>\n";
    };
    // A1: звук самого відео (загальний мікс) — посилання на той самий файл
    if (p.video_has_audio) audio_clip("clipitem-mix", "Мікс (з відео)", "<file id=\"file-video\"/>\n");
    // Далі — окремі WAV: гра, гравці, мікрофон
    for (size_t i = 0; i < p.stems.size(); ++i) {
        const auto& st = p.stems[i];
        const std::string fid = std::format("file-stem-{}", i + 1);
        const std::string file = std::format(
            "<file id=\"{}\"><name>{}</name><pathurl>{}</pathurl>{}<duration>{}</duration><media><audio>"
            "<samplecharacteristics><depth>24</depth><samplerate>48000</samplerate></samplecharacteristics>"
            "<channelcount>2</channelcount></audio></media></file>\n",
            fid, xml_escape(file_name(st.path)), xml_escape(fcp_path_url(st.path)), rate, n);
        audio_clip(std::format("clipitem-stem-{}", i + 1), st.title, file);
    }
    x += "</audio>\n</media>\n</sequence>\n</xmeml>\n";
    return x;
}

} // namespace gmdr::render
