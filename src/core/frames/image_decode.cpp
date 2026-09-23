#include "image_decode.hpp"

#include "../media/ffmpeg_util.hpp"
#include "../util/strings.hpp"
#include "tga.hpp"
#include "../util/i18n.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gmdr::frames {

static_assert(kDecodePadding >= AV_INPUT_BUFFER_PADDING_SIZE, "запас у кінці буфера менший, ніж вимагає FFmpeg");

ImageFileType detect_image_type(const uint8_t* d, size_t n, const std::string& ext) {
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return ImageFileType::Jpeg;
    if (n >= 8 && std::memcmp(d, "\x89PNG\r\n\x1a\n", 8) == 0) return ImageFileType::Png;
    if (n >= 2 && d[0] == 'B' && d[1] == 'M') return ImageFileType::Bmp;
    if (ends_with_i(ext, "tga") || ends_with_i(ext, "tga\"")) return ImageFileType::Tga;
    // TGA не має "магічного" підпису; перевіряємо правдоподібність заголовка
    int w, h, bpp;
    size_t expected;
    if (tga_header_info(d, n, w, h, bpp, expected)) return ImageFileType::Tga;
    return ImageFileType::Unknown;
}

bool image_file_complete(const uint8_t* d, size_t n, const std::string& ext) {
    switch (detect_image_type(d, n, ext)) {
    case ImageFileType::Tga: {
        int w, h, bpp;
        size_t expected;
        if (!tga_header_info(d, n, w, h, bpp, expected)) return false;
        return expected == 0 || n >= expected;
    }
    case ImageFileType::Jpeg:
        return n >= 4 && d[n - 2] == 0xFF && d[n - 1] == 0xD9;
    case ImageFileType::Png:
        return n >= 12 && std::memcmp(d + n - 8, "IEND", 4) == 0;
    default:
        return n > 0;
    }
}

namespace {
// 8-бітні планарні YUV-формати, які можна віддати далі без перетворення.
bool planar_yuv8(AVPixelFormat f, PixelLayout& layout, bool& full_range) {
    switch (f) {
    case AV_PIX_FMT_YUVJ420P: layout = PixelLayout::YUV420P; full_range = true; return true;
    case AV_PIX_FMT_YUVJ422P: layout = PixelLayout::YUV422P; full_range = true; return true;
    case AV_PIX_FMT_YUVJ444P: layout = PixelLayout::YUV444P; full_range = true; return true;
    case AV_PIX_FMT_YUV420P: layout = PixelLayout::YUV420P; full_range = false; return true;
    case AV_PIX_FMT_YUV422P: layout = PixelLayout::YUV422P; full_range = false; return true;
    case AV_PIX_FMT_YUV444P: layout = PixelLayout::YUV444P; full_range = false; return true;
    default: return false;
    }
}

// Декодер зображень через FFmpeg (JPEG/PNG/BMP): один на потік.
struct FfImageDecoder {
    AVCodecID                id = AV_CODEC_ID_NONE;
    media::CodecCtxPtr       ctx;
    media::PacketPtr         pkt = media::make_packet();
    media::FramePtr          frame = media::make_frame();
    SwsContext*              sws = nullptr;
    int                      sws_w = 0, sws_h = 0, sws_fmt = -1;

    ~FfImageDecoder() { sws_freeContext(sws); }

    // data — файл, після якого в пам'яті є kDecodePadding нулів.
    bool decode(AVCodecID codec_id, const uint8_t* data, size_t size, Image& out, std::string* error, bool keep_yuv) {
        if (id != codec_id || !ctx) {
            const AVCodec* c = avcodec_find_decoder(codec_id);
            if (!c) {
                if (error) *error = tr("немає декодера зображень у FFmpeg");
                return false;
            }
            ctx.reset(avcodec_alloc_context3(c));
            ctx->thread_count = 1;
            if (avcodec_open2(ctx.get(), c, nullptr) < 0) {
                ctx.reset();
                if (error) *error = tr("не вдалося відкрити декодер зображень");
                return false;
            }
            id = codec_id;
        }
        pkt->data = const_cast<uint8_t*>(data);
        pkt->size = static_cast<int>(size);
        int r = avcodec_send_packet(ctx.get(), pkt.get());
        pkt->data = nullptr;
        pkt->size = 0;
        if (r >= 0) r = avcodec_receive_frame(ctx.get(), frame.get());
        if (r < 0) {
            avcodec_flush_buffers(ctx.get());
            if (error) *error = tr("помилка декодування зображення: ") + media::av_error_string(r);
            return false;
        }
        const int w = frame->width, h = frame->height;
        const auto fmt = static_cast<AVPixelFormat>(frame->format);
        PixelLayout yuv_layout;
        bool full_range = true;
        if (keep_yuv && planar_yuv8(fmt, yuv_layout, full_range)) {
            // JPEG уже в YUV — копіюємо площини як є (перетворення кольору зробить кодер,
            // одним проходом і в кілька потоків)
            out.allocate(w, h, yuv_layout);
            out.full_range = full_range || frame->color_range == AVCOL_RANGE_JPEG;
            out.bt709 = frame->colorspace == AVCOL_SPC_BT709;
            for (int p = 0; p < 3; ++p) {
                const size_t rb = out.row_bytes(p);
                const int ph = out.plane_height(p);
                for (int y = 0; y < ph; ++y)
                    std::memcpy(out.row(p, y), frame->data[p] + static_cast<ptrdiff_t>(y) * frame->linesize[p], rb);
            }
        } else {
            if (!sws || sws_w != w || sws_h != h || sws_fmt != frame->format) {
                sws_freeContext(sws);
                sws = sws_getContext(w, h, fmt, w, h, AV_PIX_FMT_BGR24, SWS_BICUBIC | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT,
                                     nullptr, nullptr, nullptr);
                sws_w = w;
                sws_h = h;
                sws_fmt = frame->format;
                if (!sws) {
                    av_frame_unref(frame.get());
                    if (error) *error = tr("не вдалося перетворити формат зображення");
                    return false;
                }
            }
            out.allocate(w, h, PixelLayout::BGR24);
            uint8_t* dst[4] = {out.row(0, 0), nullptr, nullptr, nullptr};
            int dst_stride[4] = {out.stride[0], 0, 0, 0};
            sws_scale(sws, frame->data, frame->linesize, 0, h, dst, dst_stride);
        }
        av_frame_unref(frame.get());
        avcodec_flush_buffers(ctx.get());
        return true;
    }
};

AVCodecID codec_for(ImageFileType t) {
    switch (t) {
    case ImageFileType::Jpeg: return AV_CODEC_ID_MJPEG;
    case ImageFileType::Png: return AV_CODEC_ID_PNG;
    case ImageFileType::Bmp: return AV_CODEC_ID_BMP;
    default: return AV_CODEC_ID_NONE;
    }
}
} // namespace

bool decode_image_buffer(PixelBytes&& bytes, size_t size, const std::string& ext, Image& out, std::string* error,
                         bool keep_yuv) {
    if (size > bytes.size()) {
        if (error) *error = tr("внутрішня помилка: розмір файлу більший за буфер");
        return false;
    }
    const ImageFileType type = detect_image_type(bytes.data(), size, ext);
    if (type == ImageFileType::Tga) {
        const int64_t index = out.index;
        out.release();
        out.data = std::move(bytes);
        out.index = index;
        return decode_tga_inplace(out, size, error);
    }
    const AVCodecID codec = codec_for(type);
    if (codec == AV_CODEC_ID_NONE) {
        if (error) *error = tr("невідомий формат файлу кадру");
        return false;
    }
    if (bytes.size() < size + kDecodePadding) {
        const size_t old = bytes.size();
        bytes.resize(size + kDecodePadding);
        if (bytes.size() > old) std::memset(bytes.data() + old, 0, bytes.size() - old);
    }
    std::memset(bytes.data() + size, 0, kDecodePadding);
    thread_local FfImageDecoder dec;
    const bool ok = dec.decode(codec, bytes.data(), size, out, error, keep_yuv);
    recycle_buffer(std::move(bytes));
    return ok;
}

bool decode_image_file(const std::vector<uint8_t>& bytes, const std::string& ext, Image& out, std::string* error,
                       bool keep_yuv) {
    PixelBytes buf = acquire_buffer(bytes.size() + kDecodePadding);
    if (!bytes.empty()) std::memcpy(buf.data(), bytes.data(), bytes.size());
    std::memset(buf.data() + bytes.size(), 0, kDecodePadding);
    return decode_image_buffer(std::move(buf), bytes.size(), ext, out, error, keep_yuv);
}

// ============================== Читання файлу ====================================
ReadStatus read_frame_file(const std::filesystem::path& path, const std::string& ext, bool delete_when_complete,
                           FrameFileRead& out) {
    out.size = 0;
    out.complete = false;
    out.deleted = false;
    out.error.clear();
#ifdef _WIN32
    // DELETE у правах доступу дозволяє видалити файл тим самим дескриптором. Якщо гра
    // ще тримає файл відкритим (без FILE_SHARE_DELETE), таке відкриття не вдасться —
    // тоді пробуємо звичайне читання, а видалимо пізніше.
    HANDLE h = INVALID_HANDLE_VALUE;
    bool can_delete = false;
    if (delete_when_complete) {
        h = CreateFileW(path.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        can_delete = h != INVALID_HANDLE_VALUE;
    }
    if (h == INVALID_HANDLE_VALUE)
        h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return ReadStatus::Missing;
        out.error = trf("файл зайнятий або недоступний (код {})", e);
        if (e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION || e == ERROR_ACCESS_DENIED)
            return ReadStatus::Locked;
        return ReadStatus::Error;
    }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || sz.QuadPart > (1ll << 31)) {
        CloseHandle(h);
        out.error = tr("не вдалося визначити розмір файлу");
        return ReadStatus::Error;
    }
    const size_t size = static_cast<size_t>(sz.QuadPart);
    out.bytes = acquire_buffer(size + kDecodePadding);
    size_t done = 0;
    while (done < size) {
        DWORD got = 0;
        const DWORD want = static_cast<DWORD>(std::min<size_t>(size - done, 64u << 20));
        if (!ReadFile(h, out.bytes.data() + done, want, &got, nullptr)) {
            CloseHandle(h);
            out.error = trf("помилка читання файлу (код {})", GetLastError());
            return ReadStatus::Error;
        }
        if (got == 0) break;   // файл коротший, ніж був на момент запиту розміру
        done += got;
    }
    out.size = done;
    std::memset(out.bytes.data() + done, 0, kDecodePadding);
    out.complete = done > 0 && image_file_complete(out.bytes.data(), done, ext);
    if (out.complete && can_delete) {
        FILE_DISPOSITION_INFO info{};
        info.DeleteFile = TRUE;
        out.deleted = SetFileInformationByHandle(h, FileDispositionInfo, &info, sizeof(info)) != 0;
    }
    CloseHandle(h);
    return ReadStatus::Ok;
#else
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return ReadStatus::Missing;
        out.error = tr("не вдалося відкрити файл");
        return ReadStatus::Locked;
    }
    f.seekg(0, std::ios::end);
    const std::streamoff s = f.tellg();
    if (s < 0) {
        out.error = tr("не вдалося визначити розмір файлу");
        return ReadStatus::Error;
    }
    f.seekg(0, std::ios::beg);
    const size_t size = static_cast<size_t>(s);
    out.bytes = acquire_buffer(size + kDecodePadding);
    f.read(reinterpret_cast<char*>(out.bytes.data()), static_cast<std::streamsize>(size));
    const size_t done = static_cast<size_t>(std::max<std::streamsize>(0, f.gcount()));
    out.size = done;
    std::memset(out.bytes.data() + done, 0, kDecodePadding);
    out.complete = done > 0 && image_file_complete(out.bytes.data(), done, ext);
    f.close();
    if (out.complete && delete_when_complete) {
        std::error_code ec;
        out.deleted = std::filesystem::remove(path, ec);
    }
    return ReadStatus::Ok;
#endif
}

} // namespace gmdr::frames
