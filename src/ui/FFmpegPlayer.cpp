#include "FFmpegPlayer.h"
#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#define FFMPEGLOG(msg) std::cerr << "[FFmpegPlayer] " << msg << std::endl

FFmpegPlayer::FFmpegPlayer()
    : m_width(0),
      m_height(0),
      m_frameRate(30.0),
      m_duration(0.0),
      m_renderWidth(0),
      m_renderHeight(0),
      m_isLoaded(false),
      m_isPlaying(false),
      m_loop(true),
      m_currentFrame(0),
      m_volume(0.5),
      m_muted(true),
      m_formatCtx(nullptr),
      m_codecCtx(nullptr),
      m_frame(nullptr),
      m_frameRGB(nullptr),
      m_packet(nullptr),
      m_swsCtx(nullptr),
      m_videoStreamIndex(-1),
      m_rgbBuffer(nullptr),
      m_rgbBufferSize(0)
{
}

FFmpegPlayer::~FFmpegPlayer()
{
    Stop();
    CleanupDecoder();
}

void FFmpegPlayer::CleanupDecoder()
{
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
    
    if (m_rgbBuffer) {
        av_free(m_rgbBuffer);
        m_rgbBuffer = nullptr;
        m_rgbBufferSize = 0;
    }
    
    if (m_frameRGB) {
        av_frame_free(&m_frameRGB);
        m_frameRGB = nullptr;
    }
    
    if (m_frame) {
        av_frame_free(&m_frame);
        m_frame = nullptr;
    }
    
    if (m_packet) {
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }
    
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }
    
    if (m_formatCtx) {
        avformat_close_input(&m_formatCtx);
        m_formatCtx = nullptr;
    }
    
    m_videoStreamIndex = -1;
    m_isLoaded = false;
}

bool FFmpegPlayer::LoadFile(const wxString& path)
{
    FFMPEGLOG("LoadFile: " << path.ToStdString());
    
    // Clean up any previous state
    CleanupDecoder();
    m_filePath = path;
    m_currentFrame = 0;
    
    // Open the input file
    std::string pathStr = path.ToStdString();
    int ret = avformat_open_input(&m_formatCtx, pathStr.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        FFMPEGLOG("Failed to open file: " << errBuf);
        return false;
    }
    
    // Retrieve stream information
    ret = avformat_find_stream_info(m_formatCtx, nullptr);
    if (ret < 0) {
        FFMPEGLOG("Failed to find stream info");
        CleanupDecoder();
        return false;
    }
    
    // Find the video stream
    m_videoStreamIndex = -1;
    for (unsigned int i = 0; i < m_formatCtx->nb_streams; i++) {
        if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIndex = i;
            break;
        }
    }
    
    if (m_videoStreamIndex < 0) {
        FFMPEGLOG("No video stream found");
        CleanupDecoder();
        return false;
    }
    
    AVStream* videoStream = m_formatCtx->streams[m_videoStreamIndex];
    AVCodecParameters* codecParams = videoStream->codecpar;
    
    // Find the decoder
    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        FFMPEGLOG("Unsupported codec");
        CleanupDecoder();
        return false;
    }
    
    // Allocate codec context
    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        FFMPEGLOG("Failed to allocate codec context");
        CleanupDecoder();
        return false;
    }
    
    // Copy codec parameters
    ret = avcodec_parameters_to_context(m_codecCtx, codecParams);
    if (ret < 0) {
        FFMPEGLOG("Failed to copy codec parameters");
        CleanupDecoder();
        return false;
    }
    
    // Open the codec
    ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0) {
        FFMPEGLOG("Failed to open codec");
        CleanupDecoder();
        return false;
    }
    
    // Store video properties
    m_width = m_codecCtx->width;
    m_height = m_codecCtx->height;
    
    // Calculate frame rate
    if (videoStream->avg_frame_rate.den > 0 && videoStream->avg_frame_rate.num > 0) {
        m_frameRate = av_q2d(videoStream->avg_frame_rate);
    } else if (videoStream->r_frame_rate.den > 0 && videoStream->r_frame_rate.num > 0) {
        m_frameRate = av_q2d(videoStream->r_frame_rate);
    } else {
        m_frameRate = 30.0; // Default
    }
    
    // Clamp frame rate to reasonable values
    if (m_frameRate < 1.0) m_frameRate = 1.0;
    if (m_frameRate > 120.0) m_frameRate = 120.0;
    
    // Calculate duration
    if (m_formatCtx->duration > 0) {
        m_duration = (double)m_formatCtx->duration / AV_TIME_BASE;
    } else {
        m_duration = 0.0;
    }
    
    FFMPEGLOG("Video: " << m_width << "x" << m_height << " @ " << m_frameRate << " fps, duration: " << m_duration << "s");
    
    // Allocate frames
    m_frame = av_frame_alloc();
    m_frameRGB = av_frame_alloc();
    if (!m_frame || !m_frameRGB) {
        FFMPEGLOG("Failed to allocate frames");
        CleanupDecoder();
        return false;
    }
    
    // Allocate packet
    m_packet = av_packet_alloc();
    if (!m_packet) {
        FFMPEGLOG("Failed to allocate packet");
        CleanupDecoder();
        return false;
    }
    
    // Determine output size (use render size if set, otherwise original)
    int outWidth = m_renderWidth > 0 ? m_renderWidth : m_width;
    int outHeight = m_renderHeight > 0 ? m_renderHeight : m_height;
    
    // Allocate RGB buffer
    m_rgbBufferSize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, outWidth, outHeight, 1);
    m_rgbBuffer = (uint8_t*)av_malloc(m_rgbBufferSize);
    if (!m_rgbBuffer) {
        FFMPEGLOG("Failed to allocate RGB buffer");
        CleanupDecoder();
        return false;
    }
    
    // Set up the RGB frame
    av_image_fill_arrays(m_frameRGB->data, m_frameRGB->linesize, m_rgbBuffer,
                         AV_PIX_FMT_RGB24, outWidth, outHeight, 1);
    
    // Initialize the scaling context
    m_swsCtx = sws_getContext(
        m_width, m_height, m_codecCtx->pix_fmt,
        outWidth, outHeight, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    
    if (!m_swsCtx) {
        FFMPEGLOG("Failed to create scaling context");
        CleanupDecoder();
        return false;
    }
    
    m_isLoaded = true;
    
    // Decode the first frame
    if (!DecodeNextFrame()) {
        FFMPEGLOG("Failed to decode first frame");
        CleanupDecoder();
        return false;
    }
    
    return true;
}

void FFmpegPlayer::SeekToStart()
{
    if (!m_formatCtx || m_videoStreamIndex < 0) return;
    
    // Seek to the beginning of the video stream
    av_seek_frame(m_formatCtx, m_videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    
    // Flush the codec buffers
    if (m_codecCtx) {
        avcodec_flush_buffers(m_codecCtx);
    }
    
    m_currentFrame = 0;
}

bool FFmpegPlayer::DecodeNextFrame()
{
    if (!m_formatCtx || !m_codecCtx || !m_frame || !m_packet) {
        return false;
    }
    
    while (true) {
        // Read a packet
        int ret = av_read_frame(m_formatCtx, m_packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                // End of file
                if (m_loop) {
                    SeekToStart();
                    continue;
                }
                return false;
            }
            FFMPEGLOG("Error reading frame");
            return false;
        }
        
        // Check if this packet is from the video stream
        if (m_packet->stream_index != m_videoStreamIndex) {
            av_packet_unref(m_packet);
            continue;
        }
        
        // Send packet to decoder
        ret = avcodec_send_packet(m_codecCtx, m_packet);
        av_packet_unref(m_packet);
        
        if (ret < 0) {
            FFMPEGLOG("Error sending packet to decoder");
            continue;
        }
        
        // Receive frame from decoder
        ret = avcodec_receive_frame(m_codecCtx, m_frame);
        if (ret == AVERROR(EAGAIN)) {
            // Need more packets
            continue;
        } else if (ret < 0) {
            FFMPEGLOG("Error receiving frame from decoder");
            return false;
        }
        
        // Successfully decoded a frame
        m_currentFrame++;
        
        // Convert frame to bitmap
        m_currentBitmap = ConvertFrameToBitmap();
        
        return true;
    }
}

wxBitmap FFmpegPlayer::ConvertFrameToBitmap()
{
    if (!m_frame || !m_frameRGB || !m_swsCtx) {
        return wxBitmap();
    }
    
    int outWidth = m_renderWidth > 0 ? m_renderWidth : m_width;
    int outHeight = m_renderHeight > 0 ? m_renderHeight : m_height;
    
    // Convert the frame to RGB
    sws_scale(m_swsCtx, m_frame->data, m_frame->linesize, 0, m_height,
              m_frameRGB->data, m_frameRGB->linesize);
    
    // Create wxImage from RGB data
    wxImage image(outWidth, outHeight);
    
    // Copy RGB data to wxImage
    unsigned char* destData = image.GetData();
    int srcLineSize = m_frameRGB->linesize[0];
    
    for (int y = 0; y < outHeight; y++) {
        const uint8_t* srcLine = m_frameRGB->data[0] + y * srcLineSize;
        unsigned char* destLine = destData + y * outWidth * 3;
        memcpy(destLine, srcLine, outWidth * 3);
    }
    
    return wxBitmap(image);
}

int FFmpegPlayer::GetTimerIntervalMs() const
{
    if (m_frameRate <= 0) {
        return 33; // ~30 fps default
    }
    return static_cast<int>(1000.0 / m_frameRate);
}

void FFmpegPlayer::SetRenderSize(int width, int height)
{
    if (m_renderWidth == width && m_renderHeight == height) {
        return;
    }
    
    m_renderWidth = width;
    m_renderHeight = height;
    
    // If already loaded, we need to recreate the scaling context and buffers
    if (m_isLoaded && m_swsCtx) {
        sws_freeContext(m_swsCtx);
        
        int outWidth = m_renderWidth > 0 ? m_renderWidth : m_width;
        int outHeight = m_renderHeight > 0 ? m_renderHeight : m_height;
        
        // Reallocate RGB buffer
        if (m_rgbBuffer) {
            av_free(m_rgbBuffer);
        }
        m_rgbBufferSize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, outWidth, outHeight, 1);
        m_rgbBuffer = (uint8_t*)av_malloc(m_rgbBufferSize);
        
        if (m_rgbBuffer && m_frameRGB) {
            av_image_fill_arrays(m_frameRGB->data, m_frameRGB->linesize, m_rgbBuffer,
                                 AV_PIX_FMT_RGB24, outWidth, outHeight, 1);
        }
        
        m_swsCtx = sws_getContext(
            m_width, m_height, m_codecCtx->pix_fmt,
            outWidth, outHeight, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
    }
}

bool FFmpegPlayer::AdvanceFrame()
{
    if (!m_isLoaded || !m_isPlaying) {
        return false;
    }
    
    bool hasFrame = DecodeNextFrame();
    
    if (hasFrame && m_frameCallback) {
        m_frameCallback(m_currentBitmap);
    }
    
    return hasFrame;
}

void FFmpegPlayer::Play()
{
    if (!m_isLoaded) return;
    
    m_isPlaying = true;
    FFMPEGLOG("Play started");
}

void FFmpegPlayer::Stop()
{
    m_isPlaying = false;
    if (m_isLoaded) {
        SeekToStart();
    }
    FFMPEGLOG("Stop");
}

void FFmpegPlayer::Pause()
{
    m_isPlaying = false;
    FFMPEGLOG("Pause");
}