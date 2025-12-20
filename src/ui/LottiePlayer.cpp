#include "LottiePlayer.h"
#include <wx/wfstream.h>
#include <wx/zstream.h>
#include <wx/mstream.h>
#include <fstream>
#include <iostream>
#include <zlib.h>

// Remove static event table - use Bind instead for proper timer handling
wxBEGIN_EVENT_TABLE(LottiePlayer, wxEvtHandler)
wxEND_EVENT_TABLE()

static const int LOTTIE_TIMER_ID = 19999;

LottiePlayer::LottiePlayer()
    : m_timer(this, LOTTIE_TIMER_ID),
      m_isLoaded(false),
      m_isPlaying(false),
      m_loop(true),
      m_totalFrames(0),
      m_frameRate(60.0),
      m_currentFrame(0),
      m_size(512, 512),
      m_renderSize(200, 200)
{
    // Bind timer event using Bind for proper handling
    Bind(wxEVT_TIMER, &LottiePlayer::OnTimer, this, LOTTIE_TIMER_ID);
    std::cerr << "[LottiePlayer] Created, timer bound" << std::endl;
}

LottiePlayer::~LottiePlayer()
{
    Unbind(wxEVT_TIMER, &LottiePlayer::OnTimer, this, LOTTIE_TIMER_ID);
    Stop();
    std::cerr << "[LottiePlayer] Destroyed" << std::endl;
}

bool LottiePlayer::DecompressTgs(const wxString& path, std::string& jsonOut)
{
    // .tgs files are gzip-compressed Lottie JSON
    std::ifstream file(path.ToStdString(), std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[LottiePlayer] Failed to open file: " << path.ToStdString() << std::endl;
        return false;
    }
    
    // Read compressed data
    std::vector<char> compressed((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
    file.close();
    
    if (compressed.empty()) {
        std::cerr << "[LottiePlayer] Empty file: " << path.ToStdString() << std::endl;
        return false;
    }
    
    // Decompress using zlib
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    
    // Use inflateInit2 with 16+MAX_WBITS to handle gzip format
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) {
        std::cerr << "[LottiePlayer] Failed to initialize zlib" << std::endl;
        return false;
    }
    
    zs.next_in = reinterpret_cast<Bytef*>(compressed.data());
    zs.avail_in = compressed.size();
    
    std::string decompressed;
    char outBuffer[32768];
    int ret;
    
    do {
        zs.next_out = reinterpret_cast<Bytef*>(outBuffer);
        zs.avail_out = sizeof(outBuffer);
        
        ret = inflate(&zs, Z_NO_FLUSH);
        
        if (ret == Z_STREAM_ERROR || ret == Z_NEED_DICT || 
            ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&zs);
            std::cerr << "[LottiePlayer] Decompression error: " << ret << std::endl;
            return false;
        }
        
        decompressed.append(outBuffer, sizeof(outBuffer) - zs.avail_out);
    } while (ret != Z_STREAM_END);
    
    inflateEnd(&zs);
    
    jsonOut = std::move(decompressed);
    std::cerr << "[LottiePlayer] Decompressed " << compressed.size() 
              << " bytes to " << jsonOut.size() << " bytes" << std::endl;
    
    return true;
}

bool LottiePlayer::LoadTgsFile(const wxString& path)
{
#ifdef HAVE_RLOTTIE
    std::string json;
    if (!DecompressTgs(path, json)) {
        return false;
    }
    
    return LoadJson(json);
#else
    std::cerr << "[LottiePlayer] rlottie not available - cannot load .tgs files" << std::endl;
    return false;
#endif
}

bool LottiePlayer::LoadJson(const std::string& json)
{
#ifdef HAVE_RLOTTIE
    Stop();
    m_isLoaded = false;
    
    m_animation = rlottie::Animation::loadFromData(json, "", "", false);
    
    if (!m_animation) {
        std::cerr << "[LottiePlayer] Failed to parse Lottie JSON" << std::endl;
        return false;
    }
    
    // Get animation properties
    m_totalFrames = m_animation->totalFrame();
    m_frameRate = m_animation->frameRate();
    
    size_t width, height;
    m_animation->size(width, height);
    m_size = wxSize(static_cast<int>(width), static_cast<int>(height));
    
    // Use native size if render size not set
    if (m_renderSize.GetWidth() <= 0 || m_renderSize.GetHeight() <= 0) {
        m_renderSize = m_size;
    }
    
    // Allocate frame buffer
    m_frameBuffer.resize(m_renderSize.GetWidth() * m_renderSize.GetHeight());
    
    m_currentFrame = 0;
    m_isLoaded = true;
    
    std::cerr << "[LottiePlayer] Loaded animation: " << m_totalFrames << " frames, "
              << m_frameRate << " fps, " << m_size.GetWidth() << "x" << m_size.GetHeight() << std::endl;
    
    return true;
#else
    std::cerr << "[LottiePlayer] rlottie not available" << std::endl;
    return false;
#endif
}

double LottiePlayer::GetDuration() const
{
    if (m_frameRate > 0 && m_totalFrames > 0) {
        return static_cast<double>(m_totalFrames) / m_frameRate;
    }
    return 0.0;
}

void LottiePlayer::SetRenderSize(int width, int height)
{
    if (width > 0 && height > 0) {
        m_renderSize = wxSize(width, height);
        if (m_isLoaded) {
            m_frameBuffer.resize(width * height);
        }
    }
}

int LottiePlayer::GetTimerIntervalMs() const
{
    if (m_frameRate <= 0) return 33;  // Default ~30fps
    int intervalMs = static_cast<int>(1000.0 / m_frameRate);
    if (intervalMs < 16) intervalMs = 16;  // Cap at ~60fps
    return intervalMs;
}

bool LottiePlayer::AdvanceFrame()
{
    if (!m_isLoaded || !m_isPlaying) {
        return false;
    }
    
    m_currentFrame++;
    
    if (m_currentFrame >= m_totalFrames) {
        if (m_loop) {
            m_currentFrame = 0;
        } else {
            m_isPlaying = false;
            return false;
        }
    }
    
    RenderCurrentFrame();
    return true;
}

void LottiePlayer::Play()
{
    std::cerr << "[LottiePlayer] Play() called, isLoaded=" << m_isLoaded << " isPlaying=" << m_isPlaying << std::endl;
    
    if (!m_isLoaded) {
        std::cerr << "[LottiePlayer] Cannot play - not loaded" << std::endl;
        return;
    }
    
    if (m_isPlaying) {
        std::cerr << "[LottiePlayer] Already playing" << std::endl;
        return;
    }
    
    m_isPlaying = true;
    m_currentFrame = 0;
    
    std::cerr << "[LottiePlayer] Play state set, frame rate=" << m_frameRate << " fps" << std::endl;
    
    // Render first frame immediately
    RenderCurrentFrame();
}

void LottiePlayer::Stop()
{
    m_timer.Stop();
    m_isPlaying = false;
    m_currentFrame = 0;
}

void LottiePlayer::Pause()
{
    m_timer.Stop();
    m_isPlaying = false;
}

void LottiePlayer::OnTimer(wxTimerEvent& event)
{
    if (!m_isLoaded || !m_isPlaying) {
        return;
    }
    
    m_currentFrame++;
    
    if (m_currentFrame >= m_totalFrames) {
        if (m_loop) {
            m_currentFrame = 0;
        } else {
            Stop();
            return;
        }
    }
    
    RenderCurrentFrame();
}

void LottiePlayer::RenderCurrentFrame()
{
    if (!m_isLoaded) {
        std::cerr << "[LottiePlayer] RenderCurrentFrame: not loaded" << std::endl;
        return;
    }
    
    wxBitmap frame = RenderFrame(m_currentFrame);
    
    if (frame.IsOk()) {
        if (m_frameCallback) {
            m_frameCallback(frame);
        } else {
            std::cerr << "[LottiePlayer] RenderCurrentFrame: no callback set" << std::endl;
        }
    } else {
        std::cerr << "[LottiePlayer] RenderCurrentFrame: frame not ok" << std::endl;
    }
}

wxBitmap LottiePlayer::RenderFrame(size_t frameNum)
{
    return RenderFrame(frameNum, m_renderSize.GetWidth(), m_renderSize.GetHeight());
}

wxBitmap LottiePlayer::RenderFrame(size_t frameNum, int width, int height)
{
#ifdef HAVE_RLOTTIE
    if (!m_isLoaded || !m_animation) {
        return wxNullBitmap;
    }
    
    // Validate dimensions to prevent pixman errors
    if (width <= 0 || height <= 0) {
        std::cerr << "[LottiePlayer] Invalid render dimensions: " << width << "x" << height << std::endl;
        return wxNullBitmap;
    }
    
    if (frameNum >= m_totalFrames) {
        frameNum = m_totalFrames - 1;
    }
    
    // Ensure buffer is the right size
    size_t bufferSize = width * height;
    if (m_frameBuffer.size() != bufferSize) {
        m_frameBuffer.resize(bufferSize);
    }
    
    // Render frame to ARGB buffer
    rlottie::Surface surface(m_frameBuffer.data(), width, height, width * sizeof(uint32_t));
    m_animation->renderSync(frameNum, surface);
    
    // Convert ARGB to wxBitmap with alpha
    wxImage image(width, height);
    if (!image.IsOk()) {
        std::cerr << "[LottiePlayer] Failed to create wxImage " << width << "x" << height << std::endl;
        return wxNullBitmap;
    }
    image.InitAlpha();
    
    unsigned char* rgbData = image.GetData();
    unsigned char* alphaData = image.GetAlpha();
    
    for (size_t i = 0; i < bufferSize; i++) {
        uint32_t pixel = m_frameBuffer[i];
        
        // rlottie uses ARGB format (premultiplied alpha)
        uint8_t a = (pixel >> 24) & 0xFF;
        uint8_t r = (pixel >> 16) & 0xFF;
        uint8_t g = (pixel >> 8) & 0xFF;
        uint8_t b = pixel & 0xFF;
        
        // Unpremultiply alpha
        if (a > 0 && a < 255) {
            r = static_cast<uint8_t>(std::min(255, (r * 255) / a));
            g = static_cast<uint8_t>(std::min(255, (g * 255) / a));
            b = static_cast<uint8_t>(std::min(255, (b * 255) / a));
        }
        
        rgbData[i * 3] = r;
        rgbData[i * 3 + 1] = g;
        rgbData[i * 3 + 2] = b;
        alphaData[i] = a;
    }
    
    return wxBitmap(image);
#else
    return wxNullBitmap;
#endif
}