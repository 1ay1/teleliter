#include "FFmpegPlayer.h"
#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

#ifdef HAVE_SDL2
#include <SDL.h>
#endif

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
      m_hitEOF(false),
      m_currentFrame(0),
      m_currentTime(0.0),
      m_volume(0.5),
      m_muted(false),
      m_isAudioOnly(false),
      m_hasAudio(false),
      m_formatCtx(nullptr),
      m_codecCtx(nullptr),
      m_frame(nullptr),
      m_frameRGB(nullptr),
      m_packet(nullptr),
      m_swsCtx(nullptr),
      m_audioCodecCtx(nullptr),
      m_audioFrame(nullptr),
      m_swrCtx(nullptr),
      m_videoStreamIndex(-1),
      m_audioStreamIndex(-1),
      m_audioBufferReadPos(0),
      m_audioBufferWritePos(0),
      m_audioBytesPlayed(0),
      m_sdlAudioInitialized(false),
      m_sdlAudioDeviceId(0),
      m_rgbBuffer(nullptr),
      m_rgbBufferSize(0)
{
    m_audioBuffer.resize(AUDIO_BUFFER_SIZE);
}

FFmpegPlayer::~FFmpegPlayer()
{
    Stop();
    CleanupAudio();
    CleanupDecoder();
}

void FFmpegPlayer::CleanupAudio()
{
#ifdef HAVE_SDL2
    if (m_sdlAudioDeviceId > 0) {
        SDL_CloseAudioDevice(m_sdlAudioDeviceId);
        m_sdlAudioDeviceId = 0;
    }
    if (m_sdlAudioInitialized) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_sdlAudioInitialized = false;
    }
#endif

    // Clear audio packet queue
    {
        std::lock_guard<std::mutex> lock(m_audioPacketMutex);
        while (!m_audioPacketQueue.empty()) {
            AVPacket* pkt = m_audioPacketQueue.front();
            m_audioPacketQueue.pop();
            av_packet_free(&pkt);
        }
    }

    if (m_swrCtx) {
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
    }

    if (m_audioFrame) {
        av_frame_free(&m_audioFrame);
        m_audioFrame = nullptr;
    }

    if (m_audioCodecCtx) {
        avcodec_free_context(&m_audioCodecCtx);
        m_audioCodecCtx = nullptr;
    }

    m_audioStreamIndex = -1;
    m_hasAudio = false;
    m_audioBufferReadPos = 0;
    m_audioBufferWritePos = 0;
}

void FFmpegPlayer::CleanupDecoder()
{
    CleanupAudio();

    // Clear video packet queue
    {
        std::lock_guard<std::mutex> lock(m_videoPacketMutex);
        while (!m_videoPacketQueue.empty()) {
            AVPacket* pkt = m_videoPacketQueue.front();
            m_videoPacketQueue.pop();
            av_packet_free(&pkt);
        }
    }

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
    m_isAudioOnly = false;
    m_isLoaded = false;
}

bool FFmpegPlayer::LoadFile(const wxString& path)
{
    FFMPEGLOG("LoadFile: " << path.ToStdString());

    // Clean up any previous state
    CleanupDecoder();
    m_filePath = path;
    m_currentFrame = 0;
    m_currentTime = 0.0;
    m_isAudioOnly = false;
    m_hasAudio = false;
    m_hitEOF = false;
    m_audioBytesPlayed = 0;

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

    // Find video and audio streams
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
    for (unsigned int i = 0; i < m_formatCtx->nb_streams; i++) {
        if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && m_videoStreamIndex < 0) {
            m_videoStreamIndex = i;
        } else if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && m_audioStreamIndex < 0) {
            m_audioStreamIndex = i;
        }
    }

    // Calculate duration from format context
    if (m_formatCtx->duration > 0) {
        m_duration = (double)m_formatCtx->duration / AV_TIME_BASE;
    } else {
        m_duration = 0.0;
    }

    // Allocate packet (needed for both video and audio)
    m_packet = av_packet_alloc();
    if (!m_packet) {
        FFMPEGLOG("Failed to allocate packet");
        CleanupDecoder();
        return false;
    }

    // Initialize video decoder if video stream exists
    if (m_videoStreamIndex >= 0) {
        AVStream* videoStream = m_formatCtx->streams[m_videoStreamIndex];
        AVCodecParameters* codecParams = videoStream->codecpar;

        const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
        if (!codec) {
            FFMPEGLOG("Unsupported video codec");
            CleanupDecoder();
            return false;
        }

        m_codecCtx = avcodec_alloc_context3(codec);
        if (!m_codecCtx) {
            FFMPEGLOG("Failed to allocate video codec context");
            CleanupDecoder();
            return false;
        }

        ret = avcodec_parameters_to_context(m_codecCtx, codecParams);
        if (ret < 0) {
            FFMPEGLOG("Failed to copy video codec parameters");
            CleanupDecoder();
            return false;
        }

        ret = avcodec_open2(m_codecCtx, codec, nullptr);
        if (ret < 0) {
            FFMPEGLOG("Failed to open video codec");
            CleanupDecoder();
            return false;
        }

        m_width = m_codecCtx->width;
        m_height = m_codecCtx->height;

        if (videoStream->avg_frame_rate.den > 0 && videoStream->avg_frame_rate.num > 0) {
            m_frameRate = av_q2d(videoStream->avg_frame_rate);
        } else if (videoStream->r_frame_rate.den > 0 && videoStream->r_frame_rate.num > 0) {
            m_frameRate = av_q2d(videoStream->r_frame_rate);
        } else {
            m_frameRate = 30.0;
        }

        if (m_frameRate < 1.0) m_frameRate = 1.0;
        if (m_frameRate > 120.0) m_frameRate = 120.0;

        FFMPEGLOG("Video: " << m_width << "x" << m_height << " @ " << m_frameRate << " fps");

        m_frame = av_frame_alloc();
        m_frameRGB = av_frame_alloc();
        if (!m_frame || !m_frameRGB) {
            FFMPEGLOG("Failed to allocate video frames");
            CleanupDecoder();
            return false;
        }

        int outWidth = m_renderWidth > 0 ? m_renderWidth : m_width;
        int outHeight = m_renderHeight > 0 ? m_renderHeight : m_height;

        m_rgbBufferSize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, outWidth, outHeight, 1);
        m_rgbBuffer = (uint8_t*)av_malloc(m_rgbBufferSize);
        if (!m_rgbBuffer) {
            FFMPEGLOG("Failed to allocate RGB buffer");
            CleanupDecoder();
            return false;
        }

        av_image_fill_arrays(m_frameRGB->data, m_frameRGB->linesize, m_rgbBuffer,
                             AV_PIX_FMT_RGB24, outWidth, outHeight, 1);

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
    } else {
        // No video stream - this is audio only
        m_isAudioOnly = true;
        FFMPEGLOG("Audio-only file detected");
    }

    // Initialize audio decoder if audio stream exists
    if (m_audioStreamIndex >= 0) {
        if (!InitAudioDecoder()) {
            FFMPEGLOG("Failed to initialize audio decoder (continuing without audio)");
            // Don't fail - just continue without audio
        }
    }

    m_isLoaded = true;

    // For video files, decode the first frame for display
    if (!m_isAudioOnly) {
        if (!DecodeNextFrame()) {
            FFMPEGLOG("Failed to decode first frame");
            CleanupDecoder();
            return false;
        }
    }

    FFMPEGLOG("Loaded: duration=" << m_duration << "s, hasVideo=" << !m_isAudioOnly << ", hasAudio=" << m_hasAudio);

    return true;
}

bool FFmpegPlayer::InitAudioDecoder()
{
    if (m_audioStreamIndex < 0 || !m_formatCtx) {
        return false;
    }

    AVStream* audioStream = m_formatCtx->streams[m_audioStreamIndex];
    AVCodecParameters* codecParams = audioStream->codecpar;

    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        FFMPEGLOG("Unsupported audio codec");
        return false;
    }

    m_audioCodecCtx = avcodec_alloc_context3(codec);
    if (!m_audioCodecCtx) {
        FFMPEGLOG("Failed to allocate audio codec context");
        return false;
    }

    int ret = avcodec_parameters_to_context(m_audioCodecCtx, codecParams);
    if (ret < 0) {
        FFMPEGLOG("Failed to copy audio codec parameters");
        avcodec_free_context(&m_audioCodecCtx);
        return false;
    }

    ret = avcodec_open2(m_audioCodecCtx, codec, nullptr);
    if (ret < 0) {
        FFMPEGLOG("Failed to open audio codec");
        avcodec_free_context(&m_audioCodecCtx);
        return false;
    }

    m_audioFrame = av_frame_alloc();
    if (!m_audioFrame) {
        FFMPEGLOG("Failed to allocate audio frame");
        avcodec_free_context(&m_audioCodecCtx);
        return false;
    }

    // Set up resampler to convert to S16 stereo 48kHz (SDL2 friendly format)
    m_swrCtx = swr_alloc();
    if (!m_swrCtx) {
        FFMPEGLOG("Failed to allocate resampler");
        av_frame_free(&m_audioFrame);
        avcodec_free_context(&m_audioCodecCtx);
        return false;
    }

    // Configure resampler
    av_opt_set_chlayout(m_swrCtx, "in_chlayout", &m_audioCodecCtx->ch_layout, 0);
    av_opt_set_int(m_swrCtx, "in_sample_rate", m_audioCodecCtx->sample_rate, 0);
    av_opt_set_sample_fmt(m_swrCtx, "in_sample_fmt", m_audioCodecCtx->sample_fmt, 0);

    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    av_opt_set_chlayout(m_swrCtx, "out_chlayout", &outLayout, 0);
    av_opt_set_int(m_swrCtx, "out_sample_rate", 48000, 0);
    av_opt_set_sample_fmt(m_swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    ret = swr_init(m_swrCtx);
    if (ret < 0) {
        FFMPEGLOG("Failed to initialize resampler");
        swr_free(&m_swrCtx);
        av_frame_free(&m_audioFrame);
        avcodec_free_context(&m_audioCodecCtx);
        return false;
    }

    // Initialize SDL audio
    if (!InitSDLAudio()) {
        FFMPEGLOG("Failed to initialize SDL audio");
        swr_free(&m_swrCtx);
        av_frame_free(&m_audioFrame);
        avcodec_free_context(&m_audioCodecCtx);
        return false;
    }

    m_hasAudio = true;
    FFMPEGLOG("Audio initialized: " << m_audioCodecCtx->sample_rate << "Hz, "
              << m_audioCodecCtx->ch_layout.nb_channels << " channels");

    return true;
}

bool FFmpegPlayer::InitSDLAudio()
{
#ifdef HAVE_SDL2
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        FFMPEGLOG("Failed to initialize SDL audio: " << SDL_GetError());
        return false;
    }
    m_sdlAudioInitialized = true;

    SDL_AudioSpec wanted, obtained;
    SDL_zero(wanted);
    wanted.freq = 48000;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 2;
    wanted.samples = 4096;
    wanted.callback = AudioCallback;
    wanted.userdata = this;

    m_sdlAudioDeviceId = SDL_OpenAudioDevice(nullptr, 0, &wanted, &obtained, 0);
    if (m_sdlAudioDeviceId == 0) {
        FFMPEGLOG("Failed to open SDL audio device: " << SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_sdlAudioInitialized = false;
        return false;
    }

    FFMPEGLOG("SDL audio opened: " << obtained.freq << "Hz, " << (int)obtained.channels << " channels");
    return true;
#else
    FFMPEGLOG("SDL2 not available - audio playback disabled");
    return false;
#endif
}

void FFmpegPlayer::AudioCallback(void* userdata, uint8_t* stream, int len)
{
#ifdef HAVE_SDL2
    FFmpegPlayer* player = static_cast<FFmpegPlayer*>(userdata);
    if (!player || player->m_muted || !player->m_isPlaying) {
        memset(stream, 0, len);
        return;
    }

    std::lock_guard<std::mutex> lock(player->m_audioMutex);

    size_t readPos = player->m_audioBufferReadPos.load();
    size_t writePos = player->m_audioBufferWritePos.load();
    size_t available = (writePos >= readPos) ? (writePos - readPos) : (AUDIO_BUFFER_SIZE - readPos + writePos);

    size_t toCopy = std::min(available, static_cast<size_t>(len));

    if (toCopy > 0) {
        size_t firstPart = std::min(toCopy, AUDIO_BUFFER_SIZE - readPos);
        memcpy(stream, player->m_audioBuffer.data() + readPos, firstPart);
        if (toCopy > firstPart) {
            memcpy(stream + firstPart, player->m_audioBuffer.data(), toCopy - firstPart);
        }
        player->m_audioBufferReadPos = (readPos + toCopy) % AUDIO_BUFFER_SIZE;
        player->m_audioBytesPlayed += toCopy;
    }

    // Fill remaining with silence
    if (toCopy < static_cast<size_t>(len)) {
        memset(stream + toCopy, 0, len - toCopy);
    }

    // Apply volume
    if (player->m_volume < 1.0) {
        int16_t* samples = reinterpret_cast<int16_t*>(stream);
        int numSamples = len / 2;
        for (int i = 0; i < numSamples; i++) {
            samples[i] = static_cast<int16_t>(samples[i] * player->m_volume);
        }
    }
#endif
}

void FFmpegPlayer::SeekToStart()
{
    if (!m_formatCtx) return;
    
    int streamIndex = m_videoStreamIndex >= 0 ? m_videoStreamIndex : m_audioStreamIndex;
    if (streamIndex < 0) return;

    // Seek to the beginning of the stream
    av_seek_frame(m_formatCtx, streamIndex, 0, AVSEEK_FLAG_BACKWARD);

    // Flush the codec buffers
    if (m_codecCtx) {
        avcodec_flush_buffers(m_codecCtx);
    }
    if (m_audioCodecCtx) {
        avcodec_flush_buffers(m_audioCodecCtx);
    }

    // Clear packet queues
    {
        std::lock_guard<std::mutex> lock(m_videoPacketMutex);
        while (!m_videoPacketQueue.empty()) {
            AVPacket* pkt = m_videoPacketQueue.front();
            m_videoPacketQueue.pop();
            av_packet_free(&pkt);
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_audioPacketMutex);
        while (!m_audioPacketQueue.empty()) {
            AVPacket* pkt = m_audioPacketQueue.front();
            m_audioPacketQueue.pop();
            av_packet_free(&pkt);
        }
    }

    m_currentFrame = 0;
    m_currentTime = 0.0;
    m_hitEOF = false;
    m_audioBytesPlayed = 0;
}

void FFmpegPlayer::ReadAndRoutePackets()
{
    if (!m_formatCtx || !m_packet) return;

    // Check if queues are already full enough
    size_t videoQueueSize, audioQueueSize;
    {
        std::lock_guard<std::mutex> lock(m_videoPacketMutex);
        videoQueueSize = m_videoPacketQueue.size();
    }
    {
        std::lock_guard<std::mutex> lock(m_audioPacketMutex);
        audioQueueSize = m_audioPacketQueue.size();
    }

    // Read packets until both queues have enough data
    int packetsRead = 0;
    const int maxPacketsPerCall = 32;

    while (packetsRead < maxPacketsPerCall) {
        // Check if we have enough packets
        bool videoNeedsMore = (m_videoStreamIndex >= 0 && videoQueueSize < MAX_PACKET_QUEUE_SIZE);
        bool audioNeedsMore = (m_audioStreamIndex >= 0 && audioQueueSize < MAX_PACKET_QUEUE_SIZE);
        
        if (!videoNeedsMore && !audioNeedsMore) {
            break;
        }

        int ret = av_read_frame(m_formatCtx, m_packet);
        if (ret < 0) {
            // EOF or error - don't handle looping here, let DecodeNextFrame handle it
            // when the queue is actually empty
            m_hitEOF = true;
            break;
        }

        packetsRead++;

        // Route packet to appropriate queue
        if (m_packet->stream_index == m_videoStreamIndex) {
            AVPacket* pkt = av_packet_clone(m_packet);
            if (pkt) {
                std::lock_guard<std::mutex> lock(m_videoPacketMutex);
                m_videoPacketQueue.push(pkt);
                videoQueueSize++;
            }
        } else if (m_packet->stream_index == m_audioStreamIndex) {
            AVPacket* pkt = av_packet_clone(m_packet);
            if (pkt) {
                std::lock_guard<std::mutex> lock(m_audioPacketMutex);
                m_audioPacketQueue.push(pkt);
                audioQueueSize++;
            }
        }

        av_packet_unref(m_packet);
    }
}

void FFmpegPlayer::Seek(double timeSeconds)
{
    if (!m_formatCtx) return;
    
    // Use video stream if available, otherwise audio stream
    int streamIndex = m_videoStreamIndex >= 0 ? m_videoStreamIndex : m_audioStreamIndex;
    if (streamIndex < 0) return;

    // Convert time to stream timebase
    AVStream* stream = m_formatCtx->streams[streamIndex];
    int64_t timestamp = static_cast<int64_t>(timeSeconds * stream->time_base.den / stream->time_base.num);

    av_seek_frame(m_formatCtx, streamIndex, timestamp, AVSEEK_FLAG_BACKWARD);

    // Flush the codec buffers
    if (m_codecCtx) {
        avcodec_flush_buffers(m_codecCtx);
    }
    if (m_audioCodecCtx) {
        avcodec_flush_buffers(m_audioCodecCtx);
    }

    // Clear packet queues
    {
        std::lock_guard<std::mutex> lock(m_videoPacketMutex);
        while (!m_videoPacketQueue.empty()) {
            AVPacket* pkt = m_videoPacketQueue.front();
            m_videoPacketQueue.pop();
            av_packet_free(&pkt);
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_audioPacketMutex);
        while (!m_audioPacketQueue.empty()) {
            AVPacket* pkt = m_audioPacketQueue.front();
            m_audioPacketQueue.pop();
            av_packet_free(&pkt);
        }
    }

    // Reset EOF flag so we can read packets again
    m_hitEOF = false;

    // Clear audio buffer so we start fresh
    m_audioBufferReadPos = 0;
    m_audioBufferWritePos = 0;
    
    // Reset bytes played counter based on seek position
    // 48000 Hz * 2 channels * 2 bytes per sample = 192000 bytes per second
    m_audioBytesPlayed = static_cast<size_t>(timeSeconds * 192000.0);

    m_currentTime = timeSeconds;
    m_currentFrame = static_cast<size_t>(timeSeconds * m_frameRate);
}

bool FFmpegPlayer::DecodeNextFrame()
{
    if (!m_formatCtx || !m_codecCtx || !m_frame) {
        return false;
    }

    // First, try to receive a frame from already-sent packets
    int ret = avcodec_receive_frame(m_codecCtx, m_frame);
    if (ret == 0) {
        goto frame_ready;
    }

    // Need more packets - read and route them
    while (true) {
        // Ensure packets are available in the queue
        ReadAndRoutePackets();

        // Get a video packet from the queue
        AVPacket* pkt = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_videoPacketMutex);
            if (!m_videoPacketQueue.empty()) {
                pkt = m_videoPacketQueue.front();
                m_videoPacketQueue.pop();
            }
        }

        if (!pkt) {
            // No more packets available - check if we hit EOF and should loop
            if (m_hitEOF && m_loop) {
                // Seek back to start for looping
                m_hitEOF = false;
                av_seek_frame(m_formatCtx, m_videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(m_codecCtx);
                if (m_audioCodecCtx) {
                    avcodec_flush_buffers(m_audioCodecCtx);
                }
                // Clear queues since we're starting fresh
                {
                    std::lock_guard<std::mutex> lock(m_videoPacketMutex);
                    while (!m_videoPacketQueue.empty()) {
                        AVPacket* p = m_videoPacketQueue.front();
                        m_videoPacketQueue.pop();
                        av_packet_free(&p);
                    }
                }
                {
                    std::lock_guard<std::mutex> lock(m_audioPacketMutex);
                    while (!m_audioPacketQueue.empty()) {
                        AVPacket* p = m_audioPacketQueue.front();
                        m_audioPacketQueue.pop();
                        av_packet_free(&p);
                    }
                }
                m_currentFrame = 0;
                m_currentTime = 0.0;
                ReadAndRoutePackets();
                continue;
            }
            return false;
        }

        // Send packet to decoder
        ret = avcodec_send_packet(m_codecCtx, pkt);
        av_packet_free(&pkt);

        if (ret < 0 && ret != AVERROR(EAGAIN)) {
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

        goto frame_ready;
    }

frame_ready:
    // Successfully decoded a frame
    m_currentFrame++;

    // Update current time based on frame PTS
    if (m_frame->pts != AV_NOPTS_VALUE && m_formatCtx) {
        AVStream* stream = m_formatCtx->streams[m_videoStreamIndex];
        m_currentTime = m_frame->pts * av_q2d(stream->time_base);
    } else if (m_frameRate > 0) {
        m_currentTime = m_currentFrame / m_frameRate;
    }

    // Convert frame to bitmap
    m_currentBitmap = ConvertFrameToBitmap();

    return true;
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

    // For audio-only files, just fill the audio buffer
    if (m_isAudioOnly) {
        FillAudioBuffer();

        // Check if we've reached the end
        if (m_duration > 0 && m_currentTime >= m_duration) {
            if (!m_loop) {
                m_isPlaying = false;
                return false;
            }
        }
        return m_isPlaying;
    }

    // For video files, decode the next video frame
    bool hasFrame = DecodeNextFrame();

    if (hasFrame && m_frameCallback) {
        m_frameCallback(m_currentBitmap);
    }

    // Also keep audio buffer filled for video+audio files
    if (m_hasAudio) {
        FillAudioBuffer();
    }

    return hasFrame;
}

void FFmpegPlayer::Play()
{
    if (!m_isLoaded) return;

    m_isPlaying = true;

#ifdef HAVE_SDL2
    // Start SDL audio playback
    if (m_hasAudio && m_sdlAudioDeviceId > 0) {
        SDL_PauseAudioDevice(m_sdlAudioDeviceId, 0);  // 0 = unpause

        // Pre-fill audio buffer for audio-only files
        if (m_isAudioOnly) {
            FillAudioBuffer();
        }
    }
#endif

    FFMPEGLOG("Play started");
}

void FFmpegPlayer::Stop()
{
    m_isPlaying = false;

#ifdef HAVE_SDL2
    // Stop SDL audio playback
    if (m_sdlAudioDeviceId > 0) {
        SDL_PauseAudioDevice(m_sdlAudioDeviceId, 1);  // 1 = pause
    }
#endif

    if (m_isLoaded) {
        SeekToStart();
    }

    // Clear audio buffer
    m_audioBufferReadPos = 0;
    m_audioBufferWritePos = 0;

    FFMPEGLOG("Stop");
}

void FFmpegPlayer::Pause()
{
    m_isPlaying = false;

#ifdef HAVE_SDL2
    // Pause SDL audio playback
    if (m_sdlAudioDeviceId > 0) {
        SDL_PauseAudioDevice(m_sdlAudioDeviceId, 1);  // 1 = pause
    }
#endif

    FFMPEGLOG("Pause");
}

void FFmpegPlayer::FillAudioBuffer()
{
    if (!m_hasAudio || !m_formatCtx || !m_audioCodecCtx || !m_swrCtx) {
        return;
    }

    // Ensure packets are available
    ReadAndRoutePackets();

    // Fill buffer with decoded audio data
    while (m_isPlaying) {
        size_t writePos = m_audioBufferWritePos.load();
        size_t readPos = m_audioBufferReadPos.load();
        size_t available = (writePos >= readPos) ?
            (AUDIO_BUFFER_SIZE - (writePos - readPos) - 1) :
            (readPos - writePos - 1);

        // Stop if buffer is getting full
        if (available < 8192) {
            break;
        }

        // Get an audio packet from the queue
        AVPacket* pkt = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_audioPacketMutex);
            if (!m_audioPacketQueue.empty()) {
                pkt = m_audioPacketQueue.front();
                m_audioPacketQueue.pop();
            }
        }

        if (!pkt) {
            // Try to read more packets
            ReadAndRoutePackets();
            {
                std::lock_guard<std::mutex> lock(m_audioPacketMutex);
                if (!m_audioPacketQueue.empty()) {
                    pkt = m_audioPacketQueue.front();
                    m_audioPacketQueue.pop();
                }
            }
            if (!pkt) {
                // Still no packets - we're at EOF or starving
                break;
            }
        }

        // Update current time based on packet PTS (only for audio-only files)
        if (m_isAudioOnly && pkt->pts != AV_NOPTS_VALUE) {
            AVStream* stream = m_formatCtx->streams[m_audioStreamIndex];
            m_currentTime = pkt->pts * av_q2d(stream->time_base);
        }

        // Send packet to decoder
        int ret = avcodec_send_packet(m_audioCodecCtx, pkt);
        av_packet_free(&pkt);

        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            continue;
        }

        // Receive frames from decoder
        while (true) {
            ret = avcodec_receive_frame(m_audioCodecCtx, m_audioFrame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                break;
            }

            // Resample audio to output format
            int outSamples = av_rescale_rnd(
                swr_get_delay(m_swrCtx, m_audioCodecCtx->sample_rate) + m_audioFrame->nb_samples,
                48000, m_audioCodecCtx->sample_rate, AV_ROUND_UP);

            // Temporary buffer for resampled audio
            uint8_t* outBuffer = nullptr;
            int outBufferSize = av_samples_alloc(&outBuffer, nullptr, 2, outSamples, AV_SAMPLE_FMT_S16, 0);

            if (outBufferSize < 0) {
                av_frame_unref(m_audioFrame);
                continue;
            }

            int convertedSamples = swr_convert(m_swrCtx, &outBuffer, outSamples,
                (const uint8_t**)m_audioFrame->data, m_audioFrame->nb_samples);

            if (convertedSamples > 0) {
                int bytesToWrite = convertedSamples * 2 * 2;  // stereo, 16-bit

                std::lock_guard<std::mutex> lock(m_audioMutex);

                writePos = m_audioBufferWritePos.load();  // Re-read in case it changed
                for (int i = 0; i < bytesToWrite; i++) {
                    m_audioBuffer[(writePos + i) % AUDIO_BUFFER_SIZE] = outBuffer[i];
                }
                m_audioBufferWritePos = (writePos + bytesToWrite) % AUDIO_BUFFER_SIZE;
            }

            av_freep(&outBuffer);
            av_frame_unref(m_audioFrame);
        }
    }
}

bool FFmpegPlayer::DecodeAudioFrame()
{
    if (!m_hasAudio) return false;

    // For audio-only files, fill the buffer
    FillAudioBuffer();
    return m_isPlaying;
}
