#include "TransferManager.h"

TransferManager::TransferManager()
    : m_nextId(1)
{
}

TransferManager::~TransferManager()
{
}

int TransferManager::StartUpload(const wxString& filePath, int64_t totalBytes)
{
    TransferInfo info;
    info.id = m_nextId++;
    info.direction = TransferDirection::Upload;
    info.status = TransferStatus::InProgress;
    info.filePath = filePath;
    info.fileName = filePath.AfterLast('/').AfterLast('\\');
    if (info.fileName.IsEmpty()) {
        info.fileName = filePath;
    }
    info.totalBytes = totalBytes;
    info.transferredBytes = 0;
    
    m_transfers[info.id] = info;
    NotifyProgress(info);
    
    return info.id;
}

int TransferManager::StartDownload(const wxString& fileName, int64_t totalBytes)
{
    TransferInfo info;
    info.id = m_nextId++;
    info.direction = TransferDirection::Download;
    info.status = TransferStatus::InProgress;
    info.fileName = fileName;
    info.totalBytes = totalBytes;
    info.transferredBytes = 0;
    
    m_transfers[info.id] = info;
    NotifyProgress(info);
    
    return info.id;
}

void TransferManager::UpdateProgress(int transferId, int64_t transferredBytes, int64_t totalBytes)
{
    auto it = m_transfers.find(transferId);
    if (it == m_transfers.end()) {
        return;
    }
    
    TransferInfo& info = it->second;
    info.transferredBytes = transferredBytes;
    if (totalBytes > 0) {
        info.totalBytes = totalBytes;
    }
    info.status = TransferStatus::InProgress;
    
    NotifyProgress(info);
}

void TransferManager::CompleteTransfer(int transferId, const wxString& localPath)
{
    auto it = m_transfers.find(transferId);
    if (it == m_transfers.end()) {
        return;
    }
    
    TransferInfo& info = it->second;
    info.status = TransferStatus::Completed;
    info.transferredBytes = info.totalBytes;
    if (!localPath.IsEmpty()) {
        info.filePath = localPath;
    }
    
    NotifyComplete(info);
}

void TransferManager::FailTransfer(int transferId, const wxString& error)
{
    auto it = m_transfers.find(transferId);
    if (it == m_transfers.end()) {
        return;
    }
    
    TransferInfo& info = it->second;
    info.status = TransferStatus::Failed;
    info.error = error;
    
    NotifyError(info);
}

void TransferManager::CancelTransfer(int transferId)
{
    auto it = m_transfers.find(transferId);
    if (it == m_transfers.end()) {
        return;
    }
    
    TransferInfo& info = it->second;
    info.status = TransferStatus::Cancelled;
    
    NotifyError(info);
}

TransferInfo* TransferManager::GetTransfer(int transferId)
{
    auto it = m_transfers.find(transferId);
    if (it == m_transfers.end()) {
        return nullptr;
    }
    return &it->second;
}

const TransferInfo* TransferManager::GetTransfer(int transferId) const
{
    auto it = m_transfers.find(transferId);
    if (it == m_transfers.end()) {
        return nullptr;
    }
    return &it->second;
}

int TransferManager::GetActiveCount() const
{
    int count = 0;
    for (const auto& pair : m_transfers) {
        if (pair.second.status == TransferStatus::InProgress ||
            pair.second.status == TransferStatus::Pending) {
            count++;
        }
    }
    return count;
}

const TransferInfo* TransferManager::GetCurrentTransfer() const
{
    // Return the most recent active transfer
    const TransferInfo* current = nullptr;
    int highestId = 0;
    
    for (const auto& pair : m_transfers) {
        if ((pair.second.status == TransferStatus::InProgress ||
             pair.second.status == TransferStatus::Pending) &&
            pair.second.id > highestId) {
            current = &pair.second;
            highestId = pair.second.id;
        }
    }
    
    return current;
}

bool TransferManager::HasActiveTransfers() const
{
    return GetActiveCount() > 0;
}

void TransferManager::SetProgressCallback(TransferCallback callback)
{
    m_progressCallback = callback;
}

void TransferManager::SetCompleteCallback(TransferCallback callback)
{
    m_completeCallback = callback;
}

void TransferManager::SetErrorCallback(TransferCallback callback)
{
    m_errorCallback = callback;
}

void TransferManager::CleanupOldTransfers(int maxAgeSeconds)
{
    // For now, just remove completed/failed/cancelled transfers
    // In a real implementation, we'd track timestamps
    auto it = m_transfers.begin();
    while (it != m_transfers.end()) {
        if (it->second.status == TransferStatus::Completed ||
            it->second.status == TransferStatus::Failed ||
            it->second.status == TransferStatus::Cancelled) {
            it = m_transfers.erase(it);
        } else {
            ++it;
        }
    }
}

void TransferManager::NotifyProgress(const TransferInfo& info)
{
    if (m_progressCallback) {
        m_progressCallback(info);
    }
}

void TransferManager::NotifyComplete(const TransferInfo& info)
{
    if (m_completeCallback) {
        m_completeCallback(info);
    }
}

void TransferManager::NotifyError(const TransferInfo& info)
{
    if (m_errorCallback) {
        m_errorCallback(info);
    }
}