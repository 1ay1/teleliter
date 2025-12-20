#ifndef LOTTIEPLAYER_H
#define LOTTIEPLAYER_H

#include <wx/wx.h>
#include <wx/timer.h>
#include <memory>
#include <vector>
#include <string>

#ifdef HAVE_RLOTTIE
#include <rlottie.h>
#endif

// Forward declaration
class LottiePlayer;

// Callback for frame updates
using LottieFrameCallback = std::function<void(const wxBitmap& frame)>;

class LottiePlayer : public wxEvtHandler
{
public:
    LottiePlayer();
    ~LottiePlayer();
    
    // Load a .tgs file (gzip-compressed Lottie JSON)
    bool LoadTgsFile(const wxString& path);
    
    // Load raw Lottie JSON data
    bool LoadJson(const std::string& json);
    
    // Check if animation is loaded
    bool IsLoaded() const { return m_isLoaded; }
    
    // Get animation info
    size_t GetTotalFrames() const { return m_totalFrames; }
    double GetFrameRate() const { return m_frameRate; }
    double GetDuration() const;  // in seconds
    wxSize GetSize() const { return m_size; }
    
    // Playback control
    void Play();
    void Stop();
    void Pause();
    bool IsPlaying() const { return m_isPlaying; }
    
    // Advance to next frame (for external timer control)
    // Returns true if animation should continue, false if ended
    bool AdvanceFrame();
    
    // Set the frame callback - called when a new frame is ready
    void SetFrameCallback(LottieFrameCallback callback) { m_frameCallback = callback; }
    
    // Render a specific frame to a bitmap
    wxBitmap RenderFrame(size_t frameNum);
    wxBitmap RenderFrame(size_t frameNum, int width, int height);
    
    // Get current frame
    size_t GetCurrentFrame() const { return m_currentFrame; }
    
    // Set render size (default uses animation's native size)
    void SetRenderSize(int width, int height);
    
    // Loop control
    void SetLoop(bool loop) { m_loop = loop; }
    bool IsLooping() const { return m_loop; }
    
    // Get timer interval in milliseconds
    int GetTimerIntervalMs() const;
    
private:
    void OnTimer(wxTimerEvent& event);
    bool DecompressTgs(const wxString& path, std::string& jsonOut);
    void RenderCurrentFrame();
    
#ifdef HAVE_RLOTTIE
    std::unique_ptr<rlottie::Animation> m_animation;
#endif
    
    wxTimer m_timer;
    
    bool m_isLoaded;
    bool m_isPlaying;
    bool m_loop;
    
    size_t m_totalFrames;
    double m_frameRate;
    size_t m_currentFrame;
    
    wxSize m_size;           // Native animation size
    wxSize m_renderSize;     // Render output size
    
    std::vector<uint32_t> m_frameBuffer;  // ARGB pixel buffer
    
    LottieFrameCallback m_frameCallback;
    
    wxDECLARE_EVENT_TABLE();
};

#endif // LOTTIEPLAYER_H