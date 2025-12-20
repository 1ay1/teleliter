#ifndef FILEUTILS_H
#define FILEUTILS_H

#include <wx/wx.h>

// Utility to determine media type from file extension
enum class FileMediaType {
    Image,
    Video,
    Audio,
    Document,
    Unknown
};

// Utility functions
wxString FormatFileSize(wxULongLong bytes);
FileMediaType GetMediaTypeFromExtension(const wxString& filename);
const wxArrayString& GetImageExtensions();
const wxArrayString& GetVideoExtensions();
const wxArrayString& GetAudioExtensions();

#endif // FILEUTILS_H