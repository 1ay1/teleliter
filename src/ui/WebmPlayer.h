#ifndef WEBMPLAYER_H
#define WEBMPLAYER_H

#include <wx/wx.h>
#include <wx/timer.h>
#include <memory>
#include <vector>
#include <string>
#include <functional>

// Forward declarations for libvpx and libwebm
struct vpx_codec_ctx;
struct vpx_image;

namespace mkvparser {
    class MkvReader;
    class Segment;
    class Track;
    class VideoTrack;
    class Cluster;
    class BlockEntry;
}

// Callback for frame updates
using WebmFrameCallback = std::function<void(const wxBitmap& frame)>;

class WebmPlayer : public wxEvtHandler
{
public:
    WebmPlayer();
    ~WebmPlayer();
    
    // Load a .webm file
    bool LoadFile(const wxString& path);
    
    // Check if video is loaded
    bool IsLoaded() const { return m_isLoaded; }
    
    // Get video info
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    double GetFrameRate() const { return m_frameRate; }
    double GetDuration() const { return m_duration; }
    size_t GetTotalFrames() const { return m_totalFrames; }
    
    // Playback control
    void Play();
    void Stop();
    void Pause();
    bool IsPlaying() const { return m_isPlaying; }
    
    // Advance to next frame (for external timer control)
    // Returns true if animation should continue, false if ended
    bool AdvanceFrame();
    
    // Set the frame callback - called when a new frame is ready
    void SetFrameCallback(WebmFrameCallback callback) { m_frameCallback = callback; }
    
    // Get current frame number
    size_t GetCurrentFrame() const { return m_currentFrame; }
    
    // Get timer interval in milliseconds
    int GetTimerIntervalMs() const;
    
    // Loop control
    void SetLoop(bool loop) { m_loop = loop; }
    bool IsLooping() const { return m_loop; }
    
    // Render size (for scaling output)
    void SetRenderSize(int width, int height);
    int GetRenderWidth() const { return m_renderWidth > 0 ? m_renderWidth : m_width; }
    int GetRenderHeight() const { return m_renderHeight > 0 ? m_renderHeight : m_height; }
    
private:
    bool InitDecoder();
    void CleanupDecoder();
    bool DecodeNextFrame();
    wxBitmap ConvertFrameToBitmap(vpx_image* img);
    void RenderCurrentFrame();
    void SeekToStart();
    
    // File path
    wxString m_filePath;
    
    // Video properties
    int m_width;
    int m_height;
    double m_frameRate;
    double m_duration;
    size_t m_totalFrames;
    
    // Render size
    int m_renderWidth;
    int m_renderHeight;
    
    // Playback state
    bool m_isLoaded;
    bool m_isPlaying;
    bool m_loop;
    size_t m_currentFrame;
    
    // VPX decoder context
    vpx_codec_ctx* m_codec;
    
    // WebM parser state
    mkvparser::MkvReader* m_reader;
    mkvparser::Segment* m_segment;
    const mkvparser::VideoTrack* m_videoTrack;
    const mkvparser::Cluster* m_cluster;
    const mkvparser::BlockEntry* m_blockEntry;
    int m_blockFrameIndex;
    long m_videoTrackNumber;
    
    // Current decoded frame
    wxBitmap m_currentBitmap;
    
    // Frame callback
    WebmFrameCallback m_frameCallback;
};

#endif // WEBMPLAYER_H