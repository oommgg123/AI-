// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
#include "camera.h"

#include <windows.h>
#include <initguid.h>
#include <wincodec.h>

#include <cstring>
#include <vector>

void Camera::SetPreset(CameraPreset p) {
    switch (p) {
    case CameraPreset::Perspective:
        yaw   = std::atan2(2.0f, 2.0f);
        pitch = std::asin(1.6f / 3.246f);
        break;
    case CameraPreset::Top:
        yaw   = 0.0f;
        pitch = 1.55f;
        break;
    case CameraPreset::Front:
        yaw   = 0.0f;
        pitch = 0.0f;
        break;
    case CameraPreset::Right:
        yaw   = 1.5707963f;
        pitch = 0.0f;
        break;
    }
    pendingYaw = 0.0f;
    pendingPitch = 0.0f;
}

bool SaveRgbaPng(const wchar_t* path, const uint8_t* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) return false;
    static const bool comReady = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    if (!comReady) return false;

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWICImagingFactory, reinterpret_cast<void**>(&factory));
    if (FAILED(hr) || !factory) return false;

    // 输出文件流（UTF-16 路径，中文安全）
    IWICStream* stream = nullptr;
    hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
    if (FAILED(hr)) { if (stream) stream->Release(); factory->Release(); return false; }

    IWICBitmapEncoder* encoder = nullptr;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (FAILED(hr)) { if (encoder) encoder->Release(); stream->Release(); factory->Release(); return false; }

    IWICBitmapFrameEncode* frame = nullptr;
    hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));

    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);

    std::vector<uint8_t> bgra(static_cast<size_t>(width) * height * 4);
    for (size_t i = 0; i < bgra.size(); i += 4) {
        bgra[i + 0] = rgba[i + 2];
        bgra[i + 1] = rgba[i + 1];
        bgra[i + 2] = rgba[i + 0];
        bgra[i + 3] = rgba[i + 3];
    }
    if (SUCCEEDED(hr)) hr = frame->WritePixels(static_cast<UINT>(height),
                                               static_cast<UINT>(width) * 4,
                                               static_cast<UINT>(bgra.size()), bgra.data());
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();

    frame->Release();
    encoder->Release();
    stream->Release();
    factory->Release();
    return SUCCEEDED(hr);
}

bool SaveWindowShotPng(void* hwndVoid, const wchar_t* path) {
    HWND hwnd = static_cast<HWND>(hwndVoid);
    if (!hwnd || !IsWindow(hwnd)) return false;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return false;

    HDC hdc = GetDC(hwnd);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
    SelectObject(mem, old);

    BITMAPINFOHEADER bih{};
    bih.biSize = sizeof(bih);
    bih.biWidth = w;
    bih.biHeight = -h;
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;
    std::vector<uint8_t> shotBuf(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    GetDIBits(mem, bmp, 0, static_cast<UINT>(h), shotBuf.data(),
              reinterpret_cast<BITMAPINFO*>(&bih), DIB_RGB_COLORS);

    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(hwnd, hdc);

    for (size_t i = 0; i < shotBuf.size(); i += 4) std::swap(shotBuf[i], shotBuf[i + 2]);
    return SaveRgbaPng(path, shotBuf.data(), w, h);
}
