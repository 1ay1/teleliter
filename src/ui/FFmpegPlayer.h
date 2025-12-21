#ifndef FFMPEGPLAYER_H
#define FFMPEGPLAYER_H

#include <wx/wx.h>
#include <wx/timer.h>
#include <memory>
#include <string>
#include <functional>
#include <cstdint>

// Forward declarations for FFmpeg types
extern "C" {
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;
}

// Callback for frame updates
using FFmpegFrameCallback = std::function<void(const wxBitmap& frame)>;

class FFmpegPlayer : public wxEvtHandler
{
public:
    FFmpegPlayer();
    ~FFmpegPlayer();
    
    // Load a video file (supports MP4, WebM, AVI, MKV, MOV, etc.)
    bool LoadFile(const wxString& path);
    
    // Check if video is loaded
    bool IsLoaded() const { return m_isLoaded; }
    
    // Get video info
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    double GetFrameRate() const { return m_frameRate; }
    double GetDuration() const { return m_duration; }
    
    // Playback control
    void Play();
    void Stop();
    void Pause();
    bool IsPlaying() const { return m_isPlaying; }
    
    // Advance to next frame (for external timer control)
    // Returns true if animation should continue, false if ended
    bool AdvanceFrame();
    
    // Set the frame callback - called when a new frame is ready
    void SetFrameCallback(FFmpegFrameCallback callback) { m_frameCallback = callback; }
    
    // Get current frame number
    size_t GetCurrentFrame() const { return m_currentFrame; }
    
    // Get timer interval in milliseconds
    int GetTimerIntervalMs() const;
    
    // Loop control
    void SetLoop(bool loop) { m_loop = loop; }
    bool IsLooping() const { return m_loop; }
    
    // Volume control (0.0 to 1.0) - for future audio support
    void SetVolume(double volume) { m_volume = volume; }
    double GetVolume() const { return m_volume; }
    
    // Mute control
    void SetMuted(bool muted) { m_muted = muted; }
    bool IsMuted() const { return m_muted; }
    
    // Render size (for scaling output)
    void SetRenderSize(int width, int height);
    int GetRenderWidth() const { return m_renderWidth > 0 ? m_renderWidth : m_width; }
    int GetRenderHeight() const { return m_renderHeight > 0 ? m_renderHeight : m_height; }
    
private:
    bool InitDecoder();
    void CleanupDecoder();
    bool DecodeNextFrame();
    wxBitmap ConvertFrameToBitmap();
    void SeekToStart();
    
    // File path
    wxString m_filePath;
    
    // Video properties
    int m_width;
    int m_height;
    double m_frameRate;
    double m_duration;
    
    // Render size
    int m_renderWidth;
    int m_renderHeight;
    
    // Playback state
    bool m_isLoaded;
    bool m_isPlaying;
    bool m_loop;
    size_t m_currentFrame;
    
    // Audio state
    double m_volume;
    bool m_muted;
    
    // FFmpeg contexts
    AVFormatContext* m_formatCtx;
    AVCodecContext* m_codecCtx;
    AVFrame* m_frame;
    AVFrame* m_frameRGB;
    AVPacket* m_packet;
    SwsContext* m_swsCtx;
    
    // Stream index
    int m_videoStreamIndex;
    
    // RGB buffer for conversion
    uint8_t* m_rgbBuffer;
    int m_rgbBufferSize;
    
    // Current decoded frame as bitmap
    wxBitmap m_currentBitmap;
    
    // Frame callback
    FFmpegFrameCallback m_frameCallback;
};

#endif // FFMPEGPLAYER_H