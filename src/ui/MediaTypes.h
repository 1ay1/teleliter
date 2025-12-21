#ifndef MEDIATYPES_H
#define MEDIATYPES_H

#include <wx/wx.h>

// Media types for popup display
enum class MediaType {
    Photo,
    Video,
    Sticker,
    GIF,
    Voice,
    VideoNote,
    File,
    Reaction
};

// Media info structure
struct MediaInfo {
    MediaType type;
    int32_t fileId;           // TDLib file ID (0 if not applicable)
    wxString localPath;       // Local cached path (if downloaded)
    wxString remoteUrl;       // Remote URL (if available)
    wxString fileName;        // For files
    wxString fileSize;        // Human readable size
    wxString caption;         // Media caption
    wxString emoji;           // For stickers/reactions
    wxString reactedBy;       // For reactions - who reacted
    bool isDownloading;       // True if download is in progress
    int width;
    int height;
    
    // For animated stickers - thumbnail for static preview
    int32_t thumbnailFileId;
    wxString thumbnailPath;
    
    MediaInfo() : type(MediaType::Photo), fileId(0), isDownloading(false), width(0), height(0), thumbnailFileId(0) {}
};

// Tracks media spans in the chat display
struct MediaSpan {
    long startPos;            // Start position in text
    long endPos;              // End position in text
    int64_t messageId;        // Reference to source message (single source of truth)
    int32_t fileId;           // File ID for quick lookup
    int32_t thumbnailFileId;  // Thumbnail file ID
    MediaType type;           // Media type
    
    bool Contains(long pos) const {
        return pos >= startPos && pos <= endPos;
    }
    
    MediaSpan() : startPos(0), endPos(0), messageId(0), fileId(0), thumbnailFileId(0), type(MediaType::Photo) {}
};

// Tracks edited message spans for showing edit history
struct EditSpan {
    long startPos;            // Start position of "[edited]" text
    long endPos;              // End position of "[edited]" text
    int64_t messageId;        // Message ID
    wxString originalText;    // Original text before edit
    int64_t editDate;         // When it was edited
    
    bool Contains(long pos) const {
        return pos >= startPos && pos <= endPos;
    }
};

// Tracks clickable URL links in chat display
struct LinkSpan {
    long startPos;            // Start position of link text
    long endPos;              // End position of link text
    wxString url;             // The URL to open
    
    bool Contains(long pos) const {
        return pos >= startPos && pos <= endPos;
    }
};

#endif // MEDIATYPES_H