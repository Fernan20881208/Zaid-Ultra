#include "InstantReplay.hpp"

#include "../core/RootExecutor.hpp"
#include "../core/Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <utility>

#ifdef GEODE_IS_ANDROID
#include <dlfcn.h>
#include <fcntl.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace geode::prelude;

namespace zaid::ultra {
namespace {

constexpr std::int64_t kReplayWindowUs = 60'000'000;
// Keep one extra 10-second GOP plus a small scheduling margin. A saved clip
// still starts at a keyframe at or after the 60-second cutoff, so it never
// contains video older than requested.
constexpr std::int64_t kRingRetentionUs = 72'000'000;
constexpr std::size_t kMaximumRingBytes = 96 * 1024 * 1024;
constexpr int kCaptureWidth = 1280;
constexpr int kCaptureHeight = 720;
constexpr int kCaptureBitRate = 8'000'000;
constexpr int kNominalFrameRate = 120;
constexpr char kPidFile[] = "/data/local/tmp/zaid-ultra-replay-screenrecord.pid";
constexpr char kErrorFile[] = "/data/local/tmp/zaid-ultra-replay-screenrecord.err";

std::int64_t monotonicUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

std::size_t startCodeSize(std::vector<std::uint8_t> const& nal) {
    if (nal.size() >= 4 && nal[0] == 0 && nal[1] == 0 && nal[2] == 0 && nal[3] == 1) {
        return 4;
    }
    if (nal.size() >= 3 && nal[0] == 0 && nal[1] == 0 && nal[2] == 1) {
        return 3;
    }
    return 0;
}

int nalType(std::vector<std::uint8_t> const& nal) {
    auto offset = startCodeSize(nal);
    return offset < nal.size() ? nal[offset] & 0x1f : -1;
}

std::size_t findStartCode(std::vector<std::uint8_t> const& bytes, std::size_t from) {
    if (bytes.size() < 3 || from >= bytes.size() - 2) {
        return std::string::npos;
    }
    for (auto index = from; index + 2 < bytes.size(); ++index) {
        if (bytes[index] != 0 || bytes[index + 1] != 0) {
            continue;
        }
        if (bytes[index + 2] == 1 ||
            (index + 3 < bytes.size() && bytes[index + 2] == 0 && bytes[index + 3] == 1)) {
            return index;
        }
    }
    return std::string::npos;
}

std::vector<std::uint8_t> normalizeNal(
    std::vector<std::uint8_t> const& source,
    std::size_t begin,
    std::size_t end
) {
    auto codeSize = (begin + 3 < end && source[begin + 2] == 0 && source[begin + 3] == 1) ? 4u : 3u;
    std::vector<std::uint8_t> result{0, 0, 0, 1};
    result.insert(result.end(), source.begin() + static_cast<std::ptrdiff_t>(begin + codeSize),
        source.begin() + static_cast<std::ptrdiff_t>(end));
    return result;
}

std::string readSmallFile(std::filesystem::path const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    std::string value((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (value.size() > 4096) {
        value.resize(4096);
    }
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

std::string timestampedName(char const* extension) {
    auto now = std::time(nullptr);
    std::tm time{};
#ifdef GEODE_IS_ANDROID
    localtime_r(&now, &time);
#else
    if (auto* converted = std::localtime(&now)) {
        time = *converted;
    }
#endif
    std::array<char, 96> value{};
    std::strftime(value.data(), value.size(), "Zaid-Ultra-Replay-%Y%m%d-%H%M%S", &time);
    return std::string(value.data()) + extension;
}

void notifyOnMainThread(std::string message, NotificationIcon icon) {
    Loader::get()->queueInMainThread([message = std::move(message), icon] {
        Notification::create(message, icon)->show();
    });
}

#ifdef GEODE_IS_ANDROID

struct MediaMuxerApi final {
    using NewMuxer = AMediaMuxer* (*)(int, OutputFormat);
    using DeleteMuxer = media_status_t (*)(AMediaMuxer*);
    using AddTrack = ssize_t (*)(AMediaMuxer*, AMediaFormat const*);
    using StartMuxer = media_status_t (*)(AMediaMuxer*);
    using StopMuxer = media_status_t (*)(AMediaMuxer*);
    using WriteSample = media_status_t (*)(
        AMediaMuxer*, std::size_t, std::uint8_t const*, AMediaCodecBufferInfo const*
    );
    using NewFormat = AMediaFormat* (*)();
    using DeleteFormat = media_status_t (*)(AMediaFormat*);
    using SetString = void (*)(AMediaFormat*, char const*, char const*);
    using SetInt32 = void (*)(AMediaFormat*, char const*, std::int32_t);
    using SetBuffer = void (*)(AMediaFormat*, char const*, void const*, std::size_t);

    void* library = nullptr;
    NewMuxer newMuxer = nullptr;
    DeleteMuxer deleteMuxer = nullptr;
    AddTrack addTrack = nullptr;
    StartMuxer startMuxer = nullptr;
    StopMuxer stopMuxer = nullptr;
    WriteSample writeSample = nullptr;
    NewFormat newFormat = nullptr;
    DeleteFormat deleteFormat = nullptr;
    SetString setString = nullptr;
    SetInt32 setInt32 = nullptr;
    SetBuffer setBuffer = nullptr;

    ~MediaMuxerApi() {
        if (library) {
            ::dlclose(library);
        }
    }

    bool load(std::string& error) {
        library = ::dlopen("libmediandk.so", RTLD_NOW | RTLD_LOCAL);
        if (!library) {
            error = fmt::format("libmediandk no disponible: {}", ::dlerror());
            return false;
        }
#define ZU_LOAD_MEDIA(name, symbol) \
        name = reinterpret_cast<decltype(name)>(::dlsym(library, symbol)); \
        if (!name) { error = fmt::format("falta {} en libmediandk", symbol); return false; }
        ZU_LOAD_MEDIA(newMuxer, "AMediaMuxer_new")
        ZU_LOAD_MEDIA(deleteMuxer, "AMediaMuxer_delete")
        ZU_LOAD_MEDIA(addTrack, "AMediaMuxer_addTrack")
        ZU_LOAD_MEDIA(startMuxer, "AMediaMuxer_start")
        ZU_LOAD_MEDIA(stopMuxer, "AMediaMuxer_stop")
        ZU_LOAD_MEDIA(writeSample, "AMediaMuxer_writeSampleData")
        ZU_LOAD_MEDIA(newFormat, "AMediaFormat_new")
        ZU_LOAD_MEDIA(deleteFormat, "AMediaFormat_delete")
        ZU_LOAD_MEDIA(setString, "AMediaFormat_setString")
        ZU_LOAD_MEDIA(setInt32, "AMediaFormat_setInt32")
        ZU_LOAD_MEDIA(setBuffer, "AMediaFormat_setBuffer")
#undef ZU_LOAD_MEDIA
        return true;
    }
};

bool muxAvcToMp4(
    std::filesystem::path const& output,
    std::vector<InstantReplay::EncodedFrame> const& frames,
    std::vector<std::uint8_t> const& sps,
    std::vector<std::uint8_t> const& pps,
    std::string& error
) {
    MediaMuxerApi api;
    if (!api.load(error)) {
        return false;
    }

    auto outputString = output.string();
    auto fd = ::open(outputString.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644);
    if (fd < 0) {
        error = fmt::format("no se pudo crear MP4: {}", std::strerror(errno));
        return false;
    }

    auto* muxer = api.newMuxer(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
    if (!muxer) {
        ::close(fd);
        error = "AMediaMuxer_new falló";
        return false;
    }
    auto* format = api.newFormat();
    if (!format) {
        api.deleteMuxer(muxer);
        ::close(fd);
        error = "AMediaFormat_new falló";
        return false;
    }

    api.setString(format, "mime", "video/avc");
    api.setInt32(format, "width", kCaptureWidth);
    api.setInt32(format, "height", kCaptureHeight);
    api.setInt32(format, "bitrate", kCaptureBitRate);
    api.setInt32(format, "frame-rate", kNominalFrameRate);
    api.setInt32(format, "i-frame-interval", 10);
    api.setBuffer(format, "csd-0", sps.data(), sps.size());
    api.setBuffer(format, "csd-1", pps.data(), pps.size());

    auto track = api.addTrack(muxer, format);
    api.deleteFormat(format);
    if (track < 0 || api.startMuxer(muxer) != AMEDIA_OK) {
        api.deleteMuxer(muxer);
        ::close(fd);
        error = "MediaMuxer no aceptó la pista AVC";
        return false;
    }

    bool ok = true;
    auto basePts = frames.front().ptsUs;
    std::int64_t previousPts = -1;
    for (auto const& frame : frames) {
        auto pts = std::max<std::int64_t>(frame.ptsUs - basePts, previousPts + 1);
        previousPts = pts;
        AMediaCodecBufferInfo info{};
        info.offset = 0;
        info.size = static_cast<std::int32_t>(std::min<std::size_t>(
            frame.data.size(), static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())
        ));
        info.presentationTimeUs = pts;
        info.flags = frame.keyFrame ? AMEDIACODEC_BUFFER_FLAG_KEY_FRAME : 0;
        if (api.writeSample(
                muxer,
                static_cast<std::size_t>(track),
                frame.data.data(),
                &info
            ) != AMEDIA_OK) {
            ok = false;
            error = "MediaMuxer rechazó un frame AVC";
            break;
        }
    }

    auto stopResult = api.stopMuxer(muxer);
    api.deleteMuxer(muxer);
    ::close(fd);
    if (stopResult != AMEDIA_OK && ok) {
        error = "MediaMuxer no pudo finalizar el MP4";
        ok = false;
    }
    return ok;
}

#endif

bool writeRawAvc(
    std::filesystem::path const& output,
    std::vector<InstantReplay::EncodedFrame> const& frames,
    std::vector<std::uint8_t> const& sps,
    std::vector<std::uint8_t> const& pps,
    std::string& error
) {
    std::ofstream file(output, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "no se pudo crear el respaldo H.264";
        return false;
    }
    file.write(reinterpret_cast<char const*>(sps.data()), static_cast<std::streamsize>(sps.size()));
    file.write(reinterpret_cast<char const*>(pps.data()), static_cast<std::streamsize>(pps.size()));
    for (auto const& frame : frames) {
        file.write(
            reinterpret_cast<char const*>(frame.data.data()),
            static_cast<std::streamsize>(frame.data.size())
        );
    }
    if (!file.good()) {
        error = "falló la escritura del respaldo H.264";
        return false;
    }
    return true;
}

} // namespace

InstantReplay& InstantReplay::get() {
    static InstantReplay instance;
    return instance;
}

InstantReplay::~InstantReplay() {
    m_stopRequested.store(true, std::memory_order_release);
    if (m_captureThread.joinable()) {
        requestRecorderStop();
        m_captureThread.join();
    }
    if (m_saveThread.joinable()) {
        m_saveThread.join();
    }
}

void InstantReplay::beginGameplay() {
    auto enabled = settings::enabled("instant-replay");
    {
        std::lock_guard lock(m_mutex);
        m_status.enabled = enabled;
        if (!enabled) {
            m_status.summary = "desactivado en ajustes";
            return;
        }
        if (m_status.starting || m_status.buffering) {
            return;
        }
    }

    if (m_captureThread.joinable()) {
        m_captureThread.join();
    }

    std::uint64_t generation = 0;
    {
        std::lock_guard lock(m_mutex);
        generation = ++m_generation;
        m_frames.clear();
        m_sps.clear();
        m_pps.clear();
        m_currentAccessUnit.clear();
        m_currentHasVcl = false;
        m_currentKeyFrame = false;
        m_ringBytes = 0;
        m_firstPtsUs = 0;
        m_lastPtsUs = 0;
        m_status.starting = true;
        m_status.buffering = false;
        m_status.videoSupported = false;
        m_status.audioIncluded = false;
        m_status.bufferedSeconds = 0.0;
        m_status.bufferedMiB = 0.0;
        m_status.frameCount = 0;
        m_status.summary = "iniciando encoder H.264 ROOT...";
        m_status.lastError.clear();
    }
    m_stopRequested.store(false, std::memory_order_release);
    m_captureThread = std::thread([this, generation] { captureLoop(generation); });
}

void InstantReplay::endGameplay() {
    m_stopRequested.store(true, std::memory_order_release);
    std::lock_guard lock(m_mutex);
    if (m_status.starting || m_status.buffering) {
        m_status.summary = "deteniendo capturador...";
    }
}

bool InstantReplay::saveLast60Seconds() {
    std::vector<EncodedFrame> snapshot;
    std::vector<std::uint8_t> sps;
    std::vector<std::uint8_t> pps;

    {
        std::lock_guard lock(m_mutex);
        m_status.enabled = settings::enabled("instant-replay");
        if (!m_status.enabled) {
            m_status.lastError = "activa Instant Replay en los ajustes de Zaid-Ultra";
            notifyOnMainThread(m_status.lastError, NotificationIcon::Error);
            return false;
        }
        if (m_status.saving) {
            notifyOnMainThread("Ya se está guardando un clip", NotificationIcon::Info);
            return false;
        }
        if (!m_status.buffering || m_frames.empty() || m_sps.empty() || m_pps.empty()) {
            m_status.lastError = "el búfer todavía no tiene vídeo decodificable";
            notifyOnMainThread(m_status.lastError, NotificationIcon::Error);
            return false;
        }

        auto cutoff = m_frames.back().ptsUs - kReplayWindowUs;
        auto start = m_frames.end();
        for (auto iterator = m_frames.begin(); iterator != m_frames.end(); ++iterator) {
            if (iterator->keyFrame && iterator->ptsUs >= cutoff) {
                start = iterator;
                break;
            }
        }
        if (start == m_frames.end()) {
            for (auto iterator = m_frames.end(); iterator != m_frames.begin();) {
                --iterator;
                if (iterator->keyFrame) {
                    start = iterator;
                    break;
                }
            }
        }
        if (start == m_frames.end()) {
            m_status.lastError = "aún no llegó un keyframe AVC";
            notifyOnMainThread(m_status.lastError, NotificationIcon::Error);
            return false;
        }

        snapshot.assign(start, m_frames.end());
        sps = m_sps;
        pps = m_pps;
        m_status.saving = true;
        m_status.summary = fmt::format("guardando {} frames...", snapshot.size());
        m_status.lastError.clear();
    }

    if (m_saveThread.joinable()) {
        m_saveThread.join();
    }
    m_saveThread = std::thread([
        this,
        frames = std::move(snapshot),
        sps = std::move(sps),
        pps = std::move(pps)
    ]() mutable {
        auto saveDir = Mod::get()->getSaveDir() / "replay-work";
        std::error_code filesystemError;
        std::filesystem::create_directories(saveDir, filesystemError);
        if (filesystemError) {
            setFailure(fmt::format("no se pudo crear replay-work: {}", filesystemError.message()), false);
            notifyOnMainThread("Falló Guardar Clip; revisa la consola ZU", NotificationIcon::Error);
            return;
        }

        auto mp4Name = timestampedName(".mp4");
        auto localOutput = saveDir / mp4Name;
        std::string error;
        bool mp4 = false;
#ifdef GEODE_IS_ANDROID
        mp4 = muxAvcToMp4(localOutput, frames, sps, pps, error);
#endif
        std::string finalName = mp4Name;
        if (!mp4) {
            std::filesystem::remove(localOutput, filesystemError);
            finalName = timestampedName(".h264");
            localOutput = saveDir / finalName;
            auto muxError = error;
            if (!writeRawAvc(localOutput, frames, sps, pps, error)) {
                setFailure(fmt::format("MP4: {}; H264: {}", muxError, error), false);
                notifyOnMainThread("Falló Guardar Clip; revisa la consola ZU", NotificationIcon::Error);
                return;
            }
            if (settings::diagnostics()) {
                log::warn("Replay MP4 unavailable ({}); saved raw AVC fallback", muxError);
            }
        }

        auto localString = localOutput.string();
        auto publicPath = std::string("/sdcard/Download/") + finalName;
        auto script = "mkdir -p /sdcard/Download && cp " + shellQuote(localString) + " " +
            shellQuote(publicPath) + " && chmod 0644 " + shellQuote(publicPath);
        // This is deliberately owned by the save thread rather than queued as
        // a detached callback: process shutdown can then join this thread and
        // cannot leave a callback referencing a destroyed replay singleton.
        auto result = RootExecutor::get().runRoot(script);
        std::error_code removeError;
        std::filesystem::remove(localString, removeError);
        if (!result.ok()) {
            setFailure(
                fmt::format("no se pudo copiar a Download (exit={}): {}", result.exitCode, result.output),
                false
            );
            notifyOnMainThread("Clip creado, pero no se pudo copiar a Download", NotificationIcon::Error);
            return;
        }
        {
            std::lock_guard lock(m_mutex);
            m_status.saving = false;
            m_status.lastFile = publicPath;
            m_status.lastError.clear();
            m_status.summary = fmt::format(
                "clip {} guardado{}",
                finalName,
                mp4 ? "" : " (H.264 crudo)"
            );
        }
        notifyOnMainThread(
            mp4 ? "Replay guardado en Download" : "Replay H.264 guardado en Download",
            NotificationIcon::Success
        );
    });
    return true;
}

InstantReplayStatus InstantReplay::status() const {
    std::lock_guard lock(m_mutex);
    return m_status;
}

void InstantReplay::captureLoop(std::uint64_t generation) {
#ifndef GEODE_IS_ANDROID
    (void) generation;
    setFailure("Instant Replay ROOT solo está disponible en Android");
#else
    auto launchScript = fmt::format(
        "PIDFILE={}; ERRFILE={}; "
        "if [ -f \"$PIDFILE\" ]; then "
            "OLDPID=\"$(cat \"$PIDFILE\" 2>/dev/null)\"; "
            "case \"$OLDPID\" in ''|*[!0-9]*) ;; *) "
                "OLDNAME=\"$(cat /proc/$OLDPID/comm 2>/dev/null)\"; "
                "OLDARGS=\"$(tr '\\000' ' ' </proc/$OLDPID/cmdline 2>/dev/null)\"; "
                "case \"$OLDNAME:$OLDARGS\" in "
                    "screenrecord:*--output-format=h264*--size\\ 1280x720*--time-limit\\ 0*) "
                        "kill -2 \"$OLDPID\" 2>/dev/null; "
                    ";; "
                "esac; "
            ";; esac; "
        "fi; "
        ": >\"$ERRFILE\"; chmod 0644 \"$ERRFILE\"; "
        "echo $$ >\"$PIDFILE\"; chmod 0644 \"$PIDFILE\"; "
        "exec screenrecord --output-format=h264 --size {}x{} --bit-rate {} --time-limit 0 - 2>\"$ERRFILE\"",
        kPidFile,
        kErrorFile,
        kCaptureWidth,
        kCaptureHeight,
        kCaptureBitRate
    );
    auto command = "su -c " + shellQuote(launchScript);
    auto* pipe = ::popen(command.c_str(), "r");
    if (!pipe) {
        setFailure(fmt::format("popen screenrecord falló: {}", std::strerror(errno)));
        return;
    }

    auto fd = ::fileno(pipe);
    std::array<std::uint8_t, 64 * 1024> chunk{};
    std::vector<std::uint8_t> pending;
    pending.reserve(chunk.size() * 2);

    while (!m_stopRequested.load(std::memory_order_acquire)) {
        auto count = ::read(fd, chunk.data(), chunk.size());
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            setFailure(fmt::format("lectura H.264 falló: {}", std::strerror(errno)));
            break;
        }
        if (count == 0) {
            break;
        }
        pending.insert(pending.end(), chunk.begin(), chunk.begin() + count);

        auto first = findStartCode(pending, 0);
        if (first == std::string::npos) {
            if (pending.size() > 1024 * 1024) {
                setFailure("stream AVC sin códigos de inicio");
                break;
            }
            continue;
        }
        if (first > 0) {
            pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(first));
            first = 0;
        }

        for (;;) {
            auto next = findStartCode(pending, first + 3);
            if (next == std::string::npos) {
                if (first > 0) {
                    pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(first));
                }
                break;
            }
            auto nal = normalizeNal(pending, first, next);
            if (nal.size() > 5) {
                acceptNal(std::move(nal), generation);
            }
            first = next;
        }
    }

    if (!pending.empty()) {
        auto first = findStartCode(pending, 0);
        if (first != std::string::npos && first < pending.size()) {
            auto nal = normalizeNal(pending, first, pending.size());
            if (nal.size() > 5) {
                acceptNal(std::move(nal), generation);
            }
        }
    }
    finishAccessUnit(generation);
    requestRecorderStop();
    auto status = ::pclose(pipe);

    auto errorText = readSmallFile(kErrorFile);
    {
        std::lock_guard lock(m_mutex);
        if (generation == m_generation) {
            m_status.starting = false;
            m_status.buffering = false;
            if (!m_stopRequested.load(std::memory_order_acquire) && m_status.lastError.empty()) {
                auto exitCode = status != -1 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                m_status.lastError = errorText.empty()
                    ? fmt::format("screenrecord terminó inesperadamente (exit={})", exitCode)
                    : errorText;
                m_status.summary = "capturador detenido por error";
            } else if (m_status.lastError.empty()) {
                m_status.summary = fmt::format(
                    "búfer detenido; {:.1f} s conservados",
                    m_status.bufferedSeconds
                );
            }
        }
    }
    RootExecutor::get().post([] {
        RootExecutor::get().runRoot(
            "rm -f /data/local/tmp/zaid-ultra-replay-screenrecord.pid "
            "/data/local/tmp/zaid-ultra-replay-screenrecord.err"
        );
    });
#endif
}

void InstantReplay::acceptNal(std::vector<std::uint8_t> nal, std::uint64_t generation) {
    auto type = nalType(nal);
    std::lock_guard lock(m_mutex);
    if (generation != m_generation) {
        return;
    }

    if (type == 7) {
        m_sps = std::move(nal);
        return;
    }
    if (type == 8) {
        m_pps = std::move(nal);
        return;
    }

    bool vcl = type >= 1 && type <= 5;
    bool startsNew = type == 9 || (vcl && m_currentHasVcl && isFirstSlice(nal));
    // SEI generally prefixes the following picture. Close the previous access
    // unit before attaching it, matching Annex B access-unit ordering.
    if (type == 6 && m_currentHasVcl) {
        startsNew = true;
    }
    if (startsNew && !m_currentAccessUnit.empty()) {
        auto frame = EncodedFrame{
            .data = std::move(m_currentAccessUnit),
            .ptsUs = std::max(monotonicUs(), m_lastPtsUs + 1),
            .keyFrame = m_currentKeyFrame,
        };
        m_lastPtsUs = frame.ptsUs;
        if (m_firstPtsUs == 0) {
            m_firstPtsUs = frame.ptsUs;
        }
        m_ringBytes += frame.data.size();
        m_frames.emplace_back(std::move(frame));
        m_currentAccessUnit.clear();
        m_currentHasVcl = false;
        m_currentKeyFrame = false;
        pruneLocked();
    }

    m_currentAccessUnit.insert(m_currentAccessUnit.end(), nal.begin(), nal.end());
    if (vcl) {
        m_currentHasVcl = true;
        m_currentKeyFrame = m_currentKeyFrame || type == 5;
    }

    if (!m_frames.empty() && !m_sps.empty() && !m_pps.empty()) {
        m_status.starting = false;
        m_status.buffering = true;
        m_status.videoSupported = true;
        m_status.frameCount = m_frames.size();
        m_status.bufferedSeconds = std::max(
            0.0,
            static_cast<double>(m_frames.back().ptsUs - m_frames.front().ptsUs) / 1'000'000.0
        );
        m_status.bufferedMiB = static_cast<double>(m_ringBytes) / (1024.0 * 1024.0);
        if (!m_status.saving) {
            m_status.summary = fmt::format(
                "buffering {:.1f}/60 s | {:.1f} MiB | sin audio",
                std::min(60.0, m_status.bufferedSeconds),
                m_status.bufferedMiB
            );
        }
    }
}

void InstantReplay::finishAccessUnit(std::uint64_t generation) {
    std::lock_guard lock(m_mutex);
    if (generation != m_generation || !m_currentHasVcl || m_currentAccessUnit.empty()) {
        m_currentAccessUnit.clear();
        m_currentHasVcl = false;
        m_currentKeyFrame = false;
        return;
    }
    EncodedFrame frame;
    frame.data = std::move(m_currentAccessUnit);
    frame.ptsUs = std::max(monotonicUs(), m_lastPtsUs + 1);
    frame.keyFrame = m_currentKeyFrame;
    m_lastPtsUs = frame.ptsUs;
    if (m_firstPtsUs == 0) {
        m_firstPtsUs = frame.ptsUs;
    }
    m_ringBytes += frame.data.size();
    m_frames.emplace_back(std::move(frame));
    m_currentAccessUnit.clear();
    m_currentHasVcl = false;
    m_currentKeyFrame = false;
    pruneLocked();
}

void InstantReplay::pruneLocked() {
    if (m_frames.empty()) {
        return;
    }
    auto cutoff = m_frames.back().ptsUs - kRingRetentionUs;
    while (m_frames.size() > 1 &&
        (m_frames.front().ptsUs < cutoff || m_ringBytes > kMaximumRingBytes)) {
        m_ringBytes -= m_frames.front().data.size();
        m_frames.pop_front();
    }
    m_firstPtsUs = m_frames.front().ptsUs;
}

void InstantReplay::setFailure(std::string message, bool captureFailure) {
    if (message.empty()) {
        message = "error desconocido";
    }
    {
        std::lock_guard lock(m_mutex);
        if (captureFailure) {
            m_status.starting = false;
            m_status.buffering = false;
        }
        m_status.saving = false;
        m_status.lastError = message;
        m_status.summary = fmt::format("error: {}", message);
    }
    if (settings::diagnostics()) {
        log::error("Instant Replay: {}", message);
    }
}

void InstantReplay::requestRecorderStop() const {
#ifdef GEODE_IS_ANDROID
    auto script = fmt::format(
        "PID=\"$(cat {} 2>/dev/null)\"; "
        "case \"$PID\" in ''|*[!0-9]*) ;; *) "
            "NAME=\"$(cat /proc/$PID/comm 2>/dev/null)\"; "
            "ARGS=\"$(tr '\\000' ' ' </proc/$PID/cmdline 2>/dev/null)\"; "
            "case \"$NAME:$ARGS\" in "
                "screenrecord:*--output-format=h264*--size\\ 1280x720*--time-limit\\ 0*) "
                    "kill -2 \"$PID\" 2>/dev/null; "
                ";; "
            "esac; "
        ";; esac",
        kPidFile
    );
    auto command = "su -c " + shellQuote(script) + " >/dev/null 2>&1";
    (void) std::system(command.c_str());
#endif
}

bool InstantReplay::isFirstSlice(std::vector<std::uint8_t> const& nal) {
    auto offset = startCodeSize(nal);
    if (offset == 0 || offset + 1 >= nal.size()) {
        return true;
    }

    // first_mb_in_slice is the first unsigned Exp-Golomb value after the NAL
    // header. Remove emulation-prevention bytes before parsing it.
    std::vector<std::uint8_t> rbsp;
    rbsp.reserve(nal.size() - offset - 1);
    int zeroCount = 0;
    for (auto index = offset + 1; index < nal.size(); ++index) {
        auto byte = nal[index];
        if (zeroCount >= 2 && byte == 0x03) {
            zeroCount = 0;
            continue;
        }
        rbsp.push_back(byte);
        zeroCount = byte == 0 ? zeroCount + 1 : 0;
    }
    std::size_t leadingZeroBits = 0;
    std::size_t bit = 0;
    while (bit < rbsp.size() * 8) {
        auto value = (rbsp[bit / 8] >> (7 - (bit % 8))) & 1;
        ++bit;
        if (value) {
            break;
        }
        ++leadingZeroBits;
        if (leadingZeroBits > 31) {
            return true;
        }
    }
    std::uint64_t suffix = 0;
    for (std::size_t index = 0; index < leadingZeroBits && bit < rbsp.size() * 8; ++index, ++bit) {
        suffix = (suffix << 1) | ((rbsp[bit / 8] >> (7 - (bit % 8))) & 1);
    }
    auto firstMb = ((std::uint64_t{1} << leadingZeroBits) - 1) + suffix;
    return firstMb == 0;
}

std::string InstantReplay::shellQuote(std::string const& value) {
    std::string quoted{"'"};
    for (char character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted += character;
        }
    }
    quoted += '\'';
    return quoted;
}

} // namespace zaid::ultra
