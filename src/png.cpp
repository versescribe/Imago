#include "png.h"
#include "file_io.h"
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <memory>

using Microsoft::WRL::ComPtr;
namespace imago {
namespace {
constexpr GUID SaveDialogId{0x866df22a,0x7801,0x4c73,{0xb1,0xe8,0x31,0x21,0xbb,0xac,0x6c,0x99}};
struct GlobalMemoryLock {
    HGLOBAL memory;
    void* data;
    explicit GlobalMemoryLock(HGLOBAL value):memory(value),data(GlobalLock(value)){}
    ~GlobalMemoryLock(){if(data)GlobalUnlock(memory);}
    GlobalMemoryLock(const GlobalMemoryLock&)=delete;
    GlobalMemoryLock& operator=(const GlobalMemoryLock&)=delete;
};
bool EncodePng(const Image& image,const std::wstring& path,std::wstring& error) {
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    ComPtr<IStream> stream;
    if(SUCCEEDED(hr))hr=CreateStreamOnHGlobal(nullptr,TRUE,&stream);
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if(SUCCEEDED(hr))hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);
    if(SUCCEEDED(hr))hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
    if(SUCCEEDED(hr))hr=encoder->CreateNewFrame(&frame,nullptr);
    if(SUCCEEDED(hr))hr=frame->Initialize(nullptr);
    if(SUCCEEDED(hr))hr=frame->SetSize(image.width,image.height);
    if(SUCCEEDED(hr))hr=frame->SetResolution(96,96);
    auto format=GUID_WICPixelFormat32bppBGRA;
    if(SUCCEEDED(hr))hr=frame->SetPixelFormat(&format);
    if(SUCCEEDED(hr) && !IsEqualGUID(format,GUID_WICPixelFormat32bppBGRA))hr=WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    if(SUCCEEDED(hr))hr=frame->WritePixels(image.height,image.width*4,static_cast<UINT>(image.pixels.size()*4),
        reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(image.pixels.data())));
    if(SUCCEEDED(hr))hr=frame->Commit();
    if(SUCCEEDED(hr))hr=encoder->Commit();
    frame.Reset();encoder.Reset();
    STATSTG statistics{};
    if(SUCCEEDED(hr))hr=stream->Stat(&statistics,STATFLAG_NONAME);
    HGLOBAL memory{};
    if(SUCCEEDED(hr))hr=GetHGlobalFromStream(stream.Get(),&memory);
    if(FAILED(hr) || statistics.cbSize.QuadPart>MAXDWORD){error=L"PNG 编码失败，图像未保存。";return false;}
    GlobalMemoryLock locked(memory);
    if(!locked.data){error=L"无法读取 PNG 编码结果。";return false;}
    switch(WriteFileAtomically(path,{static_cast<const std::byte*>(locked.data),statistics.cbSize.LowPart})) {
    case FileWriteResult::Saved:return true;
    case FileWriteResult::CreateFailed:error=L"无法写入目标目录，请选择其他位置。";break;
    case FileWriteResult::WriteFailed:error=L"写入文件失败，请检查磁盘空间或目录权限。";break;
    case FileWriteResult::ReplaceFailed:error=L"无法完成保存，目标文件可能正在被使用。";break;
    }
    return false;
}
}
SaveResult SavePngDialog(HWND owner,const Image& image,std::wstring& error) {
    if(!image.valid()){error=L"没有可保存的图像。";return SaveResult::Failed;}
    ComPtr<IFileSaveDialog> dialog;
    HRESULT hr=CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));
    if(FAILED(hr)){error=L"无法打开系统保存对话框。";return SaveResult::Failed;}
    dialog->SetClientGuid(SaveDialogId);
    FILEOPENDIALOGOPTIONS options{};
    hr=dialog->GetOptions(&options);
    if(SUCCEEDED(hr))hr=dialog->SetOptions(options|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_OVERWRITEPROMPT|FOS_STRICTFILETYPES|FOS_NOREADONLYRETURN);
    COMDLG_FILTERSPEC filter{L"PNG 图像 (*.png)",L"*.png"};
    if(SUCCEEDED(hr))hr=dialog->SetFileTypes(1,&filter);
    if(SUCCEEDED(hr))hr=dialog->SetDefaultExtension(L"png");
    SYSTEMTIME time{};GetLocalTime(&time);
    wchar_t name[80]{};
    swprintf_s(name,L"Imago_%04u%02u%02u_%02u%02u%02u.png",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);
    dialog->SetFileName(name);dialog->SetTitle(L"保存截图");
    if(SUCCEEDED(hr))hr=dialog->Show(owner);
    if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED))return SaveResult::Cancelled;
    ComPtr<IShellItem> item;
    if(SUCCEEDED(hr))hr=dialog->GetResult(&item);
    PWSTR rawPath{};
    if(SUCCEEDED(hr))hr=item->GetDisplayName(SIGDN_FILESYSPATH,&rawPath);
    if(FAILED(hr)){error=L"无法获取保存路径。";return SaveResult::Failed;}
    struct PathDeleter{void operator()(wchar_t* value)const{CoTaskMemFree(value);}};
    std::unique_ptr<wchar_t,PathDeleter> ownedPath(rawPath);
    return EncodePng(image,ownedPath.get(),error)?SaveResult::Saved:SaveResult::Failed;
}
}
