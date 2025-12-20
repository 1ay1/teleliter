#ifndef TRANSFERTYPES_H
#define TRANSFERTYPES_H

#include <wx/wx.h>
#include <functional>

// Transfer direction
enum class TransferDirection {
    Upload,
    Download
};

// Transfer status
enum class TransferStatus {
    Pending,
    InProgress,
    Completed,
    Failed,
    Cancelled
};

// Single transfer info
struct TransferInfo {
    int id;
    TransferDirection direction;
    TransferStatus status;
    wxString fileName;
    wxString filePath;
    int64_t totalBytes;
    int64_t transferredBytes;
    wxString error;
    
    TransferInfo()
        : id(0),
          direction(TransferDirection::Download),
          status(TransferStatus::Pending),
          totalBytes(0),
          transferredBytes(0) {}
    
    int GetProgressPercent() const {
        if (totalBytes <= 0) return 0;
        return static_cast<int>((transferredBytes * 100) / totalBytes);
    }
    
    wxString GetProgressText() const {
        if (status == TransferStatus::Pending) {
            return "Pending...";
        } else if (status == TransferStatus::Failed) {
            return "Failed";
        } else if (status == TransferStatus::Cancelled) {
            return "Cancelled";
        } else if (status == TransferStatus::Completed) {
            return "Done";
        }
        
        wxString sizeText;
        double transferred = static_cast<double>(transferredBytes);
        double total = static_cast<double>(totalBytes);
        
        if (total >= 1024.0 * 1024.0) {
            sizeText = wxString::Format("%.1f/%.1f MB", 
                transferred / (1024.0 * 1024.0),
                total / (1024.0 * 1024.0));
        } else if (total >= 1024.0) {
            sizeText = wxString::Format("%.1f/%.1f KB",
                transferred / 1024.0,
                total / 1024.0);
        } else {
            sizeText = wxString::Format("%lld/%lld B", 
                transferredBytes, totalBytes);
        }
        
        return wxString::Format("%d%% %s", GetProgressPercent(), sizeText);
    }
    
    wxString GetDirectionSymbol() const {
        return direction == TransferDirection::Upload ? "^" : "v";
    }
};

// Callback for transfer events
using TransferCallback = std::function<void(const TransferInfo&)>;

#endif // TRANSFERTYPES_H