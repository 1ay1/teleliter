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
    wxString id;              // TDLib file ID
    wxString localPath;       // Local cached path (if downloaded)
    wxString remoteUrl;       // Remote URL (if available)
    wxString fileName;        // For files
    wxString fileSize;        // Human readable size
    wxString caption;         // Media caption
    wxString emoji;           // For stickers/reactions
    wxString reactedBy;       // For reactions - who reacted
    int width;
    int height;
    
    MediaInfo() : type(MediaType::Photo), width(0), height(0) {}
};

// Tracks media spans in the chat display
struct MediaSpan {
    long startPos;            // Start position in text
    long endPos;              // End position in text
    MediaInfo info;           // Media information
    
    bool Contains(long pos) const {
        return pos >= startPos && pos <= endPos;
    }
};

#endif // MEDIATYPES_H