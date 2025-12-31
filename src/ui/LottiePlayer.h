#ifndef LOTTIEPLAYER_H
#define LOTTIEPLAYER_H

#include <wx/wx.h>
#include <wx/bitmap.h>
#include <memory>
#include <functional>
#include <string>
#include <vector>

#ifdef HAVE_RLOTTIE
#include <rlottie.h>
#endif

// LottiePlayer - renders TGS (gzipped Lottie JSON) animations
// Uses rlottie library for rendering
class LottiePlayer {
public:
    LottiePlayer();
    ~LottiePlayer();

    // Load a TGS file (gzipped Lottie JSON)
    bool LoadFile(const wxString& path);
    
    // Load from raw JSON string (for already decompressed data)
    bool LoadFromData(const std::string& jsonData, const std::string& key);

    // Playback control
    void Play();
    void Stop();
    void Pause();
    bool IsPlaying() const { return m_isPlaying; }
    bool IsLoaded() const { return m_isLoaded; }

    // Looping
    void SetLoop(bool loop) { m_loop = loop; }
    bool GetLoop() const { return m_loop; }

    // Frame access
    bool AdvanceFrame();  // Advance to next frame, returns false if at end (and not looping)
    wxBitmap GetCurrentFrame() const { return m_currentBitmap; }
    
    // Get frame at specific position (0.0 to 1.0)
    wxBitmap RenderFrame(double pos);
    
    // Get frame at specific frame number
    wxBitmap RenderFrameNum(size_t frameNum);

    // Animation info
    size_t GetTotalFrames() const { return m_totalFrames; }
    double GetFrameRate() const { return m_frameRate; }
    double GetDuration() const { return m_duration; }
    size_t GetWidth() const { return m_width; }
    size_t GetHeight() const { return m_height; }
    size_t GetCurrentFrameNum() const { return m_currentFrame; }

    // Set render size (for scaling)
    void SetRenderSize(size_t width, size_t height);

    // Timer interval for animation (in milliseconds)
    int GetTimerIntervalMs() const;

    // Frame callback - called when a new frame is rendered
    void SetFrameCallback(std::function<void(const wxBitmap&)> callback) {
        m_frameCallback = callback;
    }

private:
    // Decompress TGS (gzip) to JSON string
    static bool DecompressTGS(const wxString& path, std::string& jsonOut);
    
    // Render a specific frame to bitmap
    wxBitmap RenderToBitmap(size_t frameNum);

#ifdef HAVE_RLOTTIE
    std::unique_ptr<rlottie::Animation> m_animation;
    std::vector<uint32_t> m_renderBuffer;
#endif

    wxBitmap m_currentBitmap;
    std::function<void(const wxBitmap&)> m_frameCallback;

    size_t m_width;
    size_t m_height;
    size_t m_renderWidth;
    size_t m_renderHeight;
    size_t m_totalFrames;
    size_t m_currentFrame;
    double m_frameRate;
    double m_duration;

    bool m_isLoaded;
    bool m_isPlaying;
    bool m_loop;
};

#endif // LOTTIEPLAYER_H