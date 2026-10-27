// GlbThumbnail.cpp - Fournisseur de miniatures (IThumbnailProvider) pour les fichiers .glb
// Build : voir build.bat (x64, MSVC).  Install : install.bat (en administrateur).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <propsys.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <wincodec.h>

#include <new>
#include <string>
#include <vector>

#include "glb_render.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(linker, "/EXPORT:DllGetClassObject,PRIVATE")
#pragma comment(linker, "/EXPORT:DllCanUnloadNow,PRIVATE")
#pragma comment(linker, "/EXPORT:DllRegisterServer,PRIVATE")
#pragma comment(linker, "/EXPORT:DllUnregisterServer,PRIVATE")

// {B7E1F4A2-6C3D-4E8A-9F15-2D7A8C0B3E64}
static const CLSID CLSID_GlbThumbnailProvider =
    {0xb7e1f4a2, 0x6c3d, 0x4e8a, {0x9f, 0x15, 0x2d, 0x7a, 0x8c, 0x0b, 0x3e, 0x64}};
static const wchar_t* kThumbHandlerKey = L"{E357FCCD-A995-4576-B01F-234630154E96}";  // IThumbnailProvider

static HINSTANCE g_hInst = nullptr;
static volatile LONG g_objects = 0, g_locks = 0;

// ------------------------------------------------------------ décodage des textures (WIC)
template <class T> struct CP {
    T* p = nullptr;
    ~CP() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() { return p; }
};

static bool DecodeWic(const uint8_t* d, size_t n, int& w, int& h, std::vector<uint32_t>& px) {
    if (n == 0 || n > 0x7FFFFFFF) return false;
    CP<IStream> ms;
    ms.p = SHCreateMemStream(d, (UINT)n);
    if (!ms.p) return false;
    CP<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    CP<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromStream(ms.p, nullptr, WICDecodeMetadataCacheOnDemand, &dec))) return false;
    CP<IWICBitmapFrameDecode> fr;
    if (FAILED(dec->GetFrame(0, &fr))) return false;
    CP<IWICFormatConverter> cv;
    if (FAILED(f->CreateFormatConverter(&cv))) return false;
    if (FAILED(cv->Initialize(fr.p, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                              WICBitmapPaletteTypeCustom)))
        return false;
    UINT uw = 0, uh = 0;
    if (FAILED(cv->GetSize(&uw, &uh)) || !uw || !uh || (unsigned long long)uw * uh > 64ull * 1024 * 1024) return false;
    px.resize((size_t)uw * uh);
    if (FAILED(cv->CopyPixels(nullptr, uw * 4, (UINT)(px.size() * 4), (BYTE*)px.data()))) return false;
    w = (int)uw;
    h = (int)uh;
    return true;
}

// ------------------------------------------------------------ lecture du flux
static bool ReadAll(IStream* s, std::vector<uint8_t>& buf) {
    const size_t kMax = 512ull * 1024 * 1024;
    LARGE_INTEGER z = {};
    s->Seek(z, STREAM_SEEK_SET, nullptr);
    STATSTG st = {};
    if (SUCCEEDED(s->Stat(&st, STATFLAG_NONAME)) && st.cbSize.QuadPart > 0) {
        if (st.cbSize.QuadPart > (ULONGLONG)kMax) return false;
        buf.reserve((size_t)st.cbSize.QuadPart);
    }
    uint8_t tmp[1 << 16];
    for (;;) {
        ULONG got = 0;
        HRESULT hr = s->Read(tmp, sizeof(tmp), &got);
        if (FAILED(hr)) return false;
        if (got == 0) break;
        if (buf.size() + got > kMax) return false;
        buf.insert(buf.end(), tmp, tmp + got);
        if (hr == S_FALSE) break;
    }
    return !buf.empty();
}

// ------------------------------------------------------------ le provider
class GlbThumbnailProvider : public IInitializeWithStream, public IThumbnailProvider {
public:
    GlbThumbnailProvider() { InterlockedIncrement(&g_objects); }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, __uuidof(IInitializeWithStream)))
            *ppv = static_cast<IInitializeWithStream*>(this);
        else if (IsEqualIID(riid, __uuidof(IThumbnailProvider)))
            *ppv = static_cast<IThumbnailProvider*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = (ULONG)InterlockedDecrement(&m_ref);
        if (!r) delete this;
        return r;
    }

    // IInitializeWithStream
    STDMETHODIMP Initialize(IStream* pstm, DWORD) override {
        if (m_stream) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        m_stream = pstm;
        m_stream->AddRef();
        return S_OK;
    }

    // IThumbnailProvider
    STDMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override {
        if (!phbmp || !pdwAlpha) return E_POINTER;
        *phbmp = nullptr;
        *pdwAlpha = WTSAT_UNKNOWN;
        if (!m_stream) return E_UNEXPECTED;
        HRESULT result = E_FAIL;
        HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        try {
            std::vector<uint8_t> file;
            int size = (int)(cx < 16 ? 16 : (cx > 2048 ? 2048 : cx));
            std::vector<uint32_t> px;
            if (ReadAll(m_stream, file) &&
                glbr::RenderGlbThumbnail(file.data(), file.size(), size, DecodeWic, px)) {
                BITMAPINFO bi = {};
                bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bi.bmiHeader.biWidth = size;
                bi.bmiHeader.biHeight = -size;  // top-down
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                bi.bmiHeader.biCompression = BI_RGB;
                void* bits = nullptr;
                HBITMAP hb = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
                if (hb && bits) {
                    memcpy(bits, px.data(), px.size() * 4);
                    *phbmp = hb;
                    *pdwAlpha = WTSAT_ARGB;  // alpha prémultiplié
                    result = S_OK;
                } else if (hb) DeleteObject(hb);
            }
        } catch (...) {
            result = E_FAIL;
        }
        if (SUCCEEDED(hrCo)) CoUninitialize();
        return result;
    }

private:
    ~GlbThumbnailProvider() {
        if (m_stream) m_stream->Release();
        InterlockedDecrement(&g_objects);
    }
    LONG m_ref = 1;
    IStream* m_stream = nullptr;
};

// ------------------------------------------------------------ class factory
class Factory : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = (ULONG)InterlockedDecrement(&m_ref);
        if (!r) delete this;
        return r;
    }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* p = new (std::nothrow) GlbThumbnailProvider();
        if (!p) return E_OUTOFMEMORY;
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) InterlockedIncrement(&g_locks); else InterlockedDecrement(&g_locks);
        return S_OK;
    }
private:
    LONG m_ref = 1;
};

// ------------------------------------------------------------ exports COM
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (!IsEqualCLSID(rclsid, CLSID_GlbThumbnailProvider)) return CLASS_E_CLASSNOTAVAILABLE;
    auto* f = new (std::nothrow) Factory();
    if (!f) return E_OUTOFMEMORY;
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}
STDAPI DllCanUnloadNow() { return (g_objects == 0 && g_locks == 0) ? S_OK : S_FALSE; }

static bool SetReg(const std::wstring& path, const wchar_t* name, const std::wstring& val) {
    return RegSetKeyValueW(HKEY_LOCAL_MACHINE, path.c_str(), name, REG_SZ, val.c_str(),
                           (DWORD)((val.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

STDAPI DllRegisterServer() {
    wchar_t mod[MAX_PATH];
    if (!GetModuleFileNameW(g_hInst, mod, MAX_PATH)) return HRESULT_FROM_WIN32(GetLastError());
    wchar_t clsid[64];
    StringFromGUID2(CLSID_GlbThumbnailProvider, clsid, 64);
    const wchar_t* name = L"GLB Thumbnail Provider";
    std::wstring cls = std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + clsid;
    bool ok = SetReg(cls, nullptr, name) && SetReg(cls + L"\\InprocServer32", nullptr, mod) &&
              SetReg(cls + L"\\InprocServer32", L"ThreadingModel", L"Apartment") &&
              SetReg(std::wstring(L"SOFTWARE\\Classes\\.glb\\ShellEx\\") + kThumbHandlerKey, nullptr, clsid) &&
              SetReg(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved", clsid, name);
    if (!ok) return SELFREG_E_CLASS;  // droits administrateur requis
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

STDAPI DllUnregisterServer() {
    wchar_t clsid[64];
    StringFromGUID2(CLSID_GlbThumbnailProvider, clsid, 64);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, (std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + clsid).c_str());
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, (std::wstring(L"SOFTWARE\\Classes\\.glb\\ShellEx\\") + kThumbHandlerKey).c_str());
    RegDeleteKeyValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved", clsid);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = h;
        DisableThreadLibraryCalls(h);
    }
    return TRUE;
}
