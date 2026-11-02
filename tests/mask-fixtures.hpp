// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../src/mask-image.hpp"

inline bool fixture_file(const std::string &path,const std::string &contents)
{
    HANDLE file=CreateFileW(mapping::wide_path(path).c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    DWORD written=0; const bool ok=WriteFile(file,contents.data(),DWORD(contents.size()),&written,nullptr)&&written==contents.size(); CloseHandle(file); return ok;
}
inline bool fixture_png(const std::string &path,UINT width,UINT height,std::vector<uint8_t> pixels)
{
    mapping::ComScope scope;
    mapping::NativePtr<IWICImagingFactory> factory; mapping::NativePtr<IWICStream> stream;
    mapping::NativePtr<IWICBitmapEncoder> encoder; mapping::NativePtr<IWICBitmapFrameEncode> frame;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IWICImagingFactory),reinterpret_cast<void**>(factory.put()))) ||
        FAILED(factory->CreateStream(stream.put())) || FAILED(stream->InitializeFromFilename(mapping::wide_path(path).c_str(),GENERIC_WRITE)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.put())) || FAILED(encoder->Initialize(stream.value,WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(frame.put(),nullptr)) || FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(width,height))) return false;
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
    if(FAILED(frame->SetPixelFormat(&format)) || !IsEqualGUID(format,GUID_WICPixelFormat32bppBGRA)) return false;
    for(size_t i=0;i<pixels.size();i+=4) std::swap(pixels[i],pixels[i+2]);
    return SUCCEEDED(frame->WritePixels(height,width*4,UINT(pixels.size()),pixels.data())) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}
inline bool create_mask_fixtures(const std::string &directory)
{
    CreateDirectoryW(mapping::wide_path(directory).c_str(),nullptr);
    std::vector<uint8_t> half(8*8*4,255),gray(8*8*4,128);
    for(int y=0;y<8;++y) for(int x=4;x<8;++x) half[size_t(y*8+x)*4+3]=0;
    return fixture_png(directory+"/máscara-ñ.png",8,8,half) && fixture_png(directory+"/gray.png",8,8,gray) &&
        fixture_file(directory+"/half.svg","<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 64 64\"><path fill=\"white\" d=\"M0 0H32V64H0Z\"/></svg>") &&
        fixture_file(directory+"/partial.svg","<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 64 64\"><rect width=\"64\" height=\"64\" fill=\"white\" opacity=\"0.5\"/></svg>") &&
        fixture_file(directory+"/bad.png","not a png") && fixture_file(directory+"/bad.svg","not an svg");
}
