// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <d2d1_3.h>
#include <d3d11.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cwctype>
#include <xmllite.h>
#include <cmath>

namespace mapping {
template<class T> struct NativePtr {
    T *value=nullptr;
    NativePtr()=default; NativePtr(const NativePtr&)=delete; NativePtr& operator=(const NativePtr&)=delete;
    ~NativePtr() { if(value) value->Release(); }
    T **put() { if(value) value->Release(); value=nullptr; return &value; }
    T *operator->() const { return value; }
};
struct ComScope {
    HRESULT status=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    ~ComScope() { if(SUCCEEDED(status)) CoUninitialize(); }
};
struct MaskImage { uint32_t width=0,height=0; std::vector<uint8_t> pixels; std::string error; };
inline std::wstring wide_path(const std::string &path)
{
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,nullptr,0);
    if(!count) return {};
    std::wstring result(size_t(count),0);
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,result.data(),count); result.pop_back(); return result;
}
inline MaskImage load_png(const std::wstring &path)
{
    MaskImage result; result.error="No se pudo cargar el PNG de máscara.";
    ComScope scope;
    NativePtr<IWICImagingFactory> factory; NativePtr<IWICBitmapDecoder> decoder;
    NativePtr<IWICBitmapFrameDecode> frame; NativePtr<IWICFormatConverter> converter;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IWICImagingFactory),reinterpret_cast<void**>(factory.put()))) ||
        FAILED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,decoder.put())) ||
        FAILED(decoder->GetFrame(0,frame.put())) || FAILED(frame->GetSize(&result.width,&result.height))) return result;
    if(!result.width || !result.height || result.width>4096 || result.height>4096) { result.error="La máscara debe medir entre 1 y 4096 píxeles por dimensión."; return result; }
    if(FAILED(factory->CreateFormatConverter(converter.put())) || FAILED(converter->Initialize(frame.value,GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return result;
    result.pixels.resize(size_t(result.width)*result.height*4);
    if(FAILED(converter->CopyPixels(nullptr,result.width*4,static_cast<UINT>(result.pixels.size()),result.pixels.data()))) { result.pixels.clear(); return result; }
    result.error.clear(); return result;
}
inline MaskImage load_svg(const std::wstring &path)
{
    MaskImage result; result.error="No se pudo rasterizar el SVG con Direct2D (Windows 10+).";
    ComScope scope;
    NativePtr<IStream> stream;
    if(FAILED(SHCreateStreamOnFileEx(path.c_str(),STGM_READ|STGM_SHARE_DENY_WRITE,FILE_ATTRIBUTE_NORMAL,FALSE,nullptr,stream.put()))) return result;
    STATSTG stat{};
    if(FAILED(stream->Stat(&stat,STATFLAG_NONAME)) || stat.cbSize.QuadPart>16*1024*1024) { result.error="El SVG supera el límite de 16 MiB o no se puede leer."; return result; }
    float viewport_width=1024,viewport_height=1024;
    {
        NativePtr<IXmlReader> reader;
        if(FAILED(CreateXmlReader(__uuidof(IXmlReader),reinterpret_cast<void**>(reader.put()),nullptr)) || FAILED(reader->SetInput(stream.value))) return result;
        reader->SetProperty(XmlReaderProperty_DtdProcessing,DtdProcessing_Prohibit);
        XmlNodeType type=XmlNodeType_None;
        bool found_svg=false;
        while(reader->Read(&type)==S_OK) if(type==XmlNodeType_Element) {
            const wchar_t *name=nullptr; reader->GetLocalName(&name,nullptr);
            if(!name || std::wstring(name)!=L"svg") return result;
            found_svg=true;
            if(reader->MoveToFirstAttribute()==S_OK) do {
                const wchar_t *attribute=nullptr,*value=nullptr;
                reader->GetLocalName(&attribute,nullptr); reader->GetValue(&value,nullptr);
                if(!attribute || !value) continue;
                std::wstring key(attribute); if(key!=L"width" && key!=L"height") continue;
                wchar_t *end=nullptr; double length=wcstod(value,&end);
                std::wstring unit=end?end:L"";
                while(!unit.empty() && iswspace(unit.front())) unit.erase(unit.begin());
                while(!unit.empty() && iswspace(unit.back())) unit.pop_back();
                if(unit==L"in") length*=96; else if(unit==L"cm") length*=96/2.54; else if(unit==L"mm") length*=96/25.4;
                else if(unit==L"pt") length*=96.0/72; else if(unit==L"pc") length*=16; else if(!unit.empty() && unit!=L"px") continue;
                if(std::isfinite(length) && length>0 && length<1e7) (key==L"width"?viewport_width:viewport_height)=float(length);
            } while(reader->MoveToNextAttribute()==S_OK);
            break;
        }
        if(!found_svg) return result;
    }
    LARGE_INTEGER beginning{}; if(FAILED(stream->Seek(beginning,STREAM_SEEK_SET,nullptr))) return result;
    NativePtr<ID3D11Device> d3d; NativePtr<IDXGIDevice> dxgi;
    NativePtr<ID2D1Factory1> factory; NativePtr<ID2D1Device> device;
    NativePtr<ID2D1DeviceContext> base; NativePtr<ID2D1DeviceContext5> context;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,d3d.put(),nullptr,nullptr)) ||
        FAILED(d3d->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(dxgi.put()))) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,__uuidof(ID2D1Factory1),nullptr,reinterpret_cast<void**>(factory.put()))) ||
        FAILED(factory->CreateDevice(dxgi.value,device.put())) || FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,base.put())) ||
        FAILED(base->QueryInterface(__uuidof(ID2D1DeviceContext5),reinterpret_cast<void**>(context.put())))) return result;
    constexpr UINT size=1024;
    NativePtr<ID2D1SvgDocument> document;
    if(FAILED(context->CreateSvgDocument(stream.value,D2D1::SizeF(viewport_width,viewport_height),document.put()))) return result;
    const auto format=D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED);
    const auto target_props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,format);
    const auto read_props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,format);
    NativePtr<ID2D1Bitmap1> target,readback;
    if(FAILED(context->CreateBitmap(D2D1::SizeU(size,size),nullptr,0,&target_props,target.put())) ||
        FAILED(context->CreateBitmap(D2D1::SizeU(size,size),nullptr,0,&read_props,readback.put()))) return result;
    context->SetTransform(D2D1::Matrix3x2F::Scale(float(size)/viewport_width,float(size)/viewport_height));
    context->SetTarget(target.value); context->BeginDraw(); context->Clear(D2D1::ColorF(0,0,0,0)); context->DrawSvgDocument(document.value);
    if(FAILED(context->EndDraw()) || FAILED(readback->CopyFromBitmap(nullptr,target.value,nullptr))) return result;
    D2D1_MAPPED_RECT mapped{};
    if(FAILED(readback->Map(D2D1_MAP_OPTIONS_READ,&mapped))) return result;
    result.width=result.height=size; result.pixels.resize(size_t(size)*size*4);
    for(UINT y=0;y<size;++y) for(UINT x=0;x<size;++x) {
        const auto *p=mapped.bits+y*mapped.pitch+x*4; auto *out=result.pixels.data()+(size_t(y)*size+x)*4;
        const unsigned alpha=p[3]; out[3]=p[3];
        out[0]=alpha?uint8_t(std::min(255u,(unsigned(p[2])*255+alpha/2)/alpha)):0;
        out[1]=alpha?uint8_t(std::min(255u,(unsigned(p[1])*255+alpha/2)/alpha)):0;
        out[2]=alpha?uint8_t(std::min(255u,(unsigned(p[0])*255+alpha/2)/alpha)):0;
    }
    readback->Unmap(); result.error.clear(); return result;
}
inline MaskImage load_mask_image(const std::string &path)
{
    auto wide=wide_path(path);
    if(wide.empty()) return {0,0,{},"Selecciona un archivo PNG o SVG de máscara."};
    std::wstring extension=wide.substr(wide.find_last_of(L'.')==std::wstring::npos?wide.size():wide.find_last_of(L'.'));
    for(auto &c:extension) c=static_cast<wchar_t>(towlower(c));
    if(extension==L".png") return load_png(wide);
    if(extension==L".svg") return load_svg(wide);
    return {0,0,{},"Formato de máscara no admitido. Usa PNG o SVG."};
}
}
