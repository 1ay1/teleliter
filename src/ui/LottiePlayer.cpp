#include "LottiePlayer.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <zlib.h>

#define LOTTIELOG(msg) std::cerr << "[LottiePlayer] " << msg << std::endl

LottiePlayer::LottiePlayer()
    : m_width(0), m_height(0), m_renderWidth(0), m_renderHeight(0),
      m_totalFrames(0), m_currentFrame(0), m_frameRate(60.0), m_duration(0.0),
      m_isLoaded(false), m_isPlaying(false), m_loop(true) {
}

LottiePlayer::~LottiePlayer() {
    Stop();
}

bool LottiePlayer::DecompressTGS(const wxString& path, std::string& jsonOut) {
    // TGS files are gzip-compressed JSON
    gzFile file = gzopen(path.ToStdString().c_str(), "rb");
    if (!file) {
        LOTTIELOG("Failed to open TGS file: " << path.ToStdString());
        return false;
    }

    std::ostringstream oss;
    char buffer[8192];
    int bytesRead;

    while ((bytesRead = gzread(file, buffer, sizeof(buffer))) > 0) {
        oss.write(buffer, bytesRead);
    }

    int err;
    const char* errMsg = gzerror(file, &err);
    gzclose(file);

    if (err != Z_OK && err != Z_STREAM_END) {
        LOTTIELOG("Error decompressing TGS: " << (errMsg ? errMsg : "unknown error"));
        return false;
    }

    jsonOut = oss.str();
    
    if (jsonOut.empty()) {
        LOTTIELOG("Decompressed TGS is empty");
        return false;
    }

    return true;
}

bool LottiePlayer::LoadFile(const wxString& path) {
#ifdef HAVE_RLOTTIE
    // Check file extension
    wxString ext = path.AfterLast('.').Lower();
    
    std::string jsonData;
    
    if (ext == "tgs") {
        // Decompress TGS to JSON
        if (!DecompressTGS(path, jsonData)) {
            return false;
        }
    } else if (ext == "json") {
        // Load JSON directly
        std::ifstream file(path.ToStdString());
        if (!file.is_open()) {
            LOTTIELOG("Failed to open JSON file: " << path.ToStdString());
            return false;
        }
        std::ostringstream oss;
        oss << file.rdbuf();
        jsonData = oss.str();
    } else {
        LOTTIELOG("Unsupported file extension: " << ext.ToStdString());
        return false;
    }

    return LoadFromData(jsonData, path.ToStdString());
#else
    LOTTIELOG("rlottie support not compiled in");
    return false;
#endif
}

bool LottiePlayer::LoadFromData(const std::string& jsonData, const std::string& key) {
#ifdef HAVE_RLOTTIE
    m_animation = rlottie::Animation::loadFromData(jsonData, key, "", false);
    
    if (!m_animation) {
        LOTTIELOG("Failed to parse Lottie animation");
        return false;
    }

    m_animation->size(m_width, m_height);
    m_totalFrames = m_animation->totalFrame();
    m_frameRate = m_animation->frameRate();
    m_duration = m_animation->duration();
    m_currentFrame = 0;

    // Set render size to animation size if not already set
    if (m_renderWidth == 0 || m_renderHeight == 0) {
        m_renderWidth = m_width;
        m_renderHeight = m_height;
    }

    // Allocate render buffer
    m_renderBuffer.resize(m_renderWidth * m_renderHeight);

    m_isLoaded = true;

    LOTTIELOG("Loaded: " << m_width << "x" << m_height 
              << " frames=" << m_totalFrames 
              << " fps=" << m_frameRate 
              << " duration=" << m_duration << "s");

    // Render first frame
    m_currentBitmap = RenderToBitmap(0);

    return true;
#else
    LOTTIELOG("rlottie support not compiled in");
    return false;
#endif
}

void LottiePlayer::SetRenderSize(size_t width, size_t height) {
    if (width == 0 || height == 0) return;
    
    // Maintain aspect ratio
    if (m_width > 0 && m_height > 0) {
        double scaleX = (double)width / m_width;
        double scaleY = (double)height / m_height;
        double scale = std::min(scaleX, scaleY);
        
        m_renderWidth = (size_t)(m_width * scale);
        m_renderHeight = (size_t)(m_height * scale);
    } else {
        m_renderWidth = width;
        m_renderHeight = height;
    }

#ifdef HAVE_RLOTTIE
    m_renderBuffer.resize(m_renderWidth * m_renderHeight);
#endif
}

wxBitmap LottiePlayer::RenderToBitmap(size_t frameNum) {
#ifdef HAVE_RLOTTIE
    if (!m_animation || m_renderWidth == 0 || m_renderHeight == 0) {
        return wxBitmap();
    }

    if (frameNum >= m_totalFrames) {
        frameNum = m_totalFrames - 1;
    }

    // Create surface for rendering
    rlottie::Surface surface(m_renderBuffer.data(), m_renderWidth, m_renderHeight, 
                             m_renderWidth * sizeof(uint32_t));

    // Render frame synchronously
    m_animation->renderSync(frameNum, surface);

    // Convert ARGB (premultiplied) to wxImage RGB + Alpha
    wxImage image(m_renderWidth, m_renderHeight, false);
    image.InitAlpha();
    
    unsigned char* rgb = image.GetData();
    unsigned char* alpha = image.GetAlpha();

    for (size_t i = 0; i < m_renderWidth * m_renderHeight; i++) {
        uint32_t pixel = m_renderBuffer[i];
        
        // rlottie outputs ARGB32_Premultiplied in native byte order
        uint8_t a = (pixel >> 24) & 0xFF;
        uint8_t r = (pixel >> 16) & 0xFF;
        uint8_t g = (pixel >> 8) & 0xFF;
        uint8_t b = pixel & 0xFF;

        // Un-premultiply alpha
        if (a > 0 && a < 255) {
            r = (r * 255) / a;
            g = (g * 255) / a;
            b = (b * 255) / a;
        }

        rgb[i * 3 + 0] = r;
        rgb[i * 3 + 1] = g;
        rgb[i * 3 + 2] = b;
        alpha[i] = a;
    }

    return wxBitmap(image);
#else
    return wxBitmap();
#endif
}

wxBitmap LottiePlayer::RenderFrame(double pos) {
    if (!m_isLoaded || m_totalFrames == 0) {
        return wxBitmap();
    }

    // Clamp position to [0, 1]
    if (pos < 0.0) pos = 0.0;
    if (pos > 1.0) pos = 1.0;

    size_t frameNum = (size_t)(pos * (m_totalFrames - 1));
    return RenderToBitmap(frameNum);
}

wxBitmap LottiePlayer::RenderFrameNum(size_t frameNum) {
    if (!m_isLoaded) {
        return wxBitmap();
    }
    return RenderToBitmap(frameNum);
}

void LottiePlayer::Play() {
    if (!m_isLoaded) return;
    m_isPlaying = true;
    LOTTIELOG("Play started");
}

void LottiePlayer::Stop() {
    m_isPlaying = false;
    m_currentFrame = 0;
    LOTTIELOG("Stop");
}

void LottiePlayer::Pause() {
    m_isPlaying = false;
    LOTTIELOG("Pause");
}

bool LottiePlayer::AdvanceFrame() {
    if (!m_isLoaded || !m_isPlaying) {
        return false;
    }

    m_currentFrame++;

    if (m_currentFrame >= m_totalFrames) {
        if (m_loop) {
            m_currentFrame = 0;
        } else {
            m_currentFrame = m_totalFrames - 1;
            m_isPlaying = false;
            return false;
        }
    }

    m_currentBitmap = RenderToBitmap(m_currentFrame);

    if (m_frameCallback && m_currentBitmap.IsOk()) {
        m_frameCallback(m_currentBitmap);
    }

    return true;
}

int LottiePlayer::GetTimerIntervalMs() const {
    if (m_frameRate <= 0) {
        return 33; // Default to ~30 fps
    }
    
    int interval = (int)(1000.0 / m_frameRate);
    
    // Clamp to reasonable range
    if (interval < 8) interval = 8;     // Max ~120 fps
    if (interval > 100) interval = 100; // Min 10 fps
    
    return interval;
}