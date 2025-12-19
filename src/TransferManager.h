#ifndef TRANSFERMANAGER_H
#define TRANSFERMANAGER_H

#include <wx/wx.h>
#include <wx/gauge.h>
#include <map>
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
        
        // In progress - show percentage and size
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
        return direction == TransferDirection::Upload ? "⬆" : "⬇";
    }
};

// Callback for transfer events
using TransferCallback = std::function<void(const TransferInfo&)>;

// Manages all file transfers with progress tracking
class TransferManager
{
public:
    TransferManager();
    ~TransferManager();
    
    // Start a new transfer, returns transfer ID
    int StartUpload(const wxString& filePath, int64_t totalBytes = 0);
    int StartDownload(const wxString& fileName, int64_t totalBytes = 0);
    
    // Update transfer progress (called by TDLib callbacks)
    void UpdateProgress(int transferId, int64_t transferredBytes, int64_t totalBytes);
    
    // Mark transfer as complete
    void CompleteTransfer(int transferId, const wxString& localPath = "");
    
    // Mark transfer as failed
    void FailTransfer(int transferId, const wxString& error);
    
    // Cancel a transfer
    void CancelTransfer(int transferId);
    
    // Get transfer info
    TransferInfo* GetTransfer(int transferId);
    const TransferInfo* GetTransfer(int transferId) const;
    
    // Get active transfer count
    int GetActiveCount() const;
    
    // Get the most recent active transfer (for status bar display)
    const TransferInfo* GetCurrentTransfer() const;
    
    // Check if any transfers are active
    bool HasActiveTransfers() const;
    
    // Set callback for progress updates (to update UI)
    void SetProgressCallback(TransferCallback callback);
    
    // Set callback for completion
    void SetCompleteCallback(TransferCallback callback);
    
    // Set callback for errors
    void SetErrorCallback(TransferCallback callback);
    
    // Clean up completed/failed transfers older than X seconds
    void CleanupOldTransfers(int maxAgeSeconds = 60);
    
private:
    int m_nextId;
    std::map<int, TransferInfo> m_transfers;
    
    TransferCallback m_progressCallback;
    TransferCallback m_completeCallback;
    TransferCallback m_errorCallback;
    
    void NotifyProgress(const TransferInfo& info);
    void NotifyComplete(const TransferInfo& info);
    void NotifyError(const TransferInfo& info);
};

#endif // TRANSFERMANAGER_H