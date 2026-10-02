#pragma once
#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include "Logger.h"

/**
 * @brief Background file system watcher for the Downloads directory.
 * 
 * Uses ReadDirectoryChangesW to monitor new downloads and updates,
 * coalescing rapid events before requesting a UI reload.
 */
class FolderWatcher {
public:
    FolderWatcher() : m_hDir(INVALID_HANDLE_VALUE), m_hStopEvent(NULL), m_hWndNotify(NULL) {}

    ~FolderWatcher() {
        stop();
    }

    /**
     * @brief Starts the background monitoring thread.
     * @param folderPath The directory path to monitor.
     * @param hWnd Target window handle to receive WM_USER + 100 on updates.
     */
    bool start(const std::wstring& folderPath, HWND hWnd) {
        stop();

        m_folderPath = folderPath;
        m_hWndNotify = hWnd;
        m_stopRequested = false;

        m_hDir = CreateFileW(
            folderPath.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr
        );

        if (m_hDir == INVALID_HANDLE_VALUE) {
            LOG_ERROR(L"Failed to open directory handle for watching: " + folderPath);
            return false;
        }

        m_hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_workerThread = std::thread(&FolderWatcher::run, this);

        LOG_INFO(L"Folder watcher started for: " + folderPath);
        return true;
    }

    /**
     * @brief Stops the monitoring thread and releases handles.
     */
    void stop() {
        m_stopRequested = true;
        if (m_hStopEvent) {
            SetEvent(m_hStopEvent);
        }

        if (m_hDir != INVALID_HANDLE_VALUE) {
            CancelIoEx(m_hDir, nullptr);
            CloseHandle(m_hDir);
            m_hDir = INVALID_HANDLE_VALUE;
        }

        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }

        if (m_hStopEvent) {
            CloseHandle(m_hStopEvent);
            m_hStopEvent = NULL;
        }
    }

private:
    std::wstring m_folderPath;
    HWND m_hWndNotify;
    HANDLE m_hDir;
    HANDLE m_hStopEvent;
    std::thread m_workerThread;
    std::atomic<bool> m_stopRequested{false};

    void run() {
        BYTE buffer[4096];
        OVERLAPPED overlapped = {0};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        HANDLE events[2] = { overlapped.hEvent, m_hStopEvent };

        while (!m_stopRequested) {
            ResetEvent(overlapped.hEvent);
            DWORD bytesReturned = 0;

            BOOL ok = ReadDirectoryChangesW(
                m_hDir,
                buffer,
                sizeof(buffer),
                FALSE, // Do not watch subtrees, only downloads root
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
                &bytesReturned,
                &overlapped,
                nullptr
            );

            if (!ok) {
                DWORD err = GetLastError();
                if (err != ERROR_IO_PENDING) {
                    LOG_ERROR(L"ReadDirectoryChangesW error: " + std::to_wstring(err));
                    break;
                }
            }

            // Wait for directory change or stop event
            DWORD waitRes = WaitForMultipleObjects(2, events, FALSE, INFINITE);
            if (waitRes == WAIT_OBJECT_0) {
                // Directory changed. Debounce slightly to allow in-progress writes to settle.
                std::this_thread::sleep_for(std::chrono::milliseconds(300));

                if (!m_stopRequested && m_hWndNotify && IsWindow(m_hWndNotify)) {
                    LOG_INFO(L"Directory change detected. Posting reload notification.");
                    PostMessageW(m_hWndNotify, WM_USER + 100, 0, 0);
                }
            } else {
                // Stop requested or error
                break;
            }
        }

        if (overlapped.hEvent) {
            CloseHandle(overlapped.hEvent);
        }
    }
};
