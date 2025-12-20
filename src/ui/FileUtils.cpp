#include "FileUtils.h"
#include <wx/filename.h>
#include <iostream>

#ifdef HAVE_WEBP
#include <webp/decode.h>
#include <fstream>
#include <vector>
#endif

const wxArrayString& GetImageExtensions()
{
    static wxArrayString extensions;
    if (extensions.IsEmpty()) {
        extensions.Add(".jpg");
        extensions.Add(".jpeg");
        extensions.Add(".png");
        extensions.Add(".gif");
        extensions.Add(".webp");
        extensions.Add(".bmp");
        extensions.Add(".tiff");
        extensions.Add(".tif");
    }
    return extensions;
}

const wxArrayString& GetVideoExtensions()
{
    static wxArrayString extensions;
    if (extensions.IsEmpty()) {
        extensions.Add(".mp4");
        extensions.Add(".mkv");
        extensions.Add(".avi");
        extensions.Add(".mov");
        extensions.Add(".webm");
        extensions.Add(".m4v");
        extensions.Add(".wmv");
        extensions.Add(".flv");
    }
    return extensions;
}

const wxArrayString& GetAudioExtensions()
{
    static wxArrayString extensions;
    if (extensions.IsEmpty()) {
        extensions.Add(".mp3");
        extensions.Add(".ogg");
        extensions.Add(".wav");
        extensions.Add(".flac");
        extensions.Add(".m4a");
        extensions.Add(".aac");
        extensions.Add(".wma");
        extensions.Add(".opus");
    }
    return extensions;
}

wxString FormatFileSize(wxULongLong bytes)
{
    double size = bytes.ToDouble();
    
    if (size >= 1024.0 * 1024.0 * 1024.0) {
        return wxString::Format("%.2f GB", size / (1024.0 * 1024.0 * 1024.0));
    } else if (size >= 1024.0 * 1024.0) {
        return wxString::Format("%.2f MB", size / (1024.0 * 1024.0));
    } else if (size >= 1024.0) {
        return wxString::Format("%.1f KB", size / 1024.0);
    } else {
        return wxString::Format("%.0f bytes", size);
    }
}

FileMediaType GetMediaTypeFromExtension(const wxString& filename)
{
    wxString ext = filename.AfterLast('.').Lower();
    if (ext.IsEmpty() || ext == filename.Lower()) {
        return FileMediaType::Document;
    }
    
    ext = "." + ext;
    
    const wxArrayString& imageExts = GetImageExtensions();
    for (const auto& e : imageExts) {
        if (ext == e) {
            return FileMediaType::Image;
        }
    }
    
    const wxArrayString& videoExts = GetVideoExtensions();
    for (const auto& e : videoExts) {
        if (ext == e) {
            return FileMediaType::Video;
        }
    }
    
    const wxArrayString& audioExts = GetAudioExtensions();
    for (const auto& e : audioExts) {
        if (ext == e) {
            return FileMediaType::Audio;
        }
    }
    
    return FileMediaType::Document;
}

bool HasWebPSupport()
{
#ifdef HAVE_WEBP
    return true;
#else
    return false;
#endif
}

bool IsNativelySupportedImageFormat(const wxString& path)
{
    wxFileName fn(path);
    wxString ext = fn.GetExt().Lower();
    
    // These formats are natively supported by wxWidgets
    if (ext == "jpg" || ext == "jpeg" || ext == "png" || 
        ext == "gif" || ext == "bmp" || ext == "ico" || 
        ext == "tiff" || ext == "tif" || ext == "xpm" ||
        ext == "pcx" || ext == "pnm") {
        return true;
    }
    
    return false;
}

#ifdef HAVE_WEBP
static bool LoadWebPImage(const wxString& path, wxImage& outImage)
{
    try {
        // Read file into memory
        std::ifstream file(path.ToStdString(), std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "[FileUtils] LoadWebPImage: failed to open file" << std::endl;
            return false;
        }
        
        std::streamsize size = file.tellg();
        if (size <= 0) {
            std::cerr << "[FileUtils] LoadWebPImage: empty file" << std::endl;
            return false;
        }
        
        // Limit file size to prevent memory issues (50MB max)
        if (size > 50 * 1024 * 1024) {
            std::cerr << "[FileUtils] LoadWebPImage: file too large (" << size << " bytes)" << std::endl;
            return false;
        }
        
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(size);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            std::cerr << "[FileUtils] LoadWebPImage: failed to read file" << std::endl;
            return false;
        }
        file.close();
        
        // Get WebP image info
        int width = 0, height = 0;
        if (!WebPGetInfo(buffer.data(), buffer.size(), &width, &height)) {
            std::cerr << "[FileUtils] LoadWebPImage: WebPGetInfo failed" << std::endl;
            return false;
        }
        
        // Validate dimensions
        if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
            std::cerr << "[FileUtils] LoadWebPImage: invalid dimensions " << width << "x" << height << std::endl;
            return false;
        }
        
        // Check for excessive pixel count (prevent OOM)
        if (static_cast<int64_t>(width) * height > 64 * 1024 * 1024) {
            std::cerr << "[FileUtils] LoadWebPImage: image too large (" << width << "x" << height << ")" << std::endl;
            return false;
        }
        
        // Decode WebP to RGBA
        uint8_t* rgba = WebPDecodeRGBA(buffer.data(), buffer.size(), &width, &height);
        if (!rgba) {
            std::cerr << "[FileUtils] LoadWebPImage: WebPDecodeRGBA failed" << std::endl;
            return false;
        }
        
        // Create wxImage from RGBA data
        // wxImage expects separate RGB and alpha channels
        outImage.Create(width, height, false);
        if (!outImage.IsOk()) {
            std::cerr << "[FileUtils] LoadWebPImage: failed to create wxImage" << std::endl;
            WebPFree(rgba);
            return false;
        }
        
        outImage.InitAlpha();
        
        unsigned char* imgData = outImage.GetData();
        unsigned char* alphaData = outImage.GetAlpha();
        
        if (!imgData || !alphaData) {
            std::cerr << "[FileUtils] LoadWebPImage: null image data pointers" << std::endl;
            WebPFree(rgba);
            return false;
        }
        
        for (int i = 0; i < width * height; i++) {
            imgData[i * 3 + 0] = rgba[i * 4 + 0];  // R
            imgData[i * 3 + 1] = rgba[i * 4 + 1];  // G
            imgData[i * 3 + 2] = rgba[i * 4 + 2];  // B
            alphaData[i] = rgba[i * 4 + 3];         // A
        }
        
        WebPFree(rgba);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[FileUtils] LoadWebPImage exception: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[FileUtils] LoadWebPImage: unknown exception" << std::endl;
        return false;
    }
}
#endif

bool LoadImageWithWebPSupport(const wxString& path, wxImage& outImage)
{
    try {
        if (path.IsEmpty()) {
            return false;
        }
        
        if (!wxFileExists(path)) {
            return false;
        }
        
        wxFileName fn(path);
        wxString ext = fn.GetExt().Lower();
        
        // Handle WebP files specially
        if (ext == "webp") {
#ifdef HAVE_WEBP
            return LoadWebPImage(path, outImage);
#else
            return false;
#endif
        }
        
        // For other formats, use wxImage's native loading
        if (IsNativelySupportedImageFormat(path)) {
            bool result = outImage.LoadFile(path);
            if (!result) {
                // Try loading with explicit type
                wxImage testImg;
                if (ext == "jpg" || ext == "jpeg") {
                    result = testImg.LoadFile(path, wxBITMAP_TYPE_JPEG);
                } else if (ext == "png") {
                    result = testImg.LoadFile(path, wxBITMAP_TYPE_PNG);
                }
                if (result && testImg.IsOk()) {
                    outImage = testImg;
                }
            }
            
            // Validate the loaded image
            if (result && outImage.IsOk()) {
                int w = outImage.GetWidth();
                int h = outImage.GetHeight();
                if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
                    std::cerr << "[FileUtils] LoadImageWithWebPSupport: invalid dimensions " << w << "x" << h << std::endl;
                    return false;
                }
            }
            
            return result && outImage.IsOk();
        }
        
        // Unsupported format
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[FileUtils] LoadImageWithWebPSupport exception: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[FileUtils] LoadImageWithWebPSupport: unknown exception" << std::endl;
        return false;
    }
}