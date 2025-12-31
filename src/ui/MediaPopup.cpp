#include "MediaPopup.h"
#include "FFmpegPlayer.h"
#include "LottiePlayer.h"
#include "FileUtils.h"
#include <iostream>
#include <thread>
#include <wx/display.h>
#include <wx/filename.h>
#include <wx/settings.h>

wxDEFINE_EVENT(wxEVT_IMAGE_LOADED, wxThreadEvent);

#define MPLOG(msg) std::cerr << "[MediaPopup] " << msg << std::endl

// Helper to check if file extension is a supported image format
static bool IsSupportedImageFormat(const wxString &path) {
  wxFileName fn(path);
  wxString ext = fn.GetExt().Lower();

  if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "bmp" ||
      ext == "ico" || ext == "tiff" || ext == "tif") {
    return true;
  }

  if (ext == "webp") {
    return HasWebPSupport();
  }

  return false;
}

// Helper to check if file extension is a video/animation format
static bool IsVideoFormat(const wxString &path) {
  wxFileName fn(path);
  wxString ext = fn.GetExt().Lower();

  if (ext == "mp4" || ext == "webm" || ext == "avi" || ext == "mov" ||
      ext == "mkv" || ext == "gif" || ext == "m4v" || ext == "ogv") {
    return true;
  }

  return false;
}

// Helper to check if file is a Lottie/TGS animation
static bool IsLottieFormat(const wxString &path) {
  wxFileName fn(path);
  wxString ext = fn.GetExt().Lower();
  return (ext == "tgs" || ext == "json");
}

wxBEGIN_EVENT_TABLE(MediaPopup, wxPopupWindow)
  EVT_PAINT(MediaPopup::OnPaint)
wxEND_EVENT_TABLE()

MediaPopup::MediaPopup(wxWindow *parent)
    : wxPopupWindow(parent, wxBORDER_NONE),
      m_hasImage(false),
      m_isLoading(false),
      m_isDownloadingMedia(false),
      m_hasError(false),
      m_loadingTimer(this, LOADING_TIMER_ID),
      m_loadingFrame(0),
      m_isPlayingFFmpeg(false),
      m_videoLoadPending(false),
      m_ffmpegAnimTimer(this, FFMPEG_ANIM_TIMER_ID),
      m_loopVideo(false),
      m_videoMuted(true),
      m_isPlayingLottie(false),
      m_lottieAnimTimer(this, LOTTIE_ANIM_TIMER_ID),
      m_isPlayingVoice(false),
      m_voiceProgress(0.0),
      m_voiceDuration(0.0),
      m_voiceProgressTimer(this, VOICE_PROGRESS_TIMER_ID),
      m_asyncLoadTimer(this, ASYNC_LOAD_TIMER_ID),
      m_asyncLoadPending(false),
      m_parentBottom(-1) {
  SetCursor(wxCursor(wxCURSOR_HAND));

  ApplyHexChatStyle();
  SetSize(MIN_WIDTH, MIN_HEIGHT);
  SetMinSize(wxSize(MIN_WIDTH, MIN_HEIGHT));

  Bind(wxEVT_TIMER, &MediaPopup::OnLoadingTimer, this, LOADING_TIMER_ID);
  Bind(wxEVT_TIMER, &MediaPopup::OnFFmpegAnimTimer, this, FFMPEG_ANIM_TIMER_ID);
  Bind(wxEVT_TIMER, &MediaPopup::OnLottieAnimTimer, this, LOTTIE_ANIM_TIMER_ID);
  Bind(wxEVT_TIMER, &MediaPopup::OnAsyncLoadTimer, this, ASYNC_LOAD_TIMER_ID);
  Bind(wxEVT_TIMER, &MediaPopup::OnVoiceProgressTimer, this, VOICE_PROGRESS_TIMER_ID);
  Bind(wxEVT_IMAGE_LOADED, &MediaPopup::OnImageLoaded, this);
  Bind(wxEVT_LEFT_DOWN, &MediaPopup::OnLeftDown, this);
}

MediaPopup::~MediaPopup() {
  StopAllPlayback();
  m_loadingTimer.Stop();
  m_asyncLoadTimer.Stop();
  m_ffmpegAnimTimer.Stop();
  m_lottieAnimTimer.Stop();
  m_voiceProgressTimer.Stop();
  m_ffmpegPlayer.reset();
  m_lottiePlayer.reset();
  ClearFailedLoads();
}

void MediaPopup::StopAllPlayback() {
  MPLOG("StopAllPlayback called, m_isPlayingFFmpeg=" << m_isPlayingFFmpeg 
        << " m_isPlayingLottie=" << m_isPlayingLottie
        << " m_isPlayingVoice=" << m_isPlayingVoice
        << " m_videoLoadPending=" << m_videoLoadPending);
  
  m_ffmpegAnimTimer.Stop();
  m_lottieAnimTimer.Stop();
  m_loadingTimer.Stop();
  m_asyncLoadTimer.Stop();
  m_voiceProgressTimer.Stop();

  if (m_ffmpegPlayer) {
    m_ffmpegPlayer->Stop();
  }
  if (m_lottiePlayer) {
    m_lottiePlayer->Stop();
  }
  m_isPlayingFFmpeg = false;
  m_isPlayingLottie = false;
  m_isPlayingVoice = false;
  m_voiceProgress = 0.0;
  m_videoLoadPending = false;
  m_currentVoicePath.Clear();
  m_lottiePath.Clear();

  m_isLoading = false;
  m_isDownloadingMedia = false;
  m_asyncLoadPending = false;
  m_pendingImagePath.Clear();
}

void MediaPopup::ApplyHexChatStyle() {
  m_bgColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
  // Use a more visible border - darker than the window text for contrast
  m_borderColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
  m_textColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
  m_labelColor = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);

  SetBackgroundColour(m_bgColor);
}

bool MediaPopup::IsSameMedia(const MediaInfo &a, const MediaInfo &b) const {
  if (a.type != b.type)
    return false;

  if (a.fileId != 0 && b.fileId != 0) {
    return a.fileId == b.fileId;
  }
  if (!a.localPath.IsEmpty() && !b.localPath.IsEmpty()) {
    return a.localPath == b.localPath;
  }
  if (a.thumbnailFileId != 0 && b.thumbnailFileId != 0) {
    return a.thumbnailFileId == b.thumbnailFileId;
  }
  return false;
}

void MediaPopup::ShowMedia(const MediaInfo &info, const wxPoint &pos) {
  MPLOG("ShowMedia called: fileId=" << info.fileId
        << " type=" << static_cast<int>(info.type)
        << " localPath=" << info.localPath.ToStdString()
        << " thumbnailPath=" << info.thumbnailPath.ToStdString()
        << " isDownloading=" << info.isDownloading);

  m_originalPosition = pos;

  bool isSameFile = (m_mediaInfo.fileId != 0 && m_mediaInfo.fileId == info.fileId);
  
  // If already playing or loading the same file, don't interrupt playback
  if (isSameFile && (m_isPlayingFFmpeg || m_isPlayingVoice || m_videoLoadPending)) {
    MPLOG("ShowMedia: already playing/loading same file, not interrupting");
    AdjustPositionToScreen(pos);
    return;
  }

  if (IsShown() && IsSameMedia(m_mediaInfo, info)) {
    bool localPathChanged =
        (m_mediaInfo.localPath != info.localPath && !info.localPath.IsEmpty() &&
         wxFileExists(info.localPath));
    bool thumbnailPathChanged =
        (m_mediaInfo.thumbnailPath != info.thumbnailPath &&
         !info.thumbnailPath.IsEmpty() && wxFileExists(info.thumbnailPath));

    if (!localPathChanged && !thumbnailPathChanged) {
      AdjustPositionToScreen(pos);
      return;
    }
  }

  bool hadImage = m_hasImage;

  // Only stop playback if switching to a different file
  if (!isSameFile) {
    StopAllPlayback();
  }

  m_mediaInfo = info;
  m_hasError = false;
  m_errorMessage.Clear();
  m_hasImage = isSameFile ? hadImage : false;
  m_isDownloadingMedia = false;

  bool hasLocalFile = !info.localPath.IsEmpty() && wxFileExists(info.localPath);

  // Handle voice notes specially - show waveform and play audio
  if (info.type == MediaType::Voice) {
    // Decode waveform for visualization
    m_decodedWaveform = DecodeWaveform(info.waveform, 40);
    m_voiceDuration = info.duration > 0 ? info.duration : 0.0;
    m_voiceProgress = 0.0;
    MPLOG("Voice note: duration=" << info.duration << " m_voiceDuration=" << m_voiceDuration);
    
    // Set up the popup size for voice note display
    ApplySizeAndPosition(VOICE_WIDTH, VOICE_HEIGHT);
    
    if (hasLocalFile) {
      // Don't auto-play - wait for user to click play button
      m_isPlayingVoice = false;
      Refresh();
    } else {
      // Show loading state while downloading
      m_isLoading = true;
      m_loadingFrame = 0;
      m_loadingTimer.Start(150);
      Refresh();
    }
    return;
  }

  // Handle Lottie/TGS animations (stickers)
  bool isLottieFile = hasLocalFile && IsLottieFormat(info.localPath);
  if (isLottieFile) {
    MPLOG("ShowMedia: dispatching to PlayLottie");
    PlayLottie(info.localPath, true);
    return;
  }

  // Handle video/animation formats with FFmpeg
  bool isVideoFile = hasLocalFile && IsVideoFormat(info.localPath);
  bool isImageFile = hasLocalFile && IsSupportedImageFormat(info.localPath);
  
  if (isVideoFile) {
    bool shouldLoop = (info.type == MediaType::GIF || 
                       info.type == MediaType::Sticker ||
                       info.type == MediaType::VideoNote);
    PlayVideo(info.localPath, shouldLoop, true);
    return;
  }

  // Handle static images
  if (isImageFile) {
    m_isLoading = true;
    m_loadingFrame = 0;
    m_loadingTimer.Start(150);
    ApplySizeAndPosition(MIN_WIDTH, MIN_HEIGHT);
    LoadImageAsync(info.localPath);
    Refresh();
    return;
  }

  // Fall back to thumbnail
  if (!info.thumbnailPath.IsEmpty() && wxFileExists(info.thumbnailPath)) {
    FallbackToThumbnail();
    return;
  }

  // Show loading/placeholder if file needs download
  if (info.fileId != 0 && (!hasLocalFile)) {
    m_isLoading = true;
    m_loadingFrame = 0;
    m_loadingTimer.Start(150);
    int width = (info.type == MediaType::Sticker) ? 180 : PHOTO_MAX_WIDTH;
    int height = (info.type == MediaType::Sticker) ? 120 : PHOTO_MAX_HEIGHT;
    ApplySizeAndPosition(width, height);
    Refresh();
    return;
  }

  // Show placeholder
  m_hasImage = false;
  int width = (info.type == MediaType::Sticker) ? 200 : PHOTO_MAX_WIDTH;
  int height = (info.type == MediaType::Sticker) ? 150 : PHOTO_MAX_HEIGHT;
  ApplySizeAndPosition(width, height);
  Refresh();
}

void MediaPopup::PlayVideo(const wxString &path, bool loop, bool muted) {
  MPLOG("PlayVideo: path=" << path.ToStdString() << " loop=" << loop << " muted=" << muted);

  if (HasFailedRecently(path)) {
    MPLOG("PlayVideo: skipping recently failed file");
    FallbackToThumbnail();
    return;
  }

  // Don't restart if already playing or loading the same file
  if ((m_isPlayingFFmpeg || m_videoLoadPending) && m_videoPath == path) {
    MPLOG("PlayVideo: already playing/loading same file, not restarting");
    return;
  }

  StopAllPlayback();

  m_videoPath = path;
  m_loopVideo = loop;
  m_videoMuted = muted;
  m_videoLoadPending = true;  // Mark that we're loading a video

  // Show loading state while initializing
  m_isLoading = true;
  m_loadingFrame = 0;
  m_loadingTimer.Start(150);
  ApplySizeAndPosition(MIN_WIDTH, MIN_HEIGHT);
  Refresh();

  // Use CallAfter to prevent UI freeze during FFmpeg initialization
  CallAfter([this, path, loop, muted]() {
    PlayMediaWithFFmpeg(path, loop, muted);
  });
}

void MediaPopup::PlayMediaWithFFmpeg(const wxString &path, bool loop, bool muted) {
  MPLOG("PlayMediaWithFFmpeg: " << path.ToStdString());

  m_isLoading = false;
  m_loadingTimer.Stop();
  m_videoLoadPending = false;  // No longer pending, we're actually loading now

  if (HasFailedRecently(path)) {
    FallbackToThumbnail();
    return;
  }

  if (!m_ffmpegPlayer) {
    m_ffmpegPlayer = std::make_unique<FFmpegPlayer>();
  }

  // Determine max size based on media type
  int maxWidth, maxHeight;
  if (m_mediaInfo.type == MediaType::Sticker) {
    maxWidth = STICKER_MAX_WIDTH - PADDING * 2;
    maxHeight = STICKER_MAX_HEIGHT - PADDING * 2 - 20;
  } else {
    maxWidth = PHOTO_MAX_WIDTH - PADDING * 2 - BORDER_WIDTH * 2;
    maxHeight = PHOTO_MAX_HEIGHT - PADDING * 2 - BORDER_WIDTH * 2 - 24;
  }

  m_ffmpegPlayer->SetRenderSize(maxWidth, maxHeight);
  m_ffmpegPlayer->SetLoop(loop);
  m_ffmpegPlayer->SetMuted(muted);

  m_ffmpegPlayer->SetFrameCallback(
      [this](const wxBitmap &frame) { OnFFmpegFrame(frame); });

  if (!m_ffmpegPlayer->LoadFile(path)) {
    MPLOG("PlayMediaWithFFmpeg: failed to load: " << path.ToStdString());
    MarkLoadFailed(path);
    m_ffmpegPlayer.reset();
    FallbackToThumbnail();
    return;
  }

  // Get actual dimensions and scale
  int vidWidth = m_ffmpegPlayer->GetWidth();
  int vidHeight = m_ffmpegPlayer->GetHeight();

  if (vidWidth > 0 && vidHeight > 0) {
    double scaleX = (double)maxWidth / vidWidth;
    double scaleY = (double)maxHeight / vidHeight;
    double scale = std::min(scaleX, scaleY);

    int scaledWidth = (int)(vidWidth * scale);
    int scaledHeight = (int)(vidHeight * scale);

    m_ffmpegPlayer->SetRenderSize(scaledWidth, scaledHeight);

    int popupWidth = scaledWidth + PADDING * 2 + BORDER_WIDTH * 2;
    int popupHeight = scaledHeight + PADDING * 2 + BORDER_WIDTH * 2 + 24;
    ApplySizeAndPosition(popupWidth, popupHeight);
  } else {
    int defaultWidth = (m_mediaInfo.type == MediaType::Sticker) ? STICKER_MAX_WIDTH : PHOTO_MAX_WIDTH;
    int defaultHeight = (m_mediaInfo.type == MediaType::Sticker) ? STICKER_MAX_HEIGHT : PHOTO_MAX_HEIGHT;
    ApplySizeAndPosition(defaultWidth, defaultHeight);
  }

  m_ffmpegPlayer->Play();
  m_isPlayingFFmpeg = true;
  m_hasImage = true;

  int interval = m_ffmpegPlayer->GetTimerIntervalMs();
  m_ffmpegAnimTimer.Start(interval);

  Refresh();
  MPLOG("PlayMediaWithFFmpeg: playback started, interval=" << interval << "ms");
}

void MediaPopup::StopVideo() {
  MPLOG("StopVideo called");
  m_loadingTimer.Stop();

  if (m_ffmpegPlayer) {
    m_ffmpegPlayer->Stop();
  }
  m_isPlayingFFmpeg = false;
  m_isLoading = false;
  m_videoPath.Clear();
}

void MediaPopup::OnFFmpegAnimTimer(wxTimerEvent &event) {
  if (!m_ffmpegPlayer || !m_isPlayingFFmpeg) {
    m_ffmpegAnimTimer.Stop();
    return;
  }

  if (!m_ffmpegPlayer->AdvanceFrame()) {
    m_ffmpegAnimTimer.Stop();
    m_isPlayingFFmpeg = false;
    MPLOG("OnFFmpegAnimTimer: video ended");
  }
}

void MediaPopup::OnFFmpegFrame(const wxBitmap &frame) {
  if (!frame.IsOk())
    return;

  m_bitmap = frame;
  m_hasImage = true;
  Refresh();
}

void MediaPopup::PlayLottie(const wxString &path, bool loop) {
#ifdef HAVE_RLOTTIE
  MPLOG("PlayLottie: path=" << path.ToStdString() << " loop=" << loop);

  if (HasFailedRecently(path)) {
    MPLOG("PlayLottie: skipping recently failed file");
    FallbackToThumbnail();
    return;
  }

  // Don't restart if already playing the same file
  if (m_isPlayingLottie && m_lottiePath == path) {
    MPLOG("PlayLottie: already playing same file, not restarting");
    return;
  }

  StopAllPlayback();

  m_lottiePath = path;

  // Show loading state while initializing
  m_isLoading = true;
  m_loadingFrame = 0;
  m_loadingTimer.Start(150);
  ApplySizeAndPosition(MIN_WIDTH, MIN_HEIGHT);
  Refresh();

  if (!m_lottiePlayer) {
    m_lottiePlayer = std::make_unique<LottiePlayer>();
  }

  // Determine max size for stickers
  int maxWidth = STICKER_MAX_WIDTH - PADDING * 2;
  int maxHeight = STICKER_MAX_HEIGHT - PADDING * 2 - 20;

  m_lottiePlayer->SetRenderSize(maxWidth, maxHeight);
  m_lottiePlayer->SetLoop(loop);

  m_lottiePlayer->SetFrameCallback(
      [this](const wxBitmap &frame) { OnLottieFrame(frame); });

  if (!m_lottiePlayer->LoadFile(path)) {
    MPLOG("PlayLottie: failed to load: " << path.ToStdString());
    MarkLoadFailed(path);
    m_lottiePlayer.reset();
    m_isLoading = false;
    m_loadingTimer.Stop();
    FallbackToThumbnail();
    return;
  }

  m_isLoading = false;
  m_loadingTimer.Stop();

  // Get actual dimensions and calculate popup size
  size_t lotWidth = m_lottiePlayer->GetWidth();
  size_t lotHeight = m_lottiePlayer->GetHeight();

  if (lotWidth > 0 && lotHeight > 0) {
    double scaleX = (double)maxWidth / lotWidth;
    double scaleY = (double)maxHeight / lotHeight;
    double scale = std::min(scaleX, scaleY);

    int scaledWidth = (int)(lotWidth * scale);
    int scaledHeight = (int)(lotHeight * scale);

    m_lottiePlayer->SetRenderSize(scaledWidth, scaledHeight);

    int popupWidth = scaledWidth + PADDING * 2 + BORDER_WIDTH * 2;
    int popupHeight = scaledHeight + PADDING * 2 + BORDER_WIDTH * 2 + 24;
    ApplySizeAndPosition(popupWidth, popupHeight);
  } else {
    ApplySizeAndPosition(STICKER_MAX_WIDTH, STICKER_MAX_HEIGHT);
  }

  // Get first frame
  m_bitmap = m_lottiePlayer->GetCurrentFrame();
  m_hasImage = m_bitmap.IsOk();

  m_lottiePlayer->Play();
  m_isPlayingLottie = true;

  int interval = m_lottiePlayer->GetTimerIntervalMs();
  m_lottieAnimTimer.Start(interval);

  Refresh();
  MPLOG("PlayLottie: playback started, interval=" << interval << "ms"
        << " frames=" << m_lottiePlayer->GetTotalFrames()
        << " fps=" << m_lottiePlayer->GetFrameRate());
#else
  MPLOG("PlayLottie: rlottie support not compiled in, falling back to thumbnail");
  FallbackToThumbnail();
#endif
}

void MediaPopup::StopLottie() {
  MPLOG("StopLottie called");
  m_lottieAnimTimer.Stop();

  if (m_lottiePlayer) {
    m_lottiePlayer->Stop();
  }
  m_isPlayingLottie = false;
  m_lottiePath.Clear();
}

void MediaPopup::OnLottieAnimTimer(wxTimerEvent &event) {
  if (!m_lottiePlayer || !m_isPlayingLottie) {
    m_lottieAnimTimer.Stop();
    return;
  }

  if (!m_lottiePlayer->AdvanceFrame()) {
    m_lottieAnimTimer.Stop();
    m_isPlayingLottie = false;
    MPLOG("OnLottieAnimTimer: animation ended");
  }
}

void MediaPopup::OnLottieFrame(const wxBitmap &frame) {
  if (!frame.IsOk())
    return;

  m_bitmap = frame;
  m_hasImage = true;
  Refresh();
}

void MediaPopup::FallbackToThumbnail() {
  MPLOG("FallbackToThumbnail: thumbnailPath=" << m_mediaInfo.thumbnailPath.ToStdString()
        << " localPath=" << m_mediaInfo.localPath.ToStdString());

  m_loadingTimer.Stop();

  if (!m_mediaInfo.thumbnailPath.IsEmpty() && wxFileExists(m_mediaInfo.thumbnailPath)) {
    // Try to play animated thumbnail (e.g. WebP) if it hasn't failed recently
    if (IsVideoFormat(m_mediaInfo.thumbnailPath) && !HasFailedRecently(m_mediaInfo.thumbnailPath)) {
      PlayVideo(m_mediaInfo.thumbnailPath, true, true);
      return;
    }

    m_hasError = false;
    m_errorMessage.Clear();
    LoadImageAsync(m_mediaInfo.thumbnailPath);
    Refresh();
  } else if (!m_mediaInfo.localPath.IsEmpty() &&
             wxFileExists(m_mediaInfo.localPath) &&
             IsSupportedImageFormat(m_mediaInfo.localPath)) {
    m_hasError = false;
    m_errorMessage.Clear();
    LoadImageAsync(m_mediaInfo.localPath);
    Refresh();
  } else if (!m_mediaInfo.emoji.IsEmpty()) {
    m_hasError = false;
    m_errorMessage.Clear();
    m_hasImage = false;
    ApplySizeAndPosition(200, 150);
    Refresh();
  } else {
    m_hasError = false;
    m_errorMessage.Clear();
    m_hasImage = false;
    ApplySizeAndPosition(180, 120);
    Refresh();
  }
}

void MediaPopup::SetImage(const wxImage &image) {
  if (!image.IsOk() || image.GetWidth() <= 0 || image.GetHeight() <= 0) {
    m_hasImage = false;
    return;
  }

  StopAllPlayback();
  m_isLoading = false;

  bool needsDownload = m_mediaInfo.fileId != 0 &&
      (m_mediaInfo.localPath.IsEmpty() || !wxFileExists(m_mediaInfo.localPath));

  if (!m_isDownloadingMedia && !needsDownload) {
    m_loadingTimer.Stop();
  } else if (!m_loadingTimer.IsRunning()) {
    m_loadingTimer.Start(150);
  }

  m_hasError = false;

  int maxWidth, maxHeight;
  if (m_mediaInfo.type == MediaType::Photo ||
      m_mediaInfo.type == MediaType::Video ||
      m_mediaInfo.type == MediaType::GIF) {
    maxWidth = PHOTO_MAX_WIDTH;
    maxHeight = PHOTO_MAX_HEIGHT;
  } else {
    maxWidth = STICKER_MAX_WIDTH;
    maxHeight = STICKER_MAX_HEIGHT;
  }

  int imgWidth = image.GetWidth();
  int imgHeight = image.GetHeight();

  if (imgWidth > maxWidth || imgHeight > maxHeight) {
    double scaleX = (double)maxWidth / imgWidth;
    double scaleY = (double)maxHeight / imgHeight;
    double scale = std::min(scaleX, scaleY);

    imgWidth = (int)(imgWidth * scale);
    imgHeight = (int)(imgHeight * scale);
  }

  if (imgWidth <= 0 || imgHeight <= 0) {
    m_hasImage = false;
    return;
  }

  wxImage scaled = image.Scale(imgWidth, imgHeight, wxIMAGE_QUALITY_HIGH);
  if (!scaled.IsOk()) {
    m_hasImage = false;
    return;
  }

  m_bitmap = wxBitmap(scaled);
  m_hasImage = true;

  int width = m_bitmap.GetWidth() + (PADDING * 2) + (BORDER_WIDTH * 2);
  int height = m_bitmap.GetHeight() + (PADDING * 2) + (BORDER_WIDTH * 2) + 24;
  ApplySizeAndPosition(width, height);
  Refresh();
}

void MediaPopup::SetImage(const wxString &path) {
  wxImage image;
  if (LoadImageWithWebPSupport(path, image)) {
    SetImage(image);
  } else {
    m_hasImage = false;
  }
}

void MediaPopup::ShowLoading() {
  StopVideo();
  m_isLoading = true;
  m_loadingFrame = 0;
  if (!m_loadingTimer.IsRunning()) {
    m_loadingTimer.Start(150);
  }
  m_hasImage = false;
  m_hasError = false;
  UpdateSize();
  Refresh();
}

void MediaPopup::ShowError(const wxString &message) {
  StopVideo();
  m_hasError = true;
  m_errorMessage = message;
  m_isLoading = false;
  m_hasImage = false;
  UpdateSize();
  Refresh();
}

void MediaPopup::OnLoadingTimer(wxTimerEvent &event) {
  m_loadingFrame++;

  if (!IsShown() && !m_isDownloadingMedia) {
    m_loadingTimer.Stop();
    return;
  }

  if (IsShown()) {
    Refresh();
  }
}

wxString MediaPopup::GetMediaLabel() const {
  switch (m_mediaInfo.type) {
  case MediaType::Photo:
    return "Photo";
  case MediaType::Video:
    return "Video";
  case MediaType::Sticker:
    return "Sticker" + (m_mediaInfo.emoji.IsEmpty() ? "" : " " + m_mediaInfo.emoji);
  case MediaType::GIF:
    return "GIF";
  case MediaType::Voice:
    return "Voice Message";
  case MediaType::VideoNote:
    return "Video Message";
  case MediaType::File:
    return "File: " + m_mediaInfo.fileName;
  case MediaType::Reaction:
    return m_mediaInfo.emoji + " from " + m_mediaInfo.reactedBy;
  default:
    return "Media";
  }
}

wxString MediaPopup::GetMediaIcon() const {
  switch (m_mediaInfo.type) {
  case MediaType::Photo:
    return "[Photo]";
  case MediaType::Video:
    return "[Video]";
  case MediaType::Sticker:
    return m_mediaInfo.emoji.IsEmpty() ? "[Sticker]" : m_mediaInfo.emoji;
  case MediaType::GIF:
    return "[GIF]";
  case MediaType::Voice:
    return "[Voice]";
  case MediaType::VideoNote:
    return "[VideoMsg]";
  case MediaType::File:
    return "[File]";
  case MediaType::Reaction:
    return m_mediaInfo.emoji;
  default:
    return "[Media]";
  }
}

void MediaPopup::OnLeftDown(wxMouseEvent &event) {
  MPLOG("MediaPopup clicked");
  
  // For voice notes, toggle play/pause on click
  if (m_mediaInfo.type == MediaType::Voice) {
    ToggleVoicePlayback();
    return;
  }
  
  if (m_clickCallback) {
    m_clickCallback(m_mediaInfo);
  }
  event.Skip();
}

void MediaPopup::AdjustPositionToScreen(const wxPoint &pos) {
  m_originalPosition = pos;

  if (IsShown()) {
    wxSize size = GetSize();
    ApplySizeAndPosition(size.GetWidth(), size.GetHeight());
  }
}

void MediaPopup::ApplySizeAndPosition(int width, int height) {
  width = std::max(width, MIN_WIDTH);
  height = std::max(height, MIN_HEIGHT);

  const int EDGE_MARGIN = 10;
  wxPoint targetPos = m_originalPosition;
  bool isShowingBelow = true;

  if (m_originalPosition.x == 0 && m_originalPosition.y == 0) {
    wxDisplay display(static_cast<unsigned>(0));
    wxRect screenRect = display.GetClientArea();
    targetPos.x = (screenRect.GetWidth() - width) / 2 + screenRect.GetLeft();
    targetPos.y = (screenRect.GetHeight() - height) / 2 + screenRect.GetTop();
  } else {
    int displayIndex = wxDisplay::GetFromPoint(m_originalPosition);
    if (displayIndex == wxNOT_FOUND) {
      displayIndex = 0;
      int minDist = INT_MAX;
      for (unsigned int i = 0; i < wxDisplay::GetCount(); i++) {
        wxDisplay disp(i);
        wxRect rect = disp.GetClientArea();
        int cx = rect.GetLeft() + rect.GetWidth() / 2;
        int cy = rect.GetTop() + rect.GetHeight() / 2;
        int dist = std::abs(m_originalPosition.x - cx) +
                   std::abs(m_originalPosition.y - cy);
        if (dist < minDist) {
          minDist = dist;
          displayIndex = i;
        }
      }
    }

    wxDisplay display(displayIndex);
    wxRect screenRect = display.GetClientArea();

    int effectiveLeft = screenRect.GetLeft() + EDGE_MARGIN;
    int effectiveRight = screenRect.GetRight() - EDGE_MARGIN;
    int effectiveTop = screenRect.GetTop() + EDGE_MARGIN;
    int effectiveBottom = screenRect.GetBottom() - EDGE_MARGIN;

    targetPos.x = m_originalPosition.x;

    if (targetPos.x + width > effectiveRight) {
      targetPos.x = effectiveRight - width;
    }
    if (targetPos.x < effectiveLeft) {
      targetPos.x = effectiveLeft;
    }

    const int SMALL_GAP = 5;
    int spaceBelow = effectiveBottom - m_originalPosition.y - SMALL_GAP;
    int spaceAbove = m_originalPosition.y - effectiveTop - SMALL_GAP;

    if (height <= spaceBelow) {
      targetPos.y = m_originalPosition.y + SMALL_GAP;
      isShowingBelow = true;
    } else if (height <= spaceAbove) {
      targetPos.y = m_originalPosition.y - height - SMALL_GAP;
      isShowingBelow = false;
    } else {
      if (spaceBelow >= spaceAbove) {
        targetPos.y = m_originalPosition.y + SMALL_GAP;
        isShowingBelow = true;
      } else {
        targetPos.y = m_originalPosition.y - height - SMALL_GAP;
        isShowingBelow = false;
      }
    }

    int minAllowedTop = effectiveTop - 50;
    if (targetPos.y < minAllowedTop) {
      targetPos.y = minAllowedTop;
    }
    if (targetPos.y < screenRect.GetTop()) {
      targetPos.y = screenRect.GetTop();
    }

    if (targetPos.y + height > effectiveBottom + 30) {
      int overshoot = (targetPos.y + height) - effectiveBottom;
      if (!isShowingBelow) {
        overshoot = std::min(overshoot, height / 3);
      }
      targetPos.y -= overshoot;
      if (targetPos.y < screenRect.GetTop()) {
        targetPos.y = screenRect.GetTop();
      }
    }

    if (targetPos.x < effectiveLeft) {
      targetPos.x = effectiveLeft;
    }
    if (targetPos.x + width > effectiveRight) {
      targetPos.x = effectiveRight - width;
    }
  }

#ifdef __WXGTK__
  Hide();
  Move(-5000, -5000);
  wxYield();
  SetSize(targetPos.x, targetPos.y, width, height);
  Show();
#else
  SetSize(targetPos.x, targetPos.y, width, height);
  if (!IsShown()) {
    Show();
  }
#endif

  Raise();
  Layout();
  Refresh(true);
  Update();
}

void MediaPopup::UpdateSize() {
  int width = MIN_WIDTH;
  int height = MIN_HEIGHT;

  if (m_hasImage && m_bitmap.IsOk()) {
    width = m_bitmap.GetWidth() + (PADDING * 2) + (BORDER_WIDTH * 2);
    height = m_bitmap.GetHeight() + (PADDING * 2) + (BORDER_WIDTH * 2) + 24;
  } else if (m_mediaInfo.type == MediaType::Sticker && !m_mediaInfo.emoji.IsEmpty()) {
    width = 200;
    height = 150;
  } else {
    width = PHOTO_MAX_WIDTH;
    height = PHOTO_MAX_HEIGHT;
  }

  width = std::max(width, MIN_WIDTH);
  height = std::max(height, MIN_HEIGHT);

  ApplySizeAndPosition(width, height);
}

void MediaPopup::OnPaint(wxPaintEvent &event) {
  wxBufferedPaintDC dc(this);
  wxSize size = GetSize();

  dc.SetBrush(wxBrush(m_bgColor));
  dc.SetPen(wxPen(m_borderColor, BORDER_WIDTH));
  dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());

  // Special rendering for voice notes
  if (m_mediaInfo.type == MediaType::Voice) {
    DrawVoiceWaveform(dc, size);
    return;
  }

  int contentX = PADDING + BORDER_WIDTH;
  int contentY = PADDING + BORDER_WIDTH;
  int contentWidth = size.GetWidth() - (PADDING * 2) - (BORDER_WIDTH * 2);

  if (m_isPlayingFFmpeg && m_hasImage && m_bitmap.IsOk()) {
    int imgX = contentX + (contentWidth - m_bitmap.GetWidth()) / 2;
    int imgY = contentY;
    dc.DrawBitmap(m_bitmap, imgX, imgY, true);
    DrawMediaLabel(dc, size);
    return;
  }

  if (m_hasImage && m_bitmap.IsOk()) {
    int imgX = contentX + (contentWidth - m_bitmap.GetWidth()) / 2;
    int imgY = contentY;
    dc.DrawBitmap(m_bitmap, imgX, imgY, true);

    bool isVideoType = (m_mediaInfo.type == MediaType::Video ||
                        m_mediaInfo.type == MediaType::GIF ||
                        m_mediaInfo.type == MediaType::VideoNote);

    bool isShowingThumbnail = !m_mediaInfo.thumbnailPath.IsEmpty() &&
                              wxFileExists(m_mediaInfo.thumbnailPath) &&
                              (m_mediaInfo.localPath.IsEmpty() ||
                               !wxFileExists(m_mediaInfo.localPath));
    bool needsDownload = m_mediaInfo.fileId != 0 &&
        (m_mediaInfo.localPath.IsEmpty() || !wxFileExists(m_mediaInfo.localPath));

    int centerX = imgX + m_bitmap.GetWidth() / 2;
    int centerY = imgY + m_bitmap.GetHeight() / 2;
    int radius = 24;

    if (m_isLoading || m_isDownloadingMedia || (isShowingThumbnail && needsDownload)) {
      dc.SetBrush(wxBrush(wxColour(0, 0, 0, 150)));
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.DrawRectangle(imgX, imgY, m_bitmap.GetWidth(), m_bitmap.GetHeight());

      dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.DrawCircle(centerX, centerY, radius);

      static const wxString spinnerChars[] = {"|", "/", "-", "\\", "|", "/", "-", "\\"};
      wxString spinner = spinnerChars[m_loadingFrame % 8];
      dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold());
      dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
      wxSize spinnerSize = dc.GetTextExtent(spinner);
      dc.DrawText(spinner, centerX - spinnerSize.GetWidth() / 2,
                  centerY - spinnerSize.GetHeight() / 2);

      dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Italic());
      wxString statusText = "Downloading...";
      wxSize statusSize = dc.GetTextExtent(statusText);
      dc.DrawText(statusText, centerX - statusSize.GetWidth() / 2, centerY + radius + 8);
    } else if (isVideoType) {
      dc.SetBrush(wxBrush(wxColour(0, 0, 0, 100)));
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.DrawCircle(centerX, centerY, radius + 4);

      dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.DrawCircle(centerX, centerY, radius);

      wxPoint triangle[3];
      triangle[0] = wxPoint(centerX - 6, centerY - 10);
      triangle[1] = wxPoint(centerX - 6, centerY + 10);
      triangle[2] = wxPoint(centerX + 10, centerY);
      dc.SetBrush(wxBrush(wxColour(255, 255, 255)));
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.DrawPolygon(3, triangle);

      dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Italic());
      dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
      wxString hint = "Click to play";
      wxSize hintSize = dc.GetTextExtent(hint);
      dc.DrawText(hint, centerX - hintSize.GetWidth() / 2, centerY + radius + 8);
    }

    DrawMediaLabel(dc, size);

  } else if (m_hasError) {
    dc.SetTextForeground(wxColour(0xCC, 0x00, 0x00));
    dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

    wxString errorText = m_errorMessage.IsEmpty() ? "Error loading media" : m_errorMessage;
    wxSize textSize = dc.GetTextExtent(errorText);
    int textX = (size.GetWidth() - textSize.GetWidth()) / 2;
    int textY = (size.GetHeight() - textSize.GetHeight()) / 2;
    dc.DrawText(errorText, textX, textY);

  } else if (m_isLoading || m_isDownloadingMedia) {
    static const wxString spinners[] = {"|", "/", "-", "\\", "|", "/", "-", "\\"};
    wxString spinner = spinners[m_loadingFrame % 8];

    int centerX = size.GetWidth() / 2;
    int centerY = contentY + 40;
    int radius = 28;

    dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawCircle(centerX, centerY, radius);

    dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold().Scaled(2.0));
    dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));

    wxSize spinnerSize = dc.GetTextExtent(spinner);
    dc.DrawText(spinner, centerX - spinnerSize.GetWidth() / 2,
                centerY - spinnerSize.GetHeight() / 2);

    dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
    dc.SetTextForeground(m_textColor);
    wxString statusText = "Downloading...";
    wxSize statusSize = dc.GetTextExtent(statusText);
    int statusX = (size.GetWidth() - statusSize.GetWidth()) / 2;
    int statusY = centerY + radius + 10;
    dc.DrawText(statusText, statusX, statusY);

    DrawMediaLabel(dc, size);

  } else {
    wxString icon = GetMediaIcon();
    double scaleFactor = 3.0;
    if (m_mediaInfo.type == MediaType::Sticker && !m_mediaInfo.emoji.IsEmpty()) {
      scaleFactor = 5.0;
      icon = m_mediaInfo.emoji;
    }

    dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Scaled(scaleFactor));
    dc.SetTextForeground(m_textColor);

    wxSize iconSize = dc.GetTextExtent(icon);
    int iconX = (size.GetWidth() - iconSize.GetWidth()) / 2;
    int iconY = contentY + 5;
    dc.DrawText(icon, iconX, iconY);

    wxString typeText = GetMediaLabel();
    if (m_mediaInfo.type == MediaType::Sticker && !m_mediaInfo.emoji.IsEmpty()) {
      typeText = "Sticker";
    }

    dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold());
    dc.SetTextForeground(m_textColor);

    wxSize typeSize = dc.GetTextExtent(typeText);
    int typeX = (size.GetWidth() - typeSize.GetWidth()) / 2;
    int typeY = iconY + iconSize.GetHeight() + 5;
    dc.DrawText(typeText, typeX, typeY);

    wxString infoText;
    if (!m_mediaInfo.fileSize.IsEmpty()) {
      infoText = m_mediaInfo.fileSize;
    }
    if (!m_mediaInfo.fileName.IsEmpty() && m_mediaInfo.type != MediaType::File) {
      if (!infoText.IsEmpty())
        infoText += " - ";
      infoText += m_mediaInfo.fileName;
    }

    if (!infoText.IsEmpty()) {
      dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Smaller());
      dc.SetTextForeground(m_labelColor);

      wxSize infoSize = dc.GetTextExtent(infoText);
      if (infoSize.GetWidth() > contentWidth) {
        while (infoText.Length() > 3 &&
               dc.GetTextExtent(infoText + "...").GetWidth() > contentWidth) {
          infoText = infoText.Left(infoText.Length() - 1);
        }
        infoText += "...";
        infoSize = dc.GetTextExtent(infoText);
      }

      int infoX = (size.GetWidth() - infoSize.GetWidth()) / 2;
      int infoY = typeY + typeSize.GetHeight() + 3;
      dc.DrawText(infoText, infoX, infoY);
    }
  }
}

void MediaPopup::DrawMediaLabel(wxDC &dc, const wxSize &size) {
  wxString label = GetMediaLabel();

  dc.SetTextForeground(m_labelColor);
  dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

  int maxLabelWidth = size.GetWidth() - (PADDING * 2);
  wxSize labelSize = dc.GetTextExtent(label);

  if (labelSize.GetWidth() > maxLabelWidth) {
    while (label.Length() > 3 &&
           dc.GetTextExtent(label + "...").GetWidth() > maxLabelWidth) {
      label = label.Left(label.Length() - 1);
    }
    label += "...";
    labelSize = dc.GetTextExtent(label);
  }

  int labelX = (size.GetWidth() - labelSize.GetWidth()) / 2;
  int labelY = size.GetHeight() - 18;
  dc.DrawText(label, labelX, labelY);

  if (!m_mediaInfo.caption.IsEmpty()) {
    dc.SetFont(wxSystemSettings::GetFont(wxSYS_ANSI_FIXED_FONT).Smaller().Italic());

    wxString caption = m_mediaInfo.caption;
    wxSize captionSize = dc.GetTextExtent(caption);

    if (captionSize.GetWidth() > maxLabelWidth) {
      while (caption.Length() > 3 &&
             dc.GetTextExtent(caption + "...").GetWidth() > maxLabelWidth) {
        caption = caption.Left(caption.Length() - 1);
      }
      caption += "...";
    }

    int captionX = PADDING + BORDER_WIDTH;
    int captionY = labelY - 14;
    dc.DrawText(caption, captionX, captionY);
  }
}

void MediaPopup::LoadImageAsync(const wxString &path) {
  if (path.IsEmpty() || !wxFileExists(path)) {
    MPLOG("LoadImageAsync: invalid path");
    return;
  }

  if (HasFailedRecently(path)) {
    MPLOG("LoadImageAsync: skipping recently failed path: " << path.ToStdString());
    FallbackToThumbnail();
    return;
  }

  m_pendingImagePath = path;

  std::thread([this, path]() {
    wxImage image;
    bool success = LoadImageWithWebPSupport(path, image) && image.IsOk();

    wxThreadEvent *event = new wxThreadEvent(wxEVT_IMAGE_LOADED);
    event->SetString(path);

    if (success) {
      event->SetPayload(image);
      event->SetInt(1);
    } else {
      event->SetInt(0);
    }

    wxQueueEvent(this, event);
  }).detach();
}

void MediaPopup::OnImageLoaded(wxThreadEvent &event) {
  wxString path = event.GetString();

  if (!m_pendingImagePath.IsEmpty() && path != m_pendingImagePath) {
    return;
  }

  if (event.GetInt() == 1) {
    wxImage image = event.GetPayload<wxImage>();
    SetImage(image);
  } else {
    MPLOG("OnImageLoaded: failed to load image: " << path.ToStdString());
    MarkLoadFailed(path);
    FallbackToThumbnail();
  }
}

void MediaPopup::OnAsyncLoadTimer(wxTimerEvent &event) {
  if (!m_asyncLoadPending || m_pendingImagePath.IsEmpty()) {
    return;
  }

  wxString path = m_pendingImagePath;
  m_pendingImagePath.Clear();
  m_asyncLoadPending = false;

  wxImage image;
  if (LoadImageWithWebPSupport(path, image) && image.IsOk()) {
    SetImage(image);
  } else {
    MPLOG("OnAsyncLoadTimer: failed to load image: " << path.ToStdString());
    MarkLoadFailed(path);
    FallbackToThumbnail();
  }
}

bool MediaPopup::HasFailedRecently(const wxString &path) const {
  return m_failedLoads.find(path) != m_failedLoads.end();
}

void MediaPopup::MarkLoadFailed(const wxString &path) {
  if (!path.IsEmpty()) {
    m_failedLoads.insert(path);
    MPLOG("MarkLoadFailed: " << path.ToStdString()
          << " (total failures: " << m_failedLoads.size() << ")");

    if (m_failedLoads.size() > 100) {
      m_failedLoads.clear();
    }
  }
}

void MediaPopup::ClearFailedLoads() {
  m_failedLoads.clear();
}

// Decode TDLib waveform data (5-bit values packed into bytes)
std::vector<int> MediaPopup::DecodeWaveform(const std::vector<uint8_t>& waveformData, int targetLength) {
  std::vector<int> samples;
  
  if (waveformData.empty()) {
    // Return a flat waveform if no data
    return std::vector<int>(targetLength, 16);
  }
  
  // Unpack 5-bit values from bytes
  int bitPos = 0;
  size_t byteIdx = 0;
  
  while (byteIdx < waveformData.size()) {
    int value = 0;
    int bitsRemaining = 5;
    int shift = 0;
    
    while (bitsRemaining > 0 && byteIdx < waveformData.size()) {
      int bitsInCurrentByte = 8 - bitPos;
      int bitsToTake = std::min(bitsRemaining, bitsInCurrentByte);
      
      int mask = (1 << bitsToTake) - 1;
      int extracted = (waveformData[byteIdx] >> bitPos) & mask;
      value |= (extracted << shift);
      
      shift += bitsToTake;
      bitsRemaining -= bitsToTake;
      bitPos += bitsToTake;
      
      if (bitPos >= 8) {
        bitPos = 0;
        byteIdx++;
      }
    }
    
    samples.push_back(value);
  }
  
  if (samples.empty()) {
    return std::vector<int>(targetLength, 16);
  }
  
  // Resample to target length
  std::vector<int> resampled(targetLength);
  for (int i = 0; i < targetLength; i++) {
    size_t srcIdx = (i * samples.size()) / targetLength;
    if (srcIdx >= samples.size()) srcIdx = samples.size() - 1;
    resampled[i] = samples[srcIdx];
  }
  
  return resampled;
}

void MediaPopup::PlayVoiceNote(const wxString& path) {
  MPLOG("PlayVoiceNote: " << path.ToStdString());
  
  if (HasFailedRecently(path)) {
    MPLOG("PlayVoiceNote: skipping recently failed file");
    m_hasError = true;
    m_errorMessage = "Failed to load";
    Refresh();
    return;
  }
  
  m_isLoading = false;
  m_loadingTimer.Stop();
  
  // Stop any existing playback first
  if (m_ffmpegPlayer) {
    m_ffmpegPlayer->Stop();
  }
  
  // Use FFmpegPlayer for audio playback (cross-platform via SDL2)
  if (!m_ffmpegPlayer) {
    m_ffmpegPlayer = std::make_unique<FFmpegPlayer>();
  }
  
  // Track which file we're playing
  m_currentVoicePath = path;
  
  m_ffmpegPlayer->SetLoop(false);
  m_ffmpegPlayer->SetMuted(false);  // Voice notes should be audible!
  
  if (!m_ffmpegPlayer->LoadFile(path)) {
    MPLOG("PlayVoiceNote: failed to load: " << path.ToStdString());
    MarkLoadFailed(path);
    m_hasError = true;
    m_errorMessage = "Failed to load audio";
    Refresh();
    return;
  }
  
  // Get duration from FFmpeg if we don't have it
  double duration = m_ffmpegPlayer->GetDuration();
  if (duration > 0) {
    m_voiceDuration = duration;
  }
  
  m_ffmpegPlayer->Play();
  m_isPlayingVoice = true;
  m_voiceProgress = 0.0;
  
  // Start progress timer (update ~20 times per second for smooth progress)
  m_voiceProgressTimer.Start(50);
  
  Refresh();
}

void MediaPopup::ToggleVoicePlayback() {
  // Check if we need to load a different file
  bool needsLoad = !m_ffmpegPlayer || 
                   m_currentVoicePath.IsEmpty() ||
                   m_currentVoicePath != m_mediaInfo.localPath;
  
  if (needsLoad) {
    // Load and play the new file
    if (!m_mediaInfo.localPath.IsEmpty() && wxFileExists(m_mediaInfo.localPath)) {
      PlayVoiceNote(m_mediaInfo.localPath);
    }
    return;
  }
  
  if (m_isPlayingVoice) {
    // Pause playback
    m_ffmpegPlayer->Pause();
    m_voiceProgressTimer.Stop();
    m_isPlayingVoice = false;
  } else {
    // Resume or restart
    if (m_voiceProgress >= 0.99) {
      // Restart from beginning
      m_voiceProgress = 0.0;
      m_ffmpegPlayer->Seek(0.0);
    }
    m_ffmpegPlayer->Play();
    m_voiceProgressTimer.Start(50);
    m_isPlayingVoice = true;
  }
  
  Refresh();
}

void MediaPopup::OnVoiceProgressTimer(wxTimerEvent& event) {
  if (!m_ffmpegPlayer || !m_isPlayingVoice) {
    m_voiceProgressTimer.Stop();
    return;
  }
  
  // Keep audio buffer filled (for audio-only files)
  if (m_ffmpegPlayer->IsAudioOnly()) {
    m_ffmpegPlayer->AdvanceFrame();
  }
  
  // Get current position from FFmpeg
  double currentTime = m_ffmpegPlayer->GetCurrentTime();
  
  if (m_voiceDuration > 0) {
    m_voiceProgress = currentTime / m_voiceDuration;
    if (m_voiceProgress > 1.0) m_voiceProgress = 1.0;
  }
  
  // Check if playback finished
  if (!m_ffmpegPlayer->IsPlaying() || m_voiceProgress >= 0.99) {
    m_isPlayingVoice = false;
    m_voiceProgress = 1.0;
    m_voiceProgressTimer.Stop();
  }
  
  Refresh();
}

void MediaPopup::DrawVoiceWaveform(wxDC& dc, const wxSize& size) {
  int contentWidth = size.GetWidth() - PADDING * 2 - BORDER_WIDTH * 2;
  int contentHeight = size.GetHeight() - PADDING * 2 - BORDER_WIDTH * 2;
  
  // Layout:
  // [Play/Pause Icon] [Waveform Bars] [Time]
  int iconSize = 24;
  int timeWidth = 50;
  int waveformX = PADDING + BORDER_WIDTH + iconSize + 8;
  int waveformWidth = contentWidth - iconSize - timeWidth - 16;
  int waveformHeight = contentHeight - 20;
  int waveformY = PADDING + BORDER_WIDTH + 10;
  
  // Draw play/pause icon
  wxColour accentColor(0x00, 0x88, 0xCC);  // Nice blue
  dc.SetBrush(wxBrush(accentColor));
  dc.SetPen(*wxTRANSPARENT_PEN);
  
  int iconX = PADDING + BORDER_WIDTH + 4;
  int iconY = (size.GetHeight() - iconSize) / 2;
  
  if (m_isPlayingVoice) {
    // Draw pause icon (two vertical bars)
    int barWidth = 6;
    int gap = 4;
    dc.DrawRectangle(iconX, iconY, barWidth, iconSize);
    dc.DrawRectangle(iconX + barWidth + gap, iconY, barWidth, iconSize);
  } else {
    // Draw play icon (triangle)
    wxPoint triangle[3];
    triangle[0] = wxPoint(iconX, iconY);
    triangle[1] = wxPoint(iconX, iconY + iconSize);
    triangle[2] = wxPoint(iconX + iconSize, iconY + iconSize / 2);
    dc.DrawPolygon(3, triangle);
  }
  
  // Draw waveform bars
  int numBars = m_decodedWaveform.empty() ? 40 : static_cast<int>(m_decodedWaveform.size());
  int barWidth = std::max(2, (waveformWidth - numBars) / numBars);
  int gap = 1;
  int actualBarWidth = barWidth - gap;
  if (actualBarWidth < 2) actualBarWidth = 2;
  
  // Calculate progress position
  int progressBar = static_cast<int>(m_voiceProgress * numBars);
  
  for (int i = 0; i < numBars && i * (actualBarWidth + gap) < waveformWidth; i++) {
    int barX = waveformX + i * (actualBarWidth + gap);
    
    // Get bar height from waveform data (0-31 range)
    int value = m_decodedWaveform.empty() ? 16 : m_decodedWaveform[i % m_decodedWaveform.size()];
    int barHeight = std::max(4, (value * waveformHeight) / 31);
    int barY = waveformY + (waveformHeight - barHeight) / 2;
    
    // Color based on progress
    if (i < progressBar) {
      dc.SetBrush(wxBrush(accentColor));  // Played portion
    } else {
      dc.SetBrush(wxBrush(m_labelColor));  // Unplayed portion
    }
    
    // Draw rounded bar
    dc.DrawRoundedRectangle(barX, barY, actualBarWidth, barHeight, 1);
  }
  
  // Draw time - always show current / total format
  int currentSecs = static_cast<int>(m_voiceProgress * m_voiceDuration);
  int totalSecs = static_cast<int>(m_voiceDuration);
  wxString timeStr = wxString::Format("%d:%02d / %d:%02d",
                                       currentSecs / 60, currentSecs % 60,
                                       totalSecs / 60, totalSecs % 60);
  
  dc.SetTextForeground(m_textColor);
  wxFont font = dc.GetFont();
  font.SetPointSize(9);
  dc.SetFont(font);
  
  wxSize textSize = dc.GetTextExtent(timeStr);
  int timeX = size.GetWidth() - PADDING - BORDER_WIDTH - textSize.GetWidth() - 4;
  int timeY = (size.GetHeight() - textSize.GetHeight()) / 2;
  dc.DrawText(timeStr, timeX, timeY);
  
  // Draw label at bottom
  wxString label = "Voice Message";
  if (m_isLoading) {
    label = "Loading...";
  }
  dc.SetTextForeground(m_labelColor);
  textSize = dc.GetTextExtent(label);
  dc.DrawText(label, (size.GetWidth() - textSize.GetWidth()) / 2,
              size.GetHeight() - PADDING - textSize.GetHeight());
}