#ifndef TRANSFERMANAGER_H
#define TRANSFERMANAGER_H

#include <wx/wx.h>
#include <wx/gauge.h>
#include <map>
#include "TransferTypes.h"

// Manages all file transfers with progress tracking
class TransferManager
{
public:
    TransferManager();
    ~TransferManager();
    
    int StartUpload(const wxString& filePath, int64_t totalBytes = 0);
    int StartDownload(const wxString& fileName, int64_t totalBytes = 0);
    
    void UpdateProgress(int transferId, int64_t transferredBytes, int64_t totalBytes);
    void CompleteTransfer(int transferId, const wxString& localPath = "");
    void FailTransfer(int transferId, const wxString& error);
    void CancelTransfer(int transferId);
    
    TransferInfo* GetTransfer(int transferId);
    const TransferInfo* GetTransfer(int transferId) const;
    
    int GetActiveCount() const;
    const TransferInfo* GetCurrentTransfer() const;
    bool HasActiveTransfers() const;
    
    void SetProgressCallback(TransferCallback callback);
    void SetCompleteCallback(TransferCallback callback);
    void SetErrorCallback(TransferCallback callback);
    
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