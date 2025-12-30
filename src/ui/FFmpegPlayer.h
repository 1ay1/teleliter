#ifndef FFMPEGPLAYER_H
#define FFMPEGPLAYER_H

#include <wx/wx.h>
#include <wx/timer.h>
#include <memory>
#include <string>
#include <functional>
#include <cstdint>
#include <vector>
#include <mutex>
#include <atomic>
#include <queue>

// Forward declarations for FFmpeg types
extern "C" {
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct SwrContext;
}

// Callback for frame updates
using FFmpegFrameCallback = std::function<void(const wxBitmap& frame)>;

class FFmpegPlayer : public wxEvtHandler
{
public:
    FFmpegPlayer();
    ~FFmpegPlayer();
    
    // Load a media file (supports video: MP4, WebM, AVI, MKV, MOV, etc.
    // and audio: OGG, MP3, WAV, etc.)
    bool LoadFile(const wxString& path);
    
    // Check if media is loaded
    bool IsLoaded() const { return m_isLoaded; }
    
    // Check if this is audio-only (no video stream)
    bool IsAudioOnly() const { return m_isAudioOnly; }
    
    // Check if audio is available
    bool HasAudio() const { return m_hasAudio; }
    
    // Get video info
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    double GetFrameRate() const { return m_frameRate; }
    double GetDuration() const { return m_duration; }
    double GetCurrentTime() const { 
        // For audio-only files, calculate time from bytes played
        // 48000 Hz * 2 channels * 2 bytes per sample = 192000 bytes per second
        if (m_isAudioOnly && m_audioBytesPlayed > 0) {
            return static_cast<double>(m_audioBytesPlayed) / 192000.0;
        }
        return m_currentTime; 
    }
    
    // Playback control
    void Play();
    void Stop();
    void Pause();
    bool IsPlaying() const { return m_isPlaying; }
    
    // Seeking
    void Seek(double timeSeconds);
    
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
    
    // Audio callback for SDL2 (static because SDL needs C callback)
    static void AudioCallback(void* userdata, uint8_t* stream, int len);
    
private:
    bool InitDecoder();
    bool InitAudioDecoder();
    bool InitSDLAudio();
    void CleanupDecoder();
    void CleanupAudio();
    bool DecodeNextFrame();
    bool DecodeAudioFrame();
    void FillAudioBuffer();
    void ReadAndRoutePackets();  // Unified demuxer that routes packets to correct queue
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
    bool m_hitEOF;  // Track when we've read all packets from the file
    size_t m_currentFrame;
    double m_currentTime;  // Current playback position in seconds
    
    // Audio state
    double m_volume;
    bool m_muted;
    bool m_isAudioOnly;
    bool m_hasAudio;
    
    // FFmpeg contexts (video)
    AVFormatContext* m_formatCtx;
    AVCodecContext* m_codecCtx;
    AVFrame* m_frame;
    AVFrame* m_frameRGB;
    AVPacket* m_packet;
    SwsContext* m_swsCtx;
    
    // FFmpeg contexts (audio)
    AVCodecContext* m_audioCodecCtx;
    AVFrame* m_audioFrame;
    SwrContext* m_swrCtx;
    
    // Stream indices
    int m_videoStreamIndex;
    int m_audioStreamIndex;
    
    // Audio buffer for SDL
    std::vector<uint8_t> m_audioBuffer;
    std::atomic<size_t> m_audioBufferReadPos;
    std::atomic<size_t> m_audioBufferWritePos;
    std::atomic<size_t> m_audioBytesPlayed;  // Total bytes played for time tracking
    std::mutex m_audioMutex;
    static const size_t AUDIO_BUFFER_SIZE = 192000;  // ~1 second at 48kHz stereo 16-bit
    
    // Packet queues to avoid losing packets when demuxing
    std::queue<AVPacket*> m_videoPacketQueue;
    std::queue<AVPacket*> m_audioPacketQueue;
    std::mutex m_videoPacketMutex;
    std::mutex m_audioPacketMutex;
    static const size_t MAX_PACKET_QUEUE_SIZE = 64;
    
    // SDL audio state
    bool m_sdlAudioInitialized;
    uint32_t m_sdlAudioDeviceId;
    
    // RGB buffer for conversion
    uint8_t* m_rgbBuffer;
    int m_rgbBufferSize;
    
    // Current decoded frame as bitmap
    wxBitmap m_currentBitmap;
    
    // Frame callback
    FFmpegFrameCallback m_frameCallback;
};

#endif // FFMPEGPLAYER_H