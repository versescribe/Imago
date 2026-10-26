#include "recording_devices.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <memory>

namespace imago {
namespace {
using Microsoft::WRL::ComPtr;
struct FreeString {void operator()(wchar_t* value)const{CoTaskMemFree(value);}};
std::wstring DeviceId(IMMDevice* device) {
    wchar_t* raw{};
    if(FAILED(device->GetId(&raw)))return {};
    std::unique_ptr<wchar_t,FreeString> value(raw);return value.get()?value.get():L"";
}
std::wstring DeviceName(IMMDevice* device) {
    ComPtr<IPropertyStore> properties;
    if(FAILED(device->OpenPropertyStore(STGM_READ,&properties)))return L"未命名音频设备";
    struct Property {PROPVARIANT value{};~Property(){PropVariantClear(&value);}} property;
    if(FAILED(properties->GetValue(PKEY_Device_FriendlyName,&property.value)) || property.value.vt!=VT_LPWSTR || !property.value.pwszVal)
        return L"未命名音频设备";
    std::wstring name=property.value.pwszVal;
    for(auto& character:name)if(character==L'\r' || character==L'\n' || character==L'\t')character=L' ';
    return name.empty()?L"未命名音频设备":name;
}
}
RecordingAudioDevices ListRecordingAudioDevices(bool loopback) {
    RecordingAudioDevices result;ComPtr<IMMDeviceEnumerator> enumerator;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)))) {
        result.error=L"无法读取音频设备，请检查 Windows 音频服务。";return result;
    }
    const EDataFlow flow=loopback?eRender:eCapture;
    ComPtr<IMMDevice> defaultDevice;
    if(SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow,eMultimedia,&defaultDevice))) {
        result.defaultId=DeviceId(defaultDevice.Get());result.defaultName=DeviceName(defaultDevice.Get());
    }
    ComPtr<IMMDeviceCollection> collection;UINT count{};
    if(FAILED(enumerator->EnumAudioEndpoints(flow,DEVICE_STATE_ACTIVE,&collection)) || FAILED(collection->GetCount(&count))) {
        result.error=L"无法读取设备列表，请稍后重新展开菜单。";return result;
    }
    for(UINT index=0;index<count;++index) {
        ComPtr<IMMDevice> device;if(FAILED(collection->Item(index,&device)))continue;
        auto id=DeviceId(device.Get());if(id.empty())continue;
        result.devices.push_back({std::move(id),DeviceName(device.Get())});
    }
    return result;
}
}
