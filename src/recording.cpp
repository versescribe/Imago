#include "recording.h"
#include "recording_audio.h"
#include "recording_video.h"
#include "recording_writer.h"
#include <array>
#include <atomic>
#include <mutex>
#include <thread>

namespace imago {
SIZE RecordingSize(const RecordingOptions& options) {
    const int width=options.region.right-options.region.left,height=options.region.bottom-options.region.top;
    if(width<2 || height<2)return {};
    const double scale=options.maxHeight?std::min({1.0,options.maxHeight*16.0/9/width,static_cast<double>(options.maxHeight)/height}):1;
    return {std::max(2,static_cast<int>(width*scale)&~1),std::max(2,static_cast<int>(height*scale)&~1)};
}
bool ValidateRecording(const RecordingOptions& options,std::wstring& error) {
    using GetVersion=LONG(WINAPI*)(OSVERSIONINFOW*);
    const auto versionFunction=reinterpret_cast<GetVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
    OSVERSIONINFOW version{sizeof(version)};
    if(!versionFunction || versionFunction(&version)!=0 || version.dwMajorVersion<10 || version.dwBuildNumber<19041) {
        error=L"录屏需要 Windows 10 2004 或更高版本。";return false;
    }
    const auto size=RecordingSize(options);
    if(size.cx<16 || size.cy<16){error=L"录屏区域至少需要 16 × 16 像素。";return false;}
    if(size.cx>8192 || size.cy>8192 || static_cast<std::int64_t>(size.cx)*size.cy>33554432){error=L"输出分辨率过大，请选择较低的分辨率。";return false;}
    if(options.fps!=15 && options.fps!=24 && options.fps!=30 && options.fps!=60){error=L"请选择有效的录屏帧率。";return false;}
    MONITORINFO info{sizeof(info)};const auto monitor=MonitorFromRect(&options.region,MONITOR_DEFAULTTONULL);
    RECT overlap{};
    if(!monitor || !GetMonitorInfoW(monitor,&info) || !IntersectRect(&overlap,&options.region,&info.rcMonitor) || !EqualRect(&overlap,&options.region)) {
        error=L"请在同一块显示器内选择录屏区域，当前暂不支持跨屏录制。";return false;
    }
    return true;
}
namespace {
struct MediaRuntime {
    // Delay-loaded imports keep screenshots usable when optional Windows media components are missing.
    struct Module {
        HMODULE handle{};
        explicit Module(const wchar_t* name):handle(LoadLibraryExW(name,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)) {
            if(!handle)throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()),L"缺少 Windows 媒体组件，请安装系统的媒体功能包后再录屏。");
        }
        ~Module(){FreeLibrary(handle);}
    } platform{L"mfplat.dll"},writer{L"mfreadwrite.dll"};
    MediaRuntime(){recording::Check(MFStartup(MF_VERSION,MFSTARTUP_FULL),L"无法启动 Windows 媒体组件，请检查媒体功能是否已安装。");}
    ~MediaRuntime(){MFShutdown();}
};
std::wstring RecordingError(const winrt::hresult_error& error) {
    wchar_t code[32]{};swprintf_s(code,L"（0x%08X）",static_cast<unsigned>(error.code()));
    return std::wstring(error.message().c_str())+code;
}
}
struct Recorder::Impl {
    mutable std::mutex mutex;
    RecordingStatus status;
    std::atomic<bool> stop{},paused{};
    std::shared_ptr<recording::Event> wake;
    std::jthread worker;

    void Publish(RecordingState state,LONGLONG elapsed,const std::wstring& path={},const std::wstring& error={}) {
        std::lock_guard lock(mutex);status.state=state;status.elapsed=elapsed;
        if(!path.empty())status.path=path;
        status.error=error;
    }
    void Run(const RecordingOptions& options,const std::wstring& path) noexcept {
        using namespace recording;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            struct Apartment {~Apartment(){winrt::uninit_apartment();}} apartment;
            MediaRuntime media;
            ScreenSource screen(options,wake);
            std::unique_ptr<AudioSource> computer,microphone;
            if(options.systemAudio)computer=std::make_unique<AudioSource>(true,options.systemAudioDevice);
            if(options.microphone)microphone=std::make_unique<AudioSource>(false,options.microphoneDevice);
            VideoWriter writer(options,path);
            const auto waiting=Clock();
            while(!screen.Update()) {
                if(stop.load()){Publish(RecordingState::Cancelled,0);return;}
                if(Clock()-waiting>5*Second)throw winrt::hresult_error(E_FAIL,L"未收到屏幕画面，请检查采集权限和显示器状态。");
                WaitForSingleObject(wake->handle,20);
            }
            if(stop.load()){Publish(RecordingState::Cancelled,0);return;}
            const LONGLONG origin=Clock();
            LONGLONG pausedTime=0,pauseStart=0,elapsed=0,lastIndex=-1,audioFrame=0,pendingTime=0;
            bool isPaused=false;
            ComPtr<IMFMediaBuffer> pending;
            const bool audio=computer || microphone;
            auto startAudio=[&]{if(computer)computer->Start();if(microphone)microphone->Start();};
            auto drainAudio=[&](LONGLONG now){if(computer)computer->Drain(origin,pausedTime,now);if(microphone)microphone->Drain(origin,pausedTime,now);};
            auto writeAudio=[&](LONGLONG until) {
                if(!audio)return;
                const LONGLONG end=std::max<LONGLONG>(0,until)*AudioRate/Second;
                while(audioFrame<end) {
                    const int frames=static_cast<int>(std::min<LONGLONG>(AudioBlock,end-audioFrame));
                    std::array<int,AudioBlock*AudioChannels> mixed{};
                    auto block=std::span(mixed.data(),static_cast<std::size_t>(frames)*AudioChannels);
                    if(computer)computer->Mix(audioFrame,block);if(microphone)microphone->Mix(audioFrame,block);
                    std::array<std::int16_t,AudioBlock*AudioChannels> pcm{};
                    const int gain=computer && microphone?2:1;
                    for(std::size_t i=0;i<block.size();++i)pcm[i]=static_cast<std::int16_t>(std::clamp(mixed[i]/gain,-32768,32767));
                    writer.Audio({pcm.data(),block.size()},audioFrame);audioFrame+=frames;
                }
            };
            auto flushVideo=[&](LONGLONG end) {
                if(pending){writer.Video(pending.Get(),pendingTime,std::max<LONGLONG>(1,end-pendingTime));pending.Reset();}
            };
            startAudio();Publish(RecordingState::Recording,0,path);
            std::wstring failure;
            try {
                for(;;) {
                    const auto now=Clock();elapsed=(isPaused?pauseStart:now)-origin-pausedTime;
                    if(stop.load())break;
                    const bool requested=paused.load();
                    if(requested!=isPaused) {
                        if(requested) {
                            drainAudio(now);flushVideo(elapsed);writeAudio(elapsed);
                            if(computer)computer->Suspend();if(microphone)microphone->Suspend();
                            pauseStart=now;isPaused=true;
                        }else {
                            pausedTime+=now-pauseStart;isPaused=false;startAudio();
                        }
                    }
                    screen.Update(); // Drain the two-frame pool even while paused.
                    elapsed=(isPaused?pauseStart:Clock())-origin-pausedTime;
                    if(!isPaused) {
                        drainAudio(Clock());
                        const LONGLONG index=elapsed*options.fps/Second;
                        if(index>lastIndex) {
                            // Skip overdue frames instead of retaining an unbounded backlog.
                            const LONGLONG timestamp=std::max(pendingTime,index*Second/options.fps);
                            flushVideo(timestamp);pending=screen.VideoBuffer();pendingTime=timestamp;lastIndex=index;
                        }
                        writeAudio(elapsed-Second/10); // Allow separate audio devices 100 ms to deliver packets.
                    }
                    Publish(isPaused?RecordingState::Paused:RecordingState::Recording,elapsed,path);
                    WaitForSingleObject(wake->handle,isPaused?40:5);
                }
            }catch(const winrt::hresult_error& error){failure=RecordingError(error);}
            catch(...){failure=L"系统资源不足，录屏已停止。";}
            screen.Stop();
            if(!isPaused && failure.empty()) {
                try{drainAudio(Clock());}catch(const winrt::hresult_error& error){failure=RecordingError(error);}
                catch(...){failure=L"系统资源不足，音频末尾可能不完整。";}
            }
            if(computer)computer->Stop();if(microphone)microphone->Stop();
            Publish(RecordingState::Finalizing,elapsed,path);
            try {
                flushVideo(elapsed);writeAudio(elapsed);
                if(!writer.HasVideo()){Publish(RecordingState::Cancelled,0);return;}
                writer.Finish();Publish(RecordingState::Completed,elapsed,path,failure);
            }catch(const winrt::hresult_error& error) {
                const auto partial=writer.Preserve();
                Publish(RecordingState::Failed,elapsed,partial,RecordingError(error)+L" 临时录像已保留，文件可能不完整。");
            }catch(...) {
                const auto partial=writer.Preserve();
                Publish(RecordingState::Failed,elapsed,partial,L"系统资源不足，无法完成录像收尾。临时文件已保留，可能不完整。");
            }
        }catch(const winrt::hresult_error& error){Publish(RecordingState::Failed,0,{},RecordingError(error));}
        catch(...){Publish(RecordingState::Failed,0,{},L"无法开始录屏：内存或系统资源不足。");}
    }
};
Recorder::Recorder():impl_(std::make_unique<Impl>()){}
Recorder::~Recorder(){Stop();Join();}
bool Recorder::Start(const RecordingOptions& options,const std::wstring& path,std::wstring& error) {
    const auto state=Status().state;
    if(state==RecordingState::Starting || state==RecordingState::Recording || state==RecordingState::Paused || state==RecordingState::Finalizing) {
        error=L"已有录屏正在进行。";return false;
    }
    if(!ValidateRecording(options,error))return false;
    Join();impl_->stop=false;impl_->paused=false;
    try {
        impl_->wake=std::make_shared<recording::Event>();
        impl_->Publish(RecordingState::Starting,0,path);
        impl_->worker=std::jthread([state=impl_.get(),options,path]{state->Run(options,path);});
        return true;
    }catch(...){impl_->Publish(RecordingState::Failed,0);error=L"无法启动录屏工作线程。";return false;}
}
void Recorder::SetPaused(bool paused){impl_->paused=paused;if(impl_->wake)SetEvent(impl_->wake->handle);}
void Recorder::Stop(){impl_->stop=true;if(impl_->wake)SetEvent(impl_->wake->handle);}
void Recorder::Join(){if(impl_->worker.joinable())impl_->worker.join();}
RecordingStatus Recorder::Status() const {std::lock_guard lock(impl_->mutex);return impl_->status;}
}
