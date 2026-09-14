#include "ReplayAudio.hpp"

#include "../core/RootExecutor.hpp"
#include "../core/Settings.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

#ifdef GEODE_IS_ANDROID
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace geode::prelude;

namespace zaid::ultra {
namespace {

constexpr std::int64_t kRingRetentionUs = 72'000'000;
constexpr std::size_t kMaximumRingBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaximumPacketBytes = 1024 * 1024;
constexpr std::uint32_t kPacketMagic = 0x5a554131; // ZUA1
constexpr std::uint32_t kCodecConfigFlag = 2;
constexpr std::uint32_t kTelemetryFlag = 0x40000000;
constexpr char kHelperClass[] = "com.zaid.ultra.replay.ReplayAudioCapture";
constexpr char kHelperJar[] = "/data/local/tmp/zaid-ultra-replay-audio.jar";
constexpr char kPidFile[] = "/data/local/tmp/zaid-ultra-replay-audio.pid";
constexpr char kErrorFile[] = "/data/local/tmp/zaid-ultra-replay-audio.err";

std::string readSmallFile(std::filesystem::path const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    std::string value((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (value.size() > 8192) {
        value.resize(8192);
    }
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

std::string firstDiagnosticLine(std::string value) {
    auto lineEnd = value.find_first_of("\r\n");
    if (lineEnd != std::string::npos) {
        value.resize(lineEnd);
    }
    if (value.size() > 320) {
        value.resize(317);
        value += "...";
    }
    return value;
}

#ifdef GEODE_IS_ANDROID
bool readExact(int fd, std::uint8_t* destination, std::size_t size, std::string& error) {
    std::size_t offset = 0;
    while (offset < size) {
        auto count = ::read(fd, destination + offset, size - offset);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = std::strerror(errno);
            return false;
        }
        if (count == 0) {
            error = offset == 0 ? "EOF" : "paquete AAC truncado";
            return false;
        }
        offset += static_cast<std::size_t>(count);
    }
    return true;
}
#endif

std::uint32_t readU32Be(std::uint8_t const* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
        (static_cast<std::uint32_t>(bytes[1]) << 16) |
        (static_cast<std::uint32_t>(bytes[2]) << 8) |
        static_cast<std::uint32_t>(bytes[3]);
}

std::uint64_t readU64Be(std::uint8_t const* bytes) {
    std::uint64_t value = 0;
    for (int index = 0; index < 8; ++index) {
        value = (value << 8) | bytes[index];
    }
    return value;
}

} // namespace

ReplayAudio& ReplayAudio::get() {
    static ReplayAudio instance;
    return instance;
}

ReplayAudio::~ReplayAudio() {
    m_stopRequested.store(true, std::memory_order_release);
    if (m_captureThread.joinable()) {
        requestHelperStop();
        m_captureThread.join();
    }
}

void ReplayAudio::beginGameplay(bool preserveExisting, std::int64_t ptsOffsetUs) {
    auto enabled = settings::enabled("instant-replay") &&
        settings::enabled("instant-replay-audio");
    {
        std::lock_guard lock(m_mutex);
        m_status.enabled = enabled;
        if (!enabled) {
            if (!preserveExisting) {
                m_frames.clear();
                m_codecSpecificData.clear();
                m_ringBytes = 0;
                m_status.available = false;
                m_status.bufferedSeconds = 0.0;
                m_status.bufferedMiB = 0.0;
                m_status.packetCount = 0;
            }
            m_status.starting = false;
            m_status.buffering = false;
            m_status.summary = "audio del replay desactivado";
            return;
        }
        if (m_status.starting || m_status.buffering) {
            return;
        }
    }

    if (m_captureThread.joinable()) {
        m_captureThread.join();
    }

    auto helper = Mod::get()->getResourcesDir() / "replay-audio.jar";
    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(helper, filesystemError)) {
        setFailure("replay-audio.jar no está disponible");
        return;
    }

    std::uint64_t generation = 0;
    {
        std::lock_guard lock(m_mutex);
        generation = ++m_generation;
        if (!preserveExisting) {
            m_frames.clear();
            m_codecSpecificData.clear();
            m_ringBytes = 0;
        }
        m_ptsOffsetUs = std::max<std::int64_t>(0, ptsOffsetUs);
        m_status.starting = true;
        m_status.buffering = false;
        m_status.available = !m_codecSpecificData.empty() && !m_frames.empty();
        if (!preserveExisting) {
            m_status.bufferedSeconds = 0.0;
            m_status.bufferedMiB = 0.0;
            m_status.packetCount = 0;
            m_status.signalMeasured = false;
            m_status.signalPresent = false;
            m_status.signalDbfs = -120.0;
            m_status.signalPeak = 0.0;
        }
#ifdef GEODE_IS_ANDROID
        m_status.targetUid = static_cast<int>(::getuid());
#else
        m_status.targetUid = -1;
#endif
        m_status.summary = preserveExisting
            ? "reanudando captura GAME/MEDIA por UID..."
            : "iniciando captura GAME/MEDIA por UID...";
        m_status.lastError.clear();
    }
    m_stopRequested.store(false, std::memory_order_release);
    m_captureThread = std::thread([
        this,
        generation,
        helperSource = helper.string()
    ] { captureLoop(generation, helperSource); });
}

void ReplayAudio::endGameplay() {
    m_stopRequested.store(true, std::memory_order_release);
    {
        std::lock_guard lock(m_mutex);
        if (m_status.starting || m_status.buffering) {
            m_status.summary = "deteniendo audio del replay...";
        }
    }
#ifdef GEODE_IS_ANDROID
    requestHelperStop();
#endif
}

ReplayAudioSnapshot ReplayAudio::snapshot(
    std::int64_t fromPtsUs,
    std::int64_t toPtsUs
) const {
    ReplayAudioSnapshot result;
    std::lock_guard lock(m_mutex);
    if (m_codecSpecificData.empty() || m_frames.empty()) {
        return result;
    }
    result.codecSpecificData = m_codecSpecificData;
    for (auto const& frame : m_frames) {
        if (frame.ptsUs >= fromPtsUs && frame.ptsUs <= toPtsUs) {
            result.frames.push_back(frame);
        }
    }
    return result;
}

ReplayAudioStatus ReplayAudio::status() const {
    std::lock_guard lock(m_mutex);
    return m_status;
}

void ReplayAudio::captureLoop(std::uint64_t generation, std::string helperSource) {
#ifndef GEODE_IS_ANDROID
    (void) generation;
    (void) helperSource;
    setFailure("audio ROOT solo está disponible en Android");
#else
    auto launchScript = fmt::format(
        "PIDFILE={}; ERRFILE={}; HELPER={}; SOURCE={}; "
        "if [ -f \"$PIDFILE\" ]; then "
            "OLDPID=\"$(cat \"$PIDFILE\" 2>/dev/null)\"; "
            "case \"$OLDPID\" in ''|*[!0-9]*) ;; *) "
                "OLDARGS=\"$(tr '\\000' ' ' </proc/$OLDPID/cmdline 2>/dev/null)\"; "
                "case \"$OLDARGS\" in "
                    "*zaid-ultra-replay-audio.jar*com.zaid.ultra.replay.ReplayAudioCapture*) "
                        "kill -2 \"$OLDPID\" 2>/dev/null; "
                    ";; "
                "esac; "
            ";; esac; "
        "fi; "
        "rm -f \"$HELPER\"; cp \"$SOURCE\" \"$HELPER\" || exit 21; "
        "chown 0:0 \"$HELPER\"; chmod 0444 \"$HELPER\"; "
        ": >\"$ERRFILE\"; chmod 0644 \"$ERRFILE\"; "
        "echo $$ >\"$PIDFILE\"; chmod 0644 \"$PIDFILE\"; "
        "exec env CLASSPATH=\"$HELPER\" app_process /system/bin {} --target-uid {} 2>\"$ERRFILE\"",
        kPidFile,
        kErrorFile,
        kHelperJar,
        shellQuote(helperSource),
        kHelperClass,
        static_cast<int>(::getuid())
    );
    auto command = "su -c " + shellQuote(launchScript);
    auto* pipe = ::popen(command.c_str(), "r");
    if (!pipe) {
        setFailure(fmt::format("popen audio ROOT falló: {}", std::strerror(errno)));
        return;
    }

    auto fd = ::fileno(pipe);
    std::array<std::uint8_t, 20> header{};
    std::string readError;
    while (!m_stopRequested.load(std::memory_order_acquire)) {
        if (!readExact(fd, header.data(), header.size(), readError)) {
            break;
        }
        auto magic = readU32Be(header.data());
        auto payloadSize = readU32Be(header.data() + 4);
        auto pts = readU64Be(header.data() + 8);
        auto flags = readU32Be(header.data() + 16);
        if (magic != kPacketMagic || payloadSize == 0 || payloadSize > kMaximumPacketBytes ||
            pts > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            readError = "cabecera AAC inválida";
            break;
        }

        std::vector<std::uint8_t> payload(payloadSize);
        if (!readExact(fd, payload.data(), payload.size(), readError)) {
            break;
        }
        if ((flags & kTelemetryFlag) != 0) {
            acceptTelemetry(payload, generation);
        } else {
            acceptPacket(
                std::move(payload),
                static_cast<std::int64_t>(pts),
                flags,
                generation
            );
        }
    }

    requestHelperStop();
    auto processStatus = ::pclose(pipe);
    auto errorText = readSmallFile(kErrorFile);
    if (!errorText.empty() && settings::diagnostics()) {
        log::warn("Replay audio helper output: {}", errorText);
    }
    {
        std::lock_guard lock(m_mutex);
        if (generation == m_generation) {
            m_status.starting = false;
            m_status.buffering = false;
            if (!m_stopRequested.load(std::memory_order_acquire)) {
                auto exitCode = processStatus != -1 && WIFEXITED(processStatus)
                    ? WEXITSTATUS(processStatus)
                    : -1;
                m_status.lastError = !errorText.empty()
                    ? firstDiagnosticLine(errorText)
                    : fmt::format("helper de audio terminó (exit={}, {})", exitCode, readError);
                m_status.summary = "audio no disponible; replay continúa con vídeo";
            } else if (m_status.available) {
                auto signal = m_status.signalMeasured
                    ? (m_status.signalPresent
                        ? fmt::format("señal máx {:.1f} dBFS", m_status.signalDbfs)
                        : std::string("SILENCIO PCM"))
                    : std::string("señal no medida");
                m_status.summary = fmt::format(
                    "audio listo; {:.1f} s AAC | {}",
                    m_status.bufferedSeconds,
                    signal
                );
            } else if (m_status.lastError.empty()) {
                m_status.summary = "audio detenido sin paquetes AAC";
            }
        }
    }
    RootExecutor::get().post([] {
        RootExecutor::get().runRoot(
            "rm -f /data/local/tmp/zaid-ultra-replay-audio.pid "
            "/data/local/tmp/zaid-ultra-replay-audio.err "
            "/data/local/tmp/zaid-ultra-replay-audio.jar"
        );
    });
#endif
}

void ReplayAudio::acceptPacket(
    std::vector<std::uint8_t> payload,
    std::int64_t ptsUs,
    std::uint32_t flags,
    std::uint64_t generation
) {
    std::lock_guard lock(m_mutex);
    if (generation != m_generation) {
        return;
    }
    if ((flags & kCodecConfigFlag) != 0) {
        m_codecSpecificData = std::move(payload);
    } else {
        if (ptsUs <= 0) {
            return;
        }
        ptsUs = std::max<std::int64_t>(1, ptsUs - m_ptsOffsetUs);
        if (!m_frames.empty()) {
            ptsUs = std::max(ptsUs, m_frames.back().ptsUs + 1);
        }
        m_ringBytes += payload.size();
        m_frames.push_back({
            .data = std::move(payload),
            .ptsUs = ptsUs,
            .flags = flags,
        });
        pruneLocked();
    }

    if (!m_codecSpecificData.empty() && !m_frames.empty()) {
        m_status.starting = false;
        m_status.buffering = true;
        m_status.available = true;
        m_status.packetCount = m_frames.size();
        m_status.bufferedSeconds = std::max(
            0.0,
            static_cast<double>(m_frames.back().ptsUs - m_frames.front().ptsUs) / 1'000'000.0
        );
        m_status.bufferedMiB = static_cast<double>(m_ringBytes) / (1024.0 * 1024.0);
        auto signal = m_status.signalMeasured
            ? (m_status.signalPresent
                ? fmt::format("señal máx {:.1f} dBFS", m_status.signalDbfs)
                : std::string("SILENCIO PCM"))
            : std::string("midiendo señal...");
        m_status.summary = fmt::format(
            "AAC GAME/MEDIA UID {} | {} | {:.1f} s | {:.1f} MiB",
            m_status.targetUid,
            signal,
            m_status.bufferedSeconds,
            m_status.bufferedMiB
        );
        m_status.lastError.clear();
    }
}

void ReplayAudio::acceptTelemetry(
    std::vector<std::uint8_t> const& payload,
    std::uint64_t generation
) {
    if (payload.size() != 16) {
        return;
    }
    auto peak = readU32Be(payload.data());
    auto rmsMilli = readU32Be(payload.data() + 4);
    auto nonZero = readU32Be(payload.data() + 8);
    auto targetUid = static_cast<std::int32_t>(readU32Be(payload.data() + 12));
    auto normalizedPeak = std::min(1.0, static_cast<double>(peak) / 32768.0);
    auto normalizedRms = std::min(1.0, static_cast<double>(rmsMilli) / 32'768'000.0);
    auto dbfs = normalizedRms > 0.0 ? 20.0 * std::log10(normalizedRms) : -120.0;

    std::lock_guard lock(m_mutex);
    if (generation != m_generation) {
        return;
    }
    auto signalNow = normalizedPeak >= 0.001 || nonZero >= 128;
    m_status.signalMeasured = true;
    // Ignore sub-LSB noise floors. A real game mix comfortably exceeds both
    // this peak threshold and the non-zero sample count over a one-second window.
    m_status.signalPresent = m_status.signalPresent || signalNow;
    m_status.signalDbfs = std::max(m_status.signalDbfs, std::max(-120.0, dbfs));
    m_status.signalPeak = std::max(m_status.signalPeak, normalizedPeak);
    m_status.targetUid = targetUid;
    if (m_status.available) {
        auto signal = m_status.signalPresent
            ? fmt::format("señal máx {:.1f} dBFS", m_status.signalDbfs)
            : std::string("SILENCIO PCM");
        m_status.summary = fmt::format(
            "AAC GAME/MEDIA UID {} | {} | {:.1f} s | {:.1f} MiB",
            m_status.targetUid,
            signal,
            m_status.bufferedSeconds,
            m_status.bufferedMiB
        );
    }
}

void ReplayAudio::pruneLocked() {
    if (m_frames.empty()) {
        return;
    }
    auto cutoff = m_frames.back().ptsUs - kRingRetentionUs;
    while (m_frames.size() > 1 &&
        (m_frames.front().ptsUs < cutoff || m_ringBytes > kMaximumRingBytes)) {
        m_ringBytes -= m_frames.front().data.size();
        m_frames.pop_front();
    }
}

void ReplayAudio::setFailure(std::string message) {
    if (message.empty()) {
        message = "error de audio desconocido";
    }
    {
        std::lock_guard lock(m_mutex);
        m_status.starting = false;
        m_status.buffering = false;
        // A failed resume does not invalidate AAC packets already retained
        // from the preceding segment.
        m_status.available = !m_codecSpecificData.empty() && !m_frames.empty();
        m_status.lastError = message;
        m_status.summary = "audio no disponible; replay continúa con vídeo";
    }
    if (settings::diagnostics()) {
        log::warn("Replay audio: {}", message);
    }
}

void ReplayAudio::requestHelperStop() const {
#ifdef GEODE_IS_ANDROID
    auto script = fmt::format(
        "PID=\"$(cat {} 2>/dev/null)\"; "
        "case \"$PID\" in ''|*[!0-9]*) ;; *) "
            "ARGS=\"$(tr '\\000' ' ' </proc/$PID/cmdline 2>/dev/null)\"; "
            "case \"$ARGS\" in "
                "*zaid-ultra-replay-audio.jar*com.zaid.ultra.replay.ReplayAudioCapture*) "
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

std::string ReplayAudio::shellQuote(std::string const& value) {
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
