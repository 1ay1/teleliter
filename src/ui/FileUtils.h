#ifndef FILEUTILS_H
#define FILEUTILS_H

#include <wx/wx.h>
#include <wx/image.h>

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

// Load image from file, with WebP support if available
// Returns true if image was loaded successfully
bool LoadImageWithWebPSupport(const wxString& path, wxImage& outImage);

// Check if a file extension is a natively supported image format (excluding WebP)
bool IsNativelySupportedImageFormat(const wxString& path);

// Check if WebP support is available
bool HasWebPSupport();

#endif // FILEUTILS_H