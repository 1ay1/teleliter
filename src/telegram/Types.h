#ifndef TELEGRAM_TYPES_H
#define TELEGRAM_TYPES_H

#include <wx/wx.h>
#include <functional>
#include <vector>

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
    
    ChatInfo() 
        : id(0), lastMessageDate(0), unreadCount(0), lastReadInboxMessageId(0),
          memberCount(0), isPinned(false), isMuted(false), order(0), isPrivate(false),
          isGroup(false), isSupergroup(false), isChannel(false), isBot(false),
          userId(0), supergroupId(0), basicGroupId(0) {}
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
    wxString originalText;  // Original text before edit (if available)
    
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
    
    int64_t replyToMessageId;
    wxString replyToText;
    
    bool isForwarded;
    wxString forwardedFrom;
    
    MessageInfo() 
        : id(0), chatId(0), senderId(0), date(0), editDate(0), isOutgoing(false),
          isEdited(false), hasPhoto(false), hasVideo(false),
          hasDocument(false), hasVoice(false), hasVideoNote(false),
          hasSticker(false), hasAnimation(false), mediaFileId(0),
          mediaFileSize(0), width(0), height(0), mediaThumbnailFileId(0),
          replyToMessageId(0), isForwarded(false) {}
};

// User info structure
struct UserInfo {
    int64_t id;
    wxString firstName;
    wxString lastName;
    wxString username;
    wxString phoneNumber;
    bool isBot;
    bool isVerified;
    bool isSelf;
    
    bool isOnline;
    int64_t lastSeenTime;
    
    wxString GetDisplayName() const {
        wxString name = firstName;
        if (!lastName.IsEmpty()) {
            name += " " + lastName;
        }
        return name.IsEmpty() ? username : name;
    }
    
    UserInfo() 
        : id(0), isBot(false), isVerified(false), isSelf(false),
          isOnline(false), lastSeenTime(0) {}
};

// Download state for tracking file downloads
enum class DownloadState {
    Pending,      // Download requested but not started
    Downloading,  // Download in progress
    Completed,    // Download finished successfully
    Failed,       // Download failed (will retry)
    Cancelled     // Download cancelled by user
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
    static constexpr int TIMEOUT_SECONDS = 60;  // Timeout if no progress for 60s
    
    DownloadInfo()
        : fileId(0), priority(1), state(DownloadState::Pending),
          retryCount(0), startTime(0), lastProgressTime(0),
          downloadedSize(0), totalSize(0) {}
    
    DownloadInfo(int32_t id, int prio)
        : fileId(id), priority(prio), state(DownloadState::Pending),
          retryCount(0), startTime(wxGetUTCTime()), lastProgressTime(wxGetUTCTime()),
          downloadedSize(0), totalSize(0) {}
    
    bool CanRetry() const { return retryCount < MAX_RETRIES; }
    bool IsTimedOut() const { 
        return (wxGetUTCTime() - lastProgressTime) > TIMEOUT_SECONDS; 
    }
};

// Callback types for async operations
using AuthCallback = std::function<void(AuthState state, const wxString& error)>;
using ChatsCallback = std::function<void(const std::vector<ChatInfo>& chats)>;
using MessagesCallback = std::function<void(const std::vector<MessageInfo>& messages)>;
using SendMessageCallback = std::function<void(bool success, int64_t messageId, const wxString& error)>;
using FileCallback = std::function<void(bool success, const wxString& localPath, const wxString& error)>;

#endif // TELEGRAM_TYPES_H