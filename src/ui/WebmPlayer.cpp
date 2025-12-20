#include "WebmPlayer.h"
#include <vpx/vpx_decoder.h>
#include <vpx/vp8dx.h>
#include <mkvparser/mkvparser.h>
#include <mkvparser/mkvreader.h>
#include <iostream>
#include <iomanip>
#include <cstring>

WebmPlayer::WebmPlayer()
    : m_width(0),
      m_height(0),
      m_frameRate(30.0),
      m_duration(0.0),
      m_totalFrames(0),
      m_renderWidth(0),
      m_renderHeight(0),
      m_isLoaded(false),
      m_isPlaying(false),
      m_loop(true),
      m_currentFrame(0),
      m_codec(nullptr),
      m_reader(nullptr),
      m_segment(nullptr),
      m_videoTrack(nullptr),
      m_cluster(nullptr),
      m_blockEntry(nullptr),
      m_blockFrameIndex(0),
      m_videoTrackNumber(-1)
{
}

WebmPlayer::~WebmPlayer()
{
    Stop();
    CleanupDecoder();
}

void WebmPlayer::CleanupDecoder()
{
    if (m_codec) {
        vpx_codec_destroy(m_codec);
        delete m_codec;
        m_codec = nullptr;
    }
    
    if (m_segment) {
        delete m_segment;
        m_segment = nullptr;
    }
    
    if (m_reader) {
        delete m_reader;
        m_reader = nullptr;
    }
    
    m_videoTrack = nullptr;
    m_cluster = nullptr;
    m_blockEntry = nullptr;
    m_isLoaded = false;
}

bool WebmPlayer::InitDecoder()
{
    if (!m_videoTrack) {
        return false;
    }
    
    // Get codec ID from track
    const char* codecId = m_videoTrack->GetCodecId();
    
    // Determine codec interface based on track codec ID
    vpx_codec_iface_t* codecInterface = nullptr;
    bool isVP9 = false;
    
    if (codecId) {
        std::string codec(codecId);
        if (codec.find("VP9") != std::string::npos || codec.find("vp9") != std::string::npos ||
            codec.find("vp09") != std::string::npos) {
            codecInterface = vpx_codec_vp9_dx();
            isVP9 = true;
        } else if (codec.find("VP8") != std::string::npos || codec.find("vp8") != std::string::npos) {
            codecInterface = vpx_codec_vp8_dx();
        }
    }
    
    // Default to VP9 if not determined
    if (!codecInterface) {
        codecInterface = vpx_codec_vp9_dx();
        isVP9 = true;
    }
    
    m_codec = new vpx_codec_ctx_t();
    memset(m_codec, 0, sizeof(vpx_codec_ctx_t));
    
    vpx_codec_dec_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.threads = 4;  // More threads for better performance
    cfg.w = m_width;
    cfg.h = m_height;
    
    // Try with VPX_CODEC_USE_FRAME_THREADING for better compatibility
    vpx_codec_flags_t flags = 0;
    
    // First try without any special flags
    vpx_codec_err_t res = vpx_codec_dec_init(m_codec, codecInterface, &cfg, flags);
    if (res != VPX_CODEC_OK) {
        // Try the other codec
        if (isVP9) {
            res = vpx_codec_dec_init(m_codec, vpx_codec_vp8_dx(), &cfg, flags);
        } else {
            res = vpx_codec_dec_init(m_codec, vpx_codec_vp9_dx(), &cfg, flags);
        }
        
        if (res != VPX_CODEC_OK) {
            delete m_codec;
            m_codec = nullptr;
            return false;
        }
    }
    return true;
}

bool WebmPlayer::LoadFile(const wxString& path)
{
    CleanupDecoder();
    
    m_filePath = path;
    
    // Create MkvReader
    m_reader = new mkvparser::MkvReader();
    if (m_reader->Open(path.ToStdString().c_str()) != 0) {
        CleanupDecoder();
        return false;
    }
    
    // Parse WebM header
    long long pos = 0;
    mkvparser::EBMLHeader ebmlHeader;
    long long ret = ebmlHeader.Parse(m_reader, pos);
    if (ret < 0) {
        CleanupDecoder();
        return false;
    }
    
    // Create segment
    ret = mkvparser::Segment::CreateInstance(m_reader, pos, m_segment);
    if (ret != 0 || !m_segment) {
        CleanupDecoder();
        return false;
    }
    
    // Load segment
    ret = m_segment->Load();
    if (ret < 0) {
        CleanupDecoder();
        return false;
    }
    
    // Find video track
    const mkvparser::Tracks* tracks = m_segment->GetTracks();
    if (!tracks) {
        CleanupDecoder();
        return false;
    }
    
    for (unsigned long i = 0; i < tracks->GetTracksCount(); i++) {
        const mkvparser::Track* track = tracks->GetTrackByIndex(i);
        if (!track) continue;
        
        if (track->GetType() == mkvparser::Track::kVideo) {
            const mkvparser::VideoTrack* vt = static_cast<const mkvparser::VideoTrack*>(track);
            
            // Use first video track
            if (!m_videoTrack) {
                m_videoTrack = vt;
                m_videoTrackNumber = track->GetNumber();
            }
        }
    }
    
    if (!m_videoTrack) {
        CleanupDecoder();
        return false;
    }
    
    // Get video properties
    m_width = static_cast<int>(m_videoTrack->GetWidth());
    m_height = static_cast<int>(m_videoTrack->GetHeight());
    m_frameRate = m_videoTrack->GetFrameRate();
    if (m_frameRate <= 0) {
        m_frameRate = 30.0;  // Default if not specified
    }
    
    // Get duration
    const mkvparser::SegmentInfo* info = m_segment->GetInfo();
    if (info) {
        m_duration = info->GetDuration() / 1000000000.0;  // nanoseconds to seconds
        m_totalFrames = static_cast<size_t>(m_duration * m_frameRate);
    }
    
    // Initialize VPX decoder
    if (!InitDecoder()) {
        CleanupDecoder();
        return false;
    }
    
    // Get first cluster
    m_cluster = m_segment->GetFirst();
    m_blockEntry = nullptr;
    m_blockFrameIndex = 0;
    m_currentFrame = 0;
    
    m_isLoaded = true;
    
    // Decode first frame
    if (DecodeNextFrame()) {
        RenderCurrentFrame();
    }
    
    return true;
}

void WebmPlayer::SeekToStart()
{
    if (!m_segment) return;
    
    m_cluster = m_segment->GetFirst();
    m_blockEntry = nullptr;
    m_blockFrameIndex = 0;
    m_currentFrame = 0;
}

bool WebmPlayer::DecodeNextFrame()
{
    if (!m_isLoaded || !m_codec || !m_segment) {
        return false;
    }
    
    while (m_cluster && !m_cluster->EOS()) {
        // Get next block entry
        if (!m_blockEntry) {
            long status = m_cluster->GetFirst(m_blockEntry);
            if (status < 0) {
                m_cluster = m_segment->GetNext(m_cluster);
                continue;
            }
        } else {
            long status = m_cluster->GetNext(m_blockEntry, m_blockEntry);
            if (status < 0 || !m_blockEntry || m_blockEntry->EOS()) {
                m_cluster = m_segment->GetNext(m_cluster);
                m_blockEntry = nullptr;
                m_blockFrameIndex = 0;
                continue;
            }
        }
        
        if (!m_blockEntry || m_blockEntry->EOS()) {
            continue;
        }
        
        const mkvparser::Block* block = m_blockEntry->GetBlock();
        if (!block) continue;
        
        // Check if this is our video track
        if (block->GetTrackNumber() != m_videoTrackNumber) {
            continue;
        }
        
        // Process frames in this block
        int frameCount = block->GetFrameCount();
        
        // Check if this is a keyframe
        bool isKeyframe = block->IsKey();
        
        while (m_blockFrameIndex < frameCount) {
            const mkvparser::Block::Frame& frame = block->GetFrame(m_blockFrameIndex);
            m_blockFrameIndex++;
            
            // Skip very small frames - likely superframe index or not valid video data
            if (frame.len < 32) {
                continue;
            }
            
            // Read frame data
            std::vector<uint8_t> frameData(frame.len);
            if (frame.Read(m_reader, frameData.data()) != 0) {
                continue;
            }
            
            // For now, only decode the first keyframe and show it statically
            // VP9 inter-frame decoding has issues with these Telegram stickers
            // TODO: Investigate VP9 profile/feature compatibility with libvpx
            if (m_currentFrame > 0) {
                // We already have frame 0, just keep returning that
                // This gives us a static preview which is better than nothing
                m_currentFrame++;
                if (m_currentBitmap.IsOk()) {
                    return true;  // Return the cached first frame
                }
                continue;
            }
            
            // Only decode keyframes (first frame is always a keyframe)
            if (!isKeyframe) {
                continue;
            }
            
            // Decode the keyframe
            vpx_codec_err_t res = vpx_codec_decode(m_codec, frameData.data(), 
                                                    static_cast<unsigned int>(frameData.size()), 
                                                    nullptr, 0);
            
            if (res != VPX_CODEC_OK) {
                continue;
            }
            
            // Get decoded image
            vpx_codec_iter_t iter = nullptr;
            vpx_image_t* img = vpx_codec_get_frame(m_codec, &iter);
            if (img) {
                m_currentBitmap = ConvertFrameToBitmap(img);
                m_currentFrame++;
                return true;
            }
        }
        
        // Done with this block, reset frame index
        m_blockFrameIndex = 0;
    }
    
    // End of video
    return false;
}

wxBitmap WebmPlayer::ConvertFrameToBitmap(vpx_image* img)
{
    if (!img) return wxNullBitmap;
    
    int w = img->d_w;
    int h = img->d_h;
    
    // Validate dimensions to prevent pixman errors
    if (w <= 0 || h <= 0) {
        return wxNullBitmap;
    }
    
    // Determine output size
    int outW = (m_renderWidth > 0) ? m_renderWidth : w;
    int outH = (m_renderHeight > 0) ? m_renderHeight : h;
    
    wxImage wxImg(w, h);
    if (!wxImg.IsOk()) {
        return wxNullBitmap;
    }
    unsigned char* rgbData = wxImg.GetData();
    
    // Convert YUV (I420) to RGB
    unsigned char* yPlane = img->planes[0];
    unsigned char* uPlane = img->planes[1];
    unsigned char* vPlane = img->planes[2];
    int yStride = img->stride[0];
    int uStride = img->stride[1];
    (void)img->stride[2];  // vStride not needed for I420
    
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int yIdx = y * yStride + x;
            int uvIdx = (y / 2) * uStride + (x / 2);
            
            int Y = yPlane[yIdx];
            int U = uPlane[uvIdx] - 128;
            int V = vPlane[uvIdx] - 128;
            
            // YUV to RGB conversion
            int R = Y + (1.402 * V);
            int G = Y - (0.344136 * U) - (0.714136 * V);
            int B = Y + (1.772 * U);
            
            // Clamp values
            R = (R < 0) ? 0 : ((R > 255) ? 255 : R);
            G = (G < 0) ? 0 : ((G > 255) ? 255 : G);
            B = (B < 0) ? 0 : ((B > 255) ? 255 : B);
            
            int idx = (y * w + x) * 3;
            rgbData[idx] = static_cast<unsigned char>(R);
            rgbData[idx + 1] = static_cast<unsigned char>(G);
            rgbData[idx + 2] = static_cast<unsigned char>(B);
        }
    }
    
    // Scale if needed
    if (outW != w || outH != h) {
        if (outW > 0 && outH > 0) {
            wxImg = wxImg.Scale(outW, outH, wxIMAGE_QUALITY_BILINEAR);
        }
    }
    
    if (!wxImg.IsOk()) {
        return wxNullBitmap;
    }
    
    return wxBitmap(wxImg);
}

void WebmPlayer::RenderCurrentFrame()
{
    if (m_currentBitmap.IsOk() && m_frameCallback) {
        m_frameCallback(m_currentBitmap);
    }
}

int WebmPlayer::GetTimerIntervalMs() const
{
    if (m_frameRate <= 0) return 33;  // Default ~30fps
    int intervalMs = static_cast<int>(1000.0 / m_frameRate);
    if (intervalMs < 16) intervalMs = 16;  // Cap at ~60fps
    return intervalMs;
}

void WebmPlayer::SetRenderSize(int width, int height)
{
    m_renderWidth = width;
    m_renderHeight = height;
}

bool WebmPlayer::AdvanceFrame()
{
    if (!m_isLoaded || !m_isPlaying) {
        return false;
    }
    
    if (!DecodeNextFrame()) {
        // End of video
        if (m_loop) {
            SeekToStart();
            if (!DecodeNextFrame()) {
                return false;
            }
        } else {
            m_isPlaying = false;
            return false;
        }
    }
    
    RenderCurrentFrame();
    return true;
}

void WebmPlayer::Play()
{
    if (!m_isLoaded || m_isPlaying) {
        return;
    }
    
    m_isPlaying = true;
    RenderCurrentFrame();
}

void WebmPlayer::Stop()
{
    m_isPlaying = false;
    SeekToStart();
}

void WebmPlayer::Pause()
{
    m_isPlaying = false;
}