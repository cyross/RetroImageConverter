// SPDX-License-Identifier: MIT
// Windows Imaging Component (WIC) による読み込み・書き出し
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
#include <wincodec.h>
#include <propidl.h>
#include <cstdio>
#include <algorithm>
#include "ImageIO.h"

#ifdef _MSC_VER
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#endif

namespace ric {

namespace {

template <class T> struct ComPtr {
    T* p = nullptr;
    ComPtr() = default;
    ~ComPtr() { if (p) p->Release(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    operator T*() const { return p; }
};

struct ComScope {
    bool needUninit = false;
    ComScope() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        needUninit = SUCCEEDED(hr);
    }
    ~ComScope() { if (needUninit) CoUninitialize(); }
};

bool createFactory(ComPtr<IWICImagingFactory>& f) {
    return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f.p)));
}

std::string hrMessage(const char* what, HRESULT hr) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s (HRESULT 0x%08X)", what, (unsigned)hr);
    return buf;
}

} // namespace

bool platformDecode(const std::vector<uint8_t>& data, Image& out, std::string& error) {
    ComScope com;
    ComPtr<IWICImagingFactory> factory;
    if (!createFactory(factory)) { error = "WIC を初期化できません"; return false; }
    ComPtr<IWICStream> stream;
    HRESULT hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromMemory(const_cast<BYTE*>(data.data()), (DWORD)data.size());
    ComPtr<IWICBitmapDecoder> dec;
    if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &dec);
    if (FAILED(hr)) {
        error = "画像形式を認識できません (WebP の読み込みには OS の WebP 拡張機能が必要です)";
        return false;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    hr = dec->GetFrame(0, &frame);
    ComPtr<IWICBitmapSource> conv;
    if (SUCCEEDED(hr)) hr = WICConvertBitmapSource(GUID_WICPixelFormat32bppRGBA, frame, &conv);
    UINT w = 0, h = 0;
    if (SUCCEEDED(hr)) hr = conv->GetSize(&w, &h);
    if (FAILED(hr) || w == 0 || h == 0) { error = hrMessage("画像のデコードに失敗しました", hr); return false; }
    out = Image((int)w, (int)h);
    hr = conv->CopyPixels(nullptr, w * 4, w * h * 4, reinterpret_cast<BYTE*>(out.pixels.data()));
    if (FAILED(hr)) { error = hrMessage("画素の取得に失敗しました", hr); return false; }
    return true;
}

bool platformCanEncode(FileFormat f) {
    return f == FileFormat::PNG || f == FileFormat::JPG || f == FileFormat::TIFF;
}

bool platformEncode(FileFormat f, const Image& img, int quality, std::vector<uint8_t>& out, std::string& error) {
    ComScope com;
    ComPtr<IWICImagingFactory> factory;
    if (!createFactory(factory)) { error = "WIC を初期化できません"; return false; }
    GUID container = GUID_ContainerFormatPng;
    switch (f) {
    case FileFormat::PNG: container = GUID_ContainerFormatPng; break;
    case FileFormat::JPG: container = GUID_ContainerFormatJpeg; break;
    case FileFormat::TIFF: container = GUID_ContainerFormatTiff; break;
    default: error = "未対応の形式です"; return false;
    }
    const bool alpha = (f != FileFormat::JPG) && img.hasAlpha();
    ComPtr<IStream> stream;
    HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    ComPtr<IWICBitmapEncoder> enc;
    if (SUCCEEDED(hr)) hr = factory->CreateEncoder(container, nullptr, &enc);
    if (SUCCEEDED(hr)) hr = enc->Initialize(stream, WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&frame, &props);
    if (SUCCEEDED(hr) && f == FileFormat::JPG && props) {
        PROPBAG2 opt{};
        wchar_t name[] = L"ImageQuality";
        opt.pstrName = name;
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_R4;
        v.fltVal = (float)std::max(1, std::min(100, quality)) / 100.f;
        props->Write(1, &opt, &v);
    }
    if (SUCCEEDED(hr)) hr = frame->Initialize(props);
    if (SUCCEEDED(hr)) hr = frame->SetSize((UINT)img.width, (UINT)img.height);
    WICPixelFormatGUID fmt = alpha ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR;
    WICPixelFormatGUID want = fmt;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
    if (SUCCEEDED(hr) && !IsEqualGUID(fmt, want)) hr = E_FAIL;
    if (FAILED(hr)) { error = hrMessage("エンコーダの初期化に失敗しました", hr); return false; }

    const UINT bpp = alpha ? 4 : 3;
    const UINT stride = ((UINT)img.width * bpp + 3) & ~3u;
    std::vector<BYTE> buf((size_t)stride * img.height, 0);
    for (int y = 0; y < img.height; ++y) {
        BYTE* row = &buf[(size_t)y * stride];
        for (int x = 0; x < img.width; ++x) {
            const RGBA8& c = img.at(x, y);
            row[x * bpp + 0] = c.b; row[x * bpp + 1] = c.g; row[x * bpp + 2] = c.r;
            if (alpha) row[x * bpp + 3] = c.a;
        }
    }
    hr = frame->WritePixels((UINT)img.height, stride, (UINT)buf.size(), buf.data());
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = enc->Commit();
    if (FAILED(hr)) { error = hrMessage("エンコードに失敗しました", hr); return false; }

    STATSTG st{};
    hr = stream->Stat(&st, STATFLAG_NONAME);
    HGLOBAL hg = nullptr;
    if (SUCCEEDED(hr)) hr = GetHGlobalFromStream(stream, &hg);
    if (FAILED(hr) || !hg) { error = hrMessage("出力データの取得に失敗しました", hr); return false; }
    size_t size = (size_t)st.cbSize.QuadPart;
    void* p = GlobalLock(hg);
    if (!p) { error = "メモリのロックに失敗しました"; return false; }
    out.assign((const uint8_t*)p, (const uint8_t*)p + size);
    GlobalUnlock(hg);
    return true;
}

std::string utf8ToSjis(const std::string& s) {
    if (s.empty()) return s;
    int wn = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)wn, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), wn);
    int n = WideCharToMultiByte(932, 0, w.data(), wn, nullptr, 0, nullptr, nullptr);
    std::string o((size_t)n, '\0');
    WideCharToMultiByte(932, 0, w.data(), wn, o.data(), n, nullptr, nullptr);
    return o;
}

std::string sjisToUtf8(const std::string& s) {
    if (s.empty()) return s;
    int wn = MultiByteToWideChar(932, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)wn, L'\0');
    MultiByteToWideChar(932, 0, s.data(), (int)s.size(), w.data(), wn);
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), wn, nullptr, 0, nullptr, nullptr);
    std::string o((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), wn, o.data(), n, nullptr, nullptr);
    return o;
}

} // namespace ric
#endif
