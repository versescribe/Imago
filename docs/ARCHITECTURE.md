# Imago 架构

构建版本统一读取根目录 `VERSION`，生成 `version.h`、Windows 版本资源与应用清单。
只有 Windows 系统库依赖；C++/WinRT 使用 Windows SDK 自带头文件。

- `src/main.cpp`：消息循环、托盘、主窗口操作卡片、贴图和全局快捷键。
- `src/updates.cpp`、`src/update_version.h`、`src/updates_ui.inl`：WinHTTP 后台请求、系统 JSON 解析、严格稳定版本比较、下载包就绪判断、启动检查限频及原生结果对话框。网络响应不能决定打开任意 URL；下载页固定为官方仓库。
- `src/capture_ui.inl`：截图选区、工具栏、标注交互及原生文字编辑框，包含在主应用命名空间中。
- `src/capture_extras.inl`、`src/capture_helpers.cpp`：窗口边界快照、窗口吸附、像素取色和放大预览。
- `src/settings.cpp`、`src/settings_ui.inl`：设置分组页面、配色预览、快捷键注册切换、设置文件读写，以及当前用户开机自启的注册与失败恢复。
- `src/appearance.cpp`：系统／自定义强调色、深浅色与高对比度适配、共享按钮配色与焦点环、窗口外观和短时交互动画。仅依赖 Windows 系统能力。
- `src/native_resources.h`：不可复制的 GDI 资源所有权、字体自动释放、绘图对象选择与 DC 状态恢复。
- `src/ocr.cpp`、`src/ocr_ui.inl`：Windows OCR 后台任务、取消、尺寸适配、行文本提取及截图／贴图中的复制交互。
- `src/scroll_capture.cpp`：画面稳定检测和垂直重叠匹配。
- `src/scroll_controller.cpp`：独立的滚动采集控制器。模式、采样阶段和暂停原因分别使用枚举，通过事件方法转换；调用方提供时间与图像，控制器不操作窗口或注入输入。
- `src/scroll_ui.inl`：目标窗口验证、输入投递、采样与原生长图预览，消费控制器的状态和图像更新。
- `src/recording.cpp`：录屏线程、暂停时间轴、音画时间戳、有限缓冲和完成状态；`recording_video.cpp` 使用系统 WGC 采集，D3D11 裁剪／缩放并转换 NV12；`recording_audio.cpp` 使用 WASAPI 采集与混音；`recording_writer.cpp` 使用 Media Foundation 流式编码 MP4。
- `src/recording_panel.cpp`、`src/recording_ui.inl`：复用截图矢量图标的录屏工具栏、原生主题菜单，以及截图选区、保存对话框和应用生命周期的衔接。
- `src/recording_border.cpp`：与实际选区一致的鼠标穿透边框，使用系统采集排除能力，不进入录像。
- `src/recording_files.cpp`：录像暂存路径、文件剪贴板及可取消的后台文件导出，保留已复制文件的有效期；不在关闭界面时删除缓存录像。
- `src/recording_devices.cpp`：使用系统 [MMDevice API](https://learn.microsoft.com/en-us/windows/win32/coreaudio/enumerating-audio-devices) 枚举可用音频端点及名称，采集线程根据选中的设备 ID 打开 WASAPI，不持有 UI 线程的设备对象。
- `src/toolbar.cpp`：Direct2D 矢量图标和按钮状态绘制，不依赖图标字体、图片资源或第三方 UI 库。
- `src/annotations.cpp`：标注对象、命中检测、撤销／重做和预览／导出共用绘制逻辑。
- `src/png.cpp`：系统保存对话框及 WIC PNG 编码；完整写入临时文件后替换目标文件。
- `src/file_io.cpp`：设置和 PNG 共用的原子文件写入；临时文件与目标位于同一目录，失败时只清理本次成功创建的临时文件。
- `src/image.cpp`：桌面捕获、裁剪、标准 Windows 剪贴板位图编解码。
- `tests/core_tests.cpp`：已有选区边界、裁剪、位图格式、透明度及畸形剪贴板输入测试。
- 系统标题栏、原生菜单和 HWND 按钮；紧凑工具栏使用 Windows 字体、间距、圆角和强调色风格。跟随系统深浅色及高对比度，截图像素保持原色。
- Per-Monitor V2 DPI 感知；虚拟桌面使用物理像素坐标，支持负坐标显示器。
- 空闲时使用阻塞消息循环，无轮询截图、后台定时重绘。更新检查仅在启动达到检查间隔或手动触发时请求 GitHub，网络操作在后台执行。OCR 仅在用户触发时调用系统识别器；等待使用系统事件，不轮询识别状态。Explorer 重启后重新注册托盘图标。
- 撤销历史只保存对象数据；每个历史栈最多 100 步，目标上限约 8 MiB，至少保留一份快照。每张截图最多 512 个标注，单个文字对象最多 4096 个 UTF-16 代码单元。
- 马赛克按类型和颗粒／强度缓存缩小后的截图，模糊使用自带 C++ 像素处理，未增加图像库依赖；缓存独立于撤销历史，并与本次截图一起释放。
