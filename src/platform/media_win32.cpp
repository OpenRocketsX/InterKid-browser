#ifdef _WIN32

#include "platform/media_player.h"

#include <dshow.h>
#include <objbase.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace {

constexpr double kReferenceTimePerSecond = 10000000.0;

bool DecodePercentEscapes(const std::string& input, std::string& output) {
    const auto hexValue = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }

        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }

        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }

        return -1;
    };

    output.clear();
    output.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '%') {
            output += input[i];
            continue;
        }

        if (i + 2 >= input.size()) {
            return false;
        }

        const int high = hexValue(input[i + 1]);
        const int low = hexValue(input[i + 2]);

        if (high < 0 || low < 0) {
            return false;
        }

        output += static_cast<char>((high << 4) | low);
        i += 2;
    }

    return true;
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    const int length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);

    if (length <= 0) {
        return {};
    }

    std::wstring output(static_cast<size_t>(length), L'\0');

    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            output.data(),
            length) <= 0) {
        return {};
    }

    return output;
}

std::wstring MediaToWide(const std::string& url) {
    std::string value = url;

    if (value.rfind("file://", 0) == 0) {
        value = value.substr(7);

        // file:///C:/media/video.mp4 becomes C:/media/video.mp4.
        if (value.size() >= 3 &&
            value[0] == '/' &&
            value[2] == ':') {
            value.erase(0, 1);
        }

        std::string decoded;

        if (!DecodePercentEscapes(value, decoded)) {
            return {};
        }

        std::replace(decoded.begin(), decoded.end(), '/', '\\');
        value = std::move(decoded);
    }

    return Utf8ToWide(value);
}

// This is intentionally a template so that the helper does not need to name
// PlatformMediaPlayer::Impl, which is a private nested type.
template <typename ImplType>
void ReleaseGraph(ImplType& impl) {
    if (impl.control) {
        impl.control->Stop();
    }

    if (impl.video) {
        impl.video->put_Visible(OAFALSE);
        impl.video->put_Owner(0);
        impl.video->Release();
        impl.video = nullptr;
    }

    if (impl.audio) {
        impl.audio->Release();
        impl.audio = nullptr;
    }

    if (impl.seeking) {
        impl.seeking->Release();
        impl.seeking = nullptr;
    }

    if (impl.control) {
        impl.control->Release();
        impl.control = nullptr;
    }

    if (impl.graph) {
        impl.graph->Release();
        impl.graph = nullptr;
    }
}

} // namespace

struct PlatformMediaPlayer::Impl {
    IGraphBuilder* graph = nullptr;
    IMediaControl* control = nullptr;
    IVideoWindow* video = nullptr;
    IMediaSeeking* seeking = nullptr;
    IBasicAudio* audio = nullptr;
    bool comInitialized = false;
};

PlatformMediaPlayer::PlatformMediaPlayer()
    : m_impl(new Impl) {
    const HRESULT result =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // CoInitializeEx must only be balanced with CoUninitialize when it
    // succeeded. RPC_E_CHANGED_MODE means this thread already uses another
    // COM apartment model and must not be uninitialized by this object.
    m_impl->comInitialized = SUCCEEDED(result);
}

PlatformMediaPlayer::~PlatformMediaPlayer() {
    ReleaseGraph(*m_impl);

    if (m_impl->comInitialized) {
        CoUninitialize();
    }

    delete m_impl;
}

bool PlatformMediaPlayer::Load(PlatformMediaOwner owner,
                               const std::string& url,
                               bool hasVideo,
                               bool autoplay) {
    ReleaseGraph(*m_impl);

    if (url.empty()) {
        return false;
    }

    const std::wstring wideUrl = MediaToWide(url);

    if (wideUrl.empty()) {
        return false;
    }

    if (FAILED(CoCreateInstance(
            CLSID_FilterGraph,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&m_impl->graph)))) {
        ReleaseGraph(*m_impl);
        return false;
    }

    // Avoid IID_PPV_ARGS here because MinGW's DirectShow headers can
    // produce unresolved __mingw_uuidof symbols for these interfaces.
    if (FAILED(m_impl->graph->QueryInterface(
            IID_IMediaControl,
            reinterpret_cast<void**>(&m_impl->control)))) {
        ReleaseGraph(*m_impl);
        return false;
    }

    if (FAILED(m_impl->graph->RenderFile(
            wideUrl.c_str(),
            nullptr))) {
        ReleaseGraph(*m_impl);
        return false;
    }

    // Optional interfaces. Failure to acquire these does not prevent
    // the media graph itself from loading.
    m_impl->graph->QueryInterface(
        IID_IMediaSeeking,
        reinterpret_cast<void**>(&m_impl->seeking));

    m_impl->graph->QueryInterface(
        IID_IBasicAudio,
        reinterpret_cast<void**>(&m_impl->audio));

    m_url = url;
    m_hasVideo = hasVideo;

    if (hasVideo &&
        SUCCEEDED(m_impl->graph->QueryInterface(
            IID_IVideoWindow,
            reinterpret_cast<void**>(&m_impl->video)))) {

        // MinGW's DirectShow headers define OAHWND as an integer handle
        // type, while PlatformMediaOwner is an HWND pointer type.
        m_impl->video->put_Owner(
            reinterpret_cast<OAHWND>(owner));

        m_impl->video->put_WindowStyle(
            WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);

        m_impl->video->put_Visible(OATRUE);
    }

    SetVolume(m_volume);
    SetMuted(m_muted);

    if (autoplay) {
        Play();
    }

    return true;
}

void PlatformMediaPlayer::SetRect(
    float x,
    float y,
    float width,
    float height) {

    if (!m_impl->video ||
        !std::isfinite(x) ||
        !std::isfinite(y) ||
        !std::isfinite(width) ||
        !std::isfinite(height)) {
        return;
    }

    m_impl->video->SetWindowPosition(
        static_cast<long>(x),
        static_cast<long>(y),
        static_cast<long>(std::max(1.0f, width)),
        static_cast<long>(std::max(1.0f, height)));
}

void PlatformMediaPlayer::Play() {
    if (m_impl->control) {
        m_impl->control->Run();
    }
}

void PlatformMediaPlayer::Pause() {
    if (m_impl->control) {
        m_impl->control->Pause();
    }
}

void PlatformMediaPlayer::Stop() {
    if (m_impl->control) {
        m_impl->control->Stop();
    }
}

void PlatformMediaPlayer::SetCurrentTime(double seconds) {
    if (!m_impl->seeking ||
        !std::isfinite(seconds)) {
        return;
    }

    // IMediaSeeking::SetPositions takes a non-const LONGLONG* under
    // the MinGW DirectShow headers.
    LONGLONG position = static_cast<LONGLONG>(
        std::max(0.0, seconds) * kReferenceTimePerSecond);

    m_impl->seeking->SetPositions(
        &position,
        AM_SEEKING_AbsolutePositioning,
        nullptr,
        AM_SEEKING_NoPositioning);
}

double PlatformMediaPlayer::CurrentTime() const {
    if (!m_impl->seeking) {
        return 0.0;
    }

    LONGLONG position = 0;

    return SUCCEEDED(
               m_impl->seeking->GetCurrentPosition(&position))
               ? static_cast<double>(position) /
                     kReferenceTimePerSecond
               : 0.0;
}

double PlatformMediaPlayer::Duration() const {
    if (!m_impl->seeking) {
        return 0.0;
    }

    LONGLONG duration = 0;

    return SUCCEEDED(
               m_impl->seeking->GetDuration(&duration))
               ? static_cast<double>(duration) /
                     kReferenceTimePerSecond
               : 0.0;
}

void PlatformMediaPlayer::SetVolume(double volume) {
    if (!std::isfinite(volume)) {
        volume = 1.0;
    }

    m_volume = std::clamp(volume, 0.0, 1.0);

    if (!m_impl->audio || m_muted) {
        return;
    }

    const long decibels =
        m_volume <= 0.0
            ? -10000L
            : static_cast<long>(
                  2000.0 * std::log10(m_volume));

    m_impl->audio->put_Volume(
        std::clamp(decibels, -10000L, 0L));
}

double PlatformMediaPlayer::Volume() const {
    return m_volume;
}

void PlatformMediaPlayer::SetMuted(bool muted) {
    m_muted = muted;

    if (!m_impl->audio) {
        return;
    }

    if (m_muted) {
        m_impl->audio->put_Volume(-10000);
    } else {
        SetVolume(m_volume);
    }
}

bool PlatformMediaPlayer::Muted() const {
    return m_muted;
}

bool PlatformMediaPlayer::Paused() const {
    if (!m_impl->control) {
        return true;
    }

    OAFilterState state = State_Stopped;

    return FAILED(
               m_impl->control->GetState(0, &state)) ||
           state != State_Running;
}

#endif
