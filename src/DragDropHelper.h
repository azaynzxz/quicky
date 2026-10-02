#pragma once
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <filesystem>
#include "Logger.h"

/**
 * @brief Custom enumerator for supported clipboard formats.
 * 
 * Required by OLE data transfer so target applications (Explorer, browsers)
 * can query what data formats the dragged item provides.
 */
class CEnumFormatEtc : public IEnumFORMATETC {
    LONG m_cRefCount;
    std::vector<FORMATETC> m_formats;
    ULONG m_index;

public:
    CEnumFormatEtc(const std::vector<FORMATETC>& formats)
        : m_cRefCount(1), m_formats(formats), m_index(0) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC) {
            *ppv = static_cast<IEnumFORMATETC*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&m_cRefCount);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        LONG count = InterlockedDecrement(&m_cRefCount);
        if (count == 0) delete this;
        return count;
    }

    HRESULT STDMETHODCALLTYPE Next(ULONG celt, FORMATETC* rgelt, ULONG* pceltFetched) override {
        if (!rgelt) return E_POINTER;
        ULONG fetched = 0;
        while (m_index < m_formats.size() && fetched < celt) {
            rgelt[fetched] = m_formats[m_index];
            if (rgelt[fetched].ptd) {
                // If target device is present, duplicate it
                DVTARGETDEVICE* ptd = rgelt[fetched].ptd;
                rgelt[fetched].ptd = (DVTARGETDEVICE*)CoTaskMemAlloc(ptd->tdSize);
                if (rgelt[fetched].ptd) {
                    memcpy(rgelt[fetched].ptd, ptd, ptd->tdSize);
                }
            }
            m_index++;
            fetched++;
        }
        if (pceltFetched) *pceltFetched = fetched;
        return (fetched == celt) ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Skip(ULONG celt) override {
        m_index = (std::min)(static_cast<ULONG>(m_formats.size()), m_index + celt);
        return (m_index <= m_formats.size()) ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Reset() override {
        m_index = 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** ppenum) override {
        if (!ppenum) return E_POINTER;
        CEnumFormatEtc* clone = new CEnumFormatEtc(m_formats);
        clone->m_index = m_index;
        *ppenum = clone;
        return S_OK;
    }
};

/**
 * @brief Self-contained IDataObject implementation for file transfers.
 * 
 * Provides CF_HDROP, CF_UNICODETEXT, and CFSTR_PREFERREDDROPEFFECT to support
 * both Windows OLE drag-out and clipboard copy operations without relying
 * on brittle shell PIDL bindings.
 */
class CFileDataObject : public IDataObject {
    LONG m_cRefCount;
    std::vector<std::wstring> m_paths;
    CLIPFORMAT m_cfPreferredDropEffect;

    HGLOBAL createHDrop() const {
        if (m_paths.empty()) return nullptr;

        size_t totalChars = 0;
        for (const auto& path : m_paths) {
            totalChars += path.length() + 1;
        }
        totalChars += 1; // Extra trailing null for double-null termination

        size_t totalBytes = sizeof(DROPFILES) + (totalChars * sizeof(wchar_t));
        HGLOBAL hGlobal = GlobalAlloc(GHND | GMEM_SHARE, totalBytes);
        if (!hGlobal) return nullptr;

        DROPFILES* pDropFiles = static_cast<DROPFILES*>(GlobalLock(hGlobal));
        if (!pDropFiles) {
            GlobalFree(hGlobal);
            return nullptr;
        }

        pDropFiles->pFiles = sizeof(DROPFILES);
        pDropFiles->pt.x = 0;
        pDropFiles->pt.y = 0;
        pDropFiles->fNC = FALSE;
        pDropFiles->fWide = TRUE;

        wchar_t* pDest = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(pDropFiles) + sizeof(DROPFILES));
        for (const auto& path : m_paths) {
            wcscpy_s(pDest, path.length() + 1, path.c_str());
            pDest += path.length() + 1;
        }
        *pDest = L'\0'; // Final null

        GlobalUnlock(hGlobal);
        return hGlobal;
    }

    HGLOBAL createUnicodeText() const {
        if (m_paths.empty()) return nullptr;

        std::wstring joined;
        for (size_t i = 0; i < m_paths.size(); ++i) {
            joined += m_paths[i];
            if (i + 1 < m_paths.size()) {
                joined += L"\r\n";
            }
        }

        size_t totalBytes = (joined.length() + 1) * sizeof(wchar_t);
        HGLOBAL hGlobal = GlobalAlloc(GHND | GMEM_SHARE, totalBytes);
        if (!hGlobal) return nullptr;

        wchar_t* pDest = static_cast<wchar_t*>(GlobalLock(hGlobal));
        if (!pDest) {
            GlobalFree(hGlobal);
            return nullptr;
        }

        wcscpy_s(pDest, joined.length() + 1, joined.c_str());
        GlobalUnlock(hGlobal);
        return hGlobal;
    }

    HGLOBAL createDropEffect(DWORD effect) const {
        HGLOBAL hGlobal = GlobalAlloc(GHND | GMEM_SHARE, sizeof(DWORD));
        if (!hGlobal) return nullptr;

        DWORD* pDest = static_cast<DWORD*>(GlobalLock(hGlobal));
        if (pDest) {
            *pDest = effect;
            GlobalUnlock(hGlobal);
        } else {
            GlobalFree(hGlobal);
            return nullptr;
        }
        return hGlobal;
    }

public:
    CFileDataObject(const std::vector<std::wstring>& paths)
        : m_cRefCount(1), m_paths(paths) {
        m_cfPreferredDropEffect = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT));
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&m_cRefCount);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        LONG count = InterlockedDecrement(&m_cRefCount);
        if (count == 0) delete this;
        return count;
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* pformatetcIn, STGMEDIUM* pmedium) override {
        if (!pformatetcIn || !pmedium) return E_POINTER;
        ZeroMemory(pmedium, sizeof(STGMEDIUM));

        if (!(pformatetcIn->tymed & TYMED_HGLOBAL)) {
            return DV_E_TYMED;
        }

        if (pformatetcIn->cfFormat == CF_HDROP) {
            HGLOBAL hDrop = createHDrop();
            if (!hDrop) return E_OUTOFMEMORY;
            pmedium->tymed = TYMED_HGLOBAL;
            pmedium->hGlobal = hDrop;
            pmedium->pUnkForRelease = nullptr;
            return S_OK;
        }

        if (pformatetcIn->cfFormat == CF_UNICODETEXT) {
            HGLOBAL hText = createUnicodeText();
            if (!hText) return E_OUTOFMEMORY;
            pmedium->tymed = TYMED_HGLOBAL;
            pmedium->hGlobal = hText;
            pmedium->pUnkForRelease = nullptr;
            return S_OK;
        }

        if (pformatetcIn->cfFormat == m_cfPreferredDropEffect) {
            HGLOBAL hEffect = createDropEffect(DROPEFFECT_COPY | DROPEFFECT_MOVE);
            if (!hEffect) return E_OUTOFMEMORY;
            pmedium->tymed = TYMED_HGLOBAL;
            pmedium->hGlobal = hEffect;
            pmedium->pUnkForRelease = nullptr;
            return S_OK;
        }

        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC* pformatetc, STGMEDIUM* pmedium) override {
        return DATA_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* pformatetc) override {
        if (!pformatetc) return E_POINTER;
        if (!(pformatetc->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;

        if (pformatetc->cfFormat == CF_HDROP ||
            pformatetc->cfFormat == CF_UNICODETEXT ||
            pformatetc->cfFormat == m_cfPreferredDropEffect) {
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC* pformatectIn, FORMATETC* pformatetcOut) override {
        if (!pformatetcOut) return E_POINTER;
        *pformatetcOut = *pformatectIn;
        pformatetcOut->ptd = nullptr;
        return DATA_S_SAMEFORMATETC;
    }

    HRESULT STDMETHODCALLTYPE SetData(FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc) override {
        if (!ppenumFormatEtc) return E_POINTER;
        if (dwDirection != DATADIR_GET) return E_NOTIMPL;

        std::vector<FORMATETC> formats = {
            { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL },
            { CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL },
            { m_cfPreferredDropEffect, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL }
        };

        *ppenumFormatEtc = new CEnumFormatEtc(formats);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC* pformatetc, DWORD advf, IAdviseSink* pAdvSink, DWORD* pdwConnection) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD dwConnection) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** ppenumAdvise) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
};

/**
 * @brief OLE drop source implementation handling feedback and drag state.
 */
class CDropSource : public IDropSource {
    LONG m_cRefCount;
public:
    CDropSource() : m_cRefCount(1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&m_cRefCount);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        LONG count = InterlockedDecrement(&m_cRefCount);
        if (count == 0) delete this;
        return count;
    }

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) override {
        if (fEscapePressed) {
            return DRAGDROP_S_CANCEL;
        }
        // Left mouse button released triggers drop
        if (!(grfKeyState & MK_LBUTTON)) {
            return DRAGDROP_S_DROP;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD dwEffect) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }
};

/**
 * @brief Drop target implementation receiving files dropped into Quicky.
 */
class CDropTarget : public IDropTarget {
    LONG m_cRefCount;
    HWND m_hWnd;
    std::wstring m_downloadsPath;

public:
    CDropTarget(HWND hwnd, const std::wstring& downloadsPath)
        : m_cRefCount(1), m_hWnd(hwnd), m_downloadsPath(downloadsPath) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&m_cRefCount);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        LONG count = InterlockedDecrement(&m_cRefCount);
        if (count == 0) delete this;
        return count;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override {
        if (!pdwEffect) return E_POINTER;
        FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        if (pDataObj && pDataObj->QueryGetData(&fmt) == S_OK) {
            *pdwEffect = (*pdwEffect & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_MOVE;
        } else {
            *pdwEffect = DROPEFFECT_NONE;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override {
        if (!pdwEffect) return E_POINTER;
        *pdwEffect = (*pdwEffect & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_MOVE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override {
        if (!pdwEffect) return E_POINTER;
        *pdwEffect = DROPEFFECT_NONE;

        if (!pDataObj) return E_POINTER;

        FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM stg = {0};

        if (SUCCEEDED(pDataObj->GetData(&fmt, &stg))) {
            HDROP hDrop = static_cast<HDROP>(stg.hGlobal);
            UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            LOG_INFO(L"Dropped files received: count = " + std::to_wstring(fileCount));

            UINT copiedCount = 0;
            for (UINT i = 0; i < fileCount; i++) {
                wchar_t filePath[MAX_PATH];
                if (DragQueryFileW(hDrop, i, filePath, MAX_PATH)) {
                    std::filesystem::path src(filePath);
                    std::filesystem::path dest = std::filesystem::path(m_downloadsPath) / src.filename();
                    try {
                        std::filesystem::copy(src, dest, std::filesystem::copy_options::overwrite_existing);
                        copiedCount++;
                        LOG_INFO(L"Copied dropped file: " + src.wstring() + L" -> " + dest.wstring());
                    } catch (const std::exception& ex) {
                        LOG_ERROR(L"Failed copying file: " + src.wstring());
                    }
                }
            }
            ReleaseStgMedium(&stg);

            *pdwEffect = DROPEFFECT_COPY;

            // Notify main window to reload files
            PostMessageW(m_hWnd, WM_USER + 100, 0, 0);
            return S_OK;
        }

        return E_FAIL;
    }
};

/**
 * @brief Factory function for creating file data objects for Drag Out or Clipboard.
 */
inline IDataObject* CreateFileDataObject(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return nullptr;
    return new CFileDataObject(paths);
}

/**
 * @brief Convenience overload for single path.
 */
inline IDataObject* CreateFileDataObject(const std::wstring& path) {
    return CreateFileDataObject(std::vector<std::wstring>{ path });
}

/**
 * @brief Copies given files to the Windows clipboard using CF_HDROP.
 */
inline bool CopyFilesToClipboard(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return false;
    IDataObject* pDataObject = CreateFileDataObject(paths);
    if (!pDataObject) return false;

    HRESULT hr = OleSetClipboard(pDataObject);
    if (SUCCEEDED(hr)) {
        OleFlushClipboard();
        LOG_INFO(L"Copied " + std::to_wstring(paths.size()) + L" file(s) to clipboard");
    } else {
        LOG_ERROR(L"OleSetClipboard failed with HRESULT: " + std::to_wstring(hr));
    }
    pDataObject->Release();
    return SUCCEEDED(hr);
}

/**
 * @brief Pastes any CF_HDROP files from the Windows clipboard into the target folder.
 */
inline bool PasteFilesFromClipboard(const std::wstring& destFolder, HWND notifyWnd) {
    IDataObject* pDataObject = nullptr;
    HRESULT hr = OleGetClipboard(&pDataObject);
    if (FAILED(hr) || !pDataObject) {
        LOG_WARN(L"Clipboard does not contain accessible IDataObject");
        return false;
    }

    FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stg = {0};
    bool success = false;

    if (SUCCEEDED(pDataObject->GetData(&fmt, &stg))) {
        HDROP hDrop = static_cast<HDROP>(stg.hGlobal);
        UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        LOG_INFO(L"Pasting " + std::to_wstring(fileCount) + L" file(s) from clipboard");

        for (UINT i = 0; i < fileCount; i++) {
            wchar_t filePath[MAX_PATH];
            if (DragQueryFileW(hDrop, i, filePath, MAX_PATH)) {
                std::filesystem::path src(filePath);
                std::filesystem::path dest = std::filesystem::path(destFolder) / src.filename();
                try {
                    std::filesystem::copy(src, dest, std::filesystem::copy_options::overwrite_existing);
                    LOG_INFO(L"Pasted file: " + src.wstring());
                    success = true;
                } catch (...) {
                    LOG_ERROR(L"Error pasting file: " + src.wstring());
                }
            }
        }
        ReleaseStgMedium(&stg);

        if (success && notifyWnd) {
            PostMessageW(notifyWnd, WM_USER + 100, 0, 0);
        }
    } else {
        LOG_INFO(L"Clipboard does not contain file drop data (CF_HDROP)");
    }

    pDataObject->Release();
    return success;
}
