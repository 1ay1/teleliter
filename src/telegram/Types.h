#ifndef TELEGRAM_TYPES_H
#define TELEGRAM_TYPES_H

#include <cstdint>
#include <ctime>
#include <functional>
#include <map>
#include <vector>
#include <wx/wx.h>

// Undefine Windows macros that conflict with our type names
#ifdef SendMessageCallback
#undef SendMessageCallback
#endif

// Authentication state
enum class AuthState {
  WaitTdlibParameters,
  WaitPhoneNumber,
  WaitCode,
  WaitPassword,
  Ready,
  Closed,
  Error
};

// Connection state (tracks actual connection to Telegram servers)
enum class ConnectionState {
  WaitingForNetwork, // No network connection
  ConnectingToProxy, // Connecting through proxy
  Connecting,        // Connecting to Telegram servers
  Updating,          // Downloading updates
  Ready              // Connected and ready
};

// Text entity types for message formatting
enum class TextEntityType {
  Plain,
  Bold,
  Italic,
  Underline,
  Strikethrough,
  Code,
  Pre,
  TextUrl,
  Url,
  Mention,
  MentionName,
  Hashtag,
  Cashtag,
  BotCommand,
  EmailAddress,
  PhoneNumber,
  Spoiler,
  CustomEmoji,
  BlockQuote,
  Unknown
};

// Text entity for formatted text
struct TextEntity {
  TextEntityType type = TextEntityType::Plain;
  int offset = 0;      // UTF-16 code unit offset
  int length = 0;      // Length in UTF-16 code units
  wxString url;        // For TextUrl type
  int64_t userId = 0;  // For MentionName type
  wxString language;   // For Pre type (code block language)
  int64_t customEmojiId = 0;  // For CustomEmoji type
  
  TextEntity() = default;
  TextEntity(TextEntityType t, int off, int len) : type(t), offset(off), length(len) {}
};

// Chat info structure
struct ChatInfo {
  int64_t id;
  wxString title;
  wxString lastMessage;
  int64_t lastMessageDate;
  int32_t unreadCount;
  int64_t lastReadInboxMessageId;
  int32_t memberCount;
  bool isPinned;
  bool isMuted;
  int64_t order;

  bool isPrivate;
  bool isGroup;
  bool isSupergroup;
  bool isChannel;
  bool isBot;

  int64_t userId;
  int64_t supergroupId;
  int64_t basicGroupId;

  int64_t lastReadOutboxMessageId;
  int64_t
      lastReadOutboxTime; // Unix timestamp when we learned the message was read

  ChatInfo()
      : id(0), lastMessageDate(0), unreadCount(0), lastReadInboxMessageId(0),
        memberCount(0), isPinned(false), isMuted(false), order(0),
        isPrivate(false), isGroup(false), isSupergroup(false), isChannel(false),
        isBot(false), userId(0), supergroupId(0), basicGroupId(0),
        lastReadOutboxMessageId(0), lastReadOutboxTime(0) {}
};

// Message info structure
struct MessageInfo {
  int64_t id;
  int64_t chatId;
  int64_t senderId;
  wxString senderName;
  wxString text;
  int64_t date;
  int64_t editDate;
  bool isOutgoing;
  bool isEdited;
  wxString originalText; // Original text before edit (if available)

  // When a message is sent, the server assigns a new ID different from the
  // temporary local ID. This field holds the new server-assigned ID so the UI
  // can update its tracking. If non-zero, the UI should update its stored
  // message ID from 'id' to 'serverMessageId'.
  int64_t serverMessageId;

  bool hasPhoto;
  bool hasVideo;
  bool hasDocument;
  bool hasVoice;
  bool hasVideoNote;
  bool hasSticker;
  bool hasAnimation;

  wxString mediaCaption;
  wxString mediaFileName;
  int32_t mediaFileId;
  wxString mediaLocalPath;
  int64_t mediaFileSize;
  int width;
  int height;

  // For animated stickers - thumbnail for preview
  int32_t mediaThumbnailFileId;
  wxString mediaThumbnailPath;

  // For voice/video notes - duration and waveform
  int32_t mediaDuration;              // Duration in seconds
  std::vector<uint8_t> mediaWaveform; // Waveform data (5-bit values packed)

  int64_t replyToMessageId;
  wxString replyToText;

  bool isForwarded;
  wxString forwardedFrom;

  // Reactions: emoji -> list of user names who reacted
  std::map<wxString, std::vector<wxString>> reactions;

  // Text entities for formatting
  std::vector<TextEntity> entities;

  MessageInfo()
      : id(0), chatId(0), senderId(0), date(0), editDate(0), isOutgoing(false),
        isEdited(false), serverMessageId(0), hasPhoto(false), hasVideo(false),
        hasDocument(false), hasVoice(false), hasVideoNote(false),
        hasSticker(false), hasAnimation(false), mediaFileId(0),
        mediaFileSize(0), width(0), height(0), mediaThumbnailFileId(0),
        mediaDuration(0), replyToMessageId(0), isForwarded(false) {}
};

// User info structure
struct UserInfo {
  int64_t id;
  wxString firstName;
  wxString lastName;
  wxString username;
  wxString phoneNumber;
  wxString bio;
  bool isBot;
  bool isVerified;
  bool isSelf;

  bool isOnline;
  int64_t lastSeenTime;
  int64_t
      onlineExpires; // Unix timestamp when online status expires (from TDLib)

  // Profile photo
  int32_t profilePhotoSmallFileId;
  wxString profilePhotoSmallPath;
  int32_t profilePhotoBigFileId;
  wxString profilePhotoBigPath;

  wxString GetDisplayName() const {
    wxString name = firstName;
    if (!lastName.IsEmpty()) {
      name += " " + lastName;
    }
    name = name.Trim();
    
    // Fallback chain: name -> username -> phone number -> ID
    if (!name.IsEmpty()) {
      return name;
    }
    if (!username.IsEmpty()) {
      return "@" + username;
    }
    if (!phoneNumber.IsEmpty()) {
      return phoneNumber;
    }
    if (id != 0) {
      return wxString::Format("User %lld", id);
    }
    return "Unknown";
  }

  // Check if user is currently online (considering expiry time)
  bool IsCurrentlyOnline() const {
    if (!isOnline)
      return false;
    if (onlineExpires == 0)
      return isOnline; // No expiry set, trust isOnline
    return std::time(nullptr) < onlineExpires;
  }

  // Format last seen time as a human-readable string
  wxString GetLastSeenString() const {
    if (IsCurrentlyOnline()) {
      return "online";
    }
    if (lastSeenTime == 0) {
      return "last seen a long time ago";
    }
    int64_t now = std::time(nullptr);
    int64_t diff = now - lastSeenTime;
    if (diff < 60) {
      return "last seen just now";
    } else if (diff < 3600) {
      int mins = diff / 60;
      return wxString::Format("last seen %d minute%s ago", mins, mins == 1 ? "" : "s");
    } else if (diff < 86400) {
      int hours = diff / 3600;
      return wxString::Format("last seen %d hour%s ago", hours, hours == 1 ? "" : "s");
    } else if (diff < 604800) {
      int days = diff / 86400;
      return wxString::Format("last seen %d day%s ago", days, days == 1 ? "" : "s");
    } else {
      wxDateTime dt((time_t)lastSeenTime);
      return "last seen " + dt.Format("%b %d");
    }
  }

  UserInfo()
      : id(0), isBot(false), isVerified(false), isSelf(false), isOnline(false),
        lastSeenTime(0), onlineExpires(0), profilePhotoSmallFileId(0),
        profilePhotoBigFileId(0) {}
};

// Download state for tracking file downloads
enum class DownloadState {
  Pending,     // Download requested but not started
  Downloading, // Download in progress
  Completed,   // Download finished successfully
  Failed,      // Download failed (will retry)
  Cancelled    // Download cancelled by user
};

// Download info for tracking active downloads
struct DownloadInfo {
  int32_t fileId;
  int priority;
  DownloadState state;
  int retryCount;
  int64_t startTime;        // When download was initiated (for timeout)
  int64_t lastProgressTime; // Last time we received progress update
  int64_t downloadedSize;
  int64_t totalSize;
  wxString localPath;
  wxString errorMessage;

  static constexpr int MAX_RETRIES = 3;
  static constexpr int TIMEOUT_SECONDS = 60; // Timeout if no progress for 60s

  DownloadInfo()
      : fileId(0), priority(1), state(DownloadState::Pending), retryCount(0),
        startTime(0), lastProgressTime(0), downloadedSize(0), totalSize(0) {}

  DownloadInfo(int32_t id, int prio)
      : fileId(id), priority(prio), state(DownloadState::Pending),
        retryCount(0), startTime(wxGetUTCTime()),
        lastProgressTime(wxGetUTCTime()), downloadedSize(0), totalSize(0) {}

  bool CanRetry() const { return retryCount < MAX_RETRIES; }
  bool IsTimedOut() const {
    return (wxGetUTCTime() - lastProgressTime) > TIMEOUT_SECONDS;
  }
};

// Callback types for async operations
using AuthCallback =
    std::function<void(AuthState state, const wxString &error)>;
using ChatsCallback = std::function<void(const std::vector<ChatInfo> &chats)>;
using MessagesCallback =
    std::function<void(const std::vector<MessageInfo> &messages)>;
using SendMessageCallback =
    std::function<void(bool success, int64_t messageId, const wxString &error)>;
using FileCallback = std::function<void(bool success, const wxString &localPath,
                                        const wxString &error)>;

#endif // TELEGRAM_TYPES_H