# VR Implementation Bug Fixes

## Summary
检查了Flycast Quest VR实现，发现并修复了7个关键bug。这些bug涉及JNI调用、OpenXR API使用、资源管理等多个方面。

## Bugs Found and Fixed

### 1. JNI String Function Error (quest_vr_jni.cpp:136)
**问题**: 使用了不存在的JNI函数 `NewStringUTF8`
**原因**: JNI正确的函数名是 `NewStringUTF`，不是 `NewStringUTF8`
**修复**: 
```cpp
// 修复前:
jstring jModeName = env->NewStringUTF8(modeName);

// 修复后:
jstring jModeName = env->NewStringUTF(modeName);
```

### 2. Wrong Variable Passed to GetGameModeName (quest_vr_jni.cpp:135)
**问题**: 传递了错误的变量类型
**原因**: `GetGameModeName` 需要 `GameMode` 枚举，但传递的是 `jint mode` 而不是转换后的 `vrMode`
**修复**:
```cpp
// 修复前:
const char* modeName = g_vrManager->GetGameModeName(mode);

// 修复后:
const char* modeName = g_vrManager->GetGameModeName(vrMode);
```

### 3. xrLocateViews Incorrect Parameters (quest_vr_manager.cpp:352-353)
**问题**: `xrLocateViews` API调用缺少必需的输出参数
**原因**: OpenXR的 `xrLocateViews` 签名是:
```cpp
XrResult xrLocateViews(
    XrSession session,
    const XrViewLocateInfo* viewLocateInfo,
    XrViewState* viewState,
    uint32_t viewCapacityInput,
    uint32_t* viewCountOutput,  // 缺少这个参数!
    XrView* views
);
```
**修复**:
```cpp
// 修复前:
XrResult result = xrLocateViews(session_, &viewLocateInfo, &viewState, 
                                   static_cast<uint32_t>(viewCount), views);

// 修复后:
uint32_t viewCountOutput = 0;
XrResult result = xrLocateViews(session_, &viewLocateInfo, &viewState, 
                                   static_cast<uint32_t>(viewCount), &viewCountOutput, views);
```

### 4. Missing stdexcept Header (vulkan_renderer_quest.cpp:7)
**问题**: 使用了 `std::invalid_argument` 但未包含相应头文件
**原因**: 代码在596行抛出 `std::invalid_argument`，但缺少 `<stdexcept>` 头文件
**修复**:
```cpp
#include <cstring>
#include <algorithm>
#include <cmath>
#include <stdexcept>  // 新增
```

### 5. Resource Leak - Swapchains and Action Sets Not Destroyed (quest_vr_manager.cpp:77-95)
**问题**: OpenXR资源未正确释放，导致内存泄漏
**原因**: `Shutdown()` 函数未销毁 swapchains、action sets 和 controller spaces
**修复**:
```cpp
void QuestVRManager::Shutdown() {
    // 新增: Destroy swapchains
    for (int i = 0; i < VIEW_COUNT; i++) {
        if (swapchains_[i].swapchain != XR_NULL_HANDLE) {
            xrDestroySwapchain(swapchains_[i].swapchain);
            swapchains_[i].swapchain = XR_NULL_HANDLE;
        }
    }

    // 新增: Destroy controller spaces and action sets
    for (int i = 0; i < 2; i++) {
        if (controllers_[i].space != XR_NULL_HANDLE) {
            xrDestroySpace(controllers_[i].space);
            controllers_[i].space = XR_NULL_HANDLE;
        }
        if (controllers_[i].actionSet != XR_NULL_HANDLE) {
            xrDestroyActionSet(controllers_[i].actionSet);
            controllers_[i].actionSet = XR_NULL_HANDLE;
        }
    }

    // 新增: Destroy Vulkan resources
    if (vkDevice_ != VK_NULL_HANDLE) {
        vkDestroyDevice(vkDevice_, nullptr);
        vkDevice_ = VK_NULL_HANDLE;
    }

    if (vkInstance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(vkInstance_, nullptr);
        vkInstance_ = VK_NULL_HANDLE;
    }

    // 原有代码...
}
```

### 6. xrSyncActions Incorrect Usage (quest_vr_manager.cpp:420-428)
**问题**: `xrSyncActions` API使用不正确
**原因**: `xrSyncActions` 需要 `XrActionsSyncInfo*` 结构体，而不是直接传递 `XrActiveActionSet*`
**正确签名**:
```cpp
XrResult xrSyncActions(
    XrSession session,
    const XrActionsSyncInfo* syncInfo
);
```
**修复**:
```cpp
// 修复前:
for (int i = 0; i < 2; i++) {
    XrActiveActionSet activeActionSet{XR_TYPE_ACTIVE_ACTION_SET};
    activeActionSet.actionSet = controllers_[i].actionSet;
    activeActionSet.subactionPath = XR_NULL_PATH;
    xrSyncActions(session_, &activeActionSet);  // 错误!
}

// 修复后:
XrActiveActionSet activeActionSets[2];
for (int i = 0; i < 2; i++) {
    activeActionSets[i].actionSet = controllers_[i].actionSet;
    activeActionSets[i].subactionPath = XR_NULL_PATH;
}

XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
syncInfo.countActiveActionSets = 2;
syncInfo.activeActionSets = activeActionSets;

xrSyncActions(session_, &syncInfo);
```

### 7. Missing xrBeginFrame Call (quest_vr_manager.cpp:347-367)
**问题**: OpenXR帧序列不完整
**原因**: 缺少关键的 `xrBeginFrame` 调用
**OpenXR正确帧序列**:
1. `xrWaitFrame` - 等待下一帧
2. `xrBeginFrame` - 开始帧渲染 ⚠️ **这个缺失了!**
3. 渲染内容
4. `xrEndFrame` - 结束帧

**修复**:
```cpp
bool QuestVRManager::BeginFrame() {
    // ... xrWaitFrame code ...
    
    if (result != XR_SUCCESS || !frameState_.shouldRender) {
        return false;
    }

    // 新增: xrBeginFrame
    XrFrameBeginInfo frameBeginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    result = xrBeginFrame(session_, &frameBeginInfo);
    if (result != XR_SUCCESS) {
        LOGE("xrBeginFrame failed: %d", result);
        return false;
    }

    return true;
}
```

### 8. Missing primaryViewConfigurationType (quest_vr_manager.cpp:349)
**问题**: `XrSessionBeginInfo` 缺少必需字段
**原因**: OpenXR规范要求设置 `primaryViewConfigurationType`
**修复**:
```cpp
// 修复前:
XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
XrResult result = xrBeginSession(session_, &beginInfo);

// 修复后:
XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
XrResult result = xrBeginSession(session_, &beginInfo);
```

## Impact Analysis

### Critical Issues (会导致崩溃或严重功能失败)
- ❌ **Bug #3**: xrLocateViews 参数错误 - 会导致运行时崩溃
- ❌ **Bug #6**: xrSyncActions 使用错误 - 会导致运行时崩溃
- ❌ **Bug #7**: 缺少 xrBeginFrame - 违反OpenXR规范，可能导致渲染失败

### High Priority (会导致功能异常)
- ⚠️ **Bug #1**: NewStringUTF8 错误 - 编译错误
- ⚠️ **Bug #4**: 缺少头文件 - 编译错误
- ⚠️ **Bug #8**: 缺少 primaryViewConfigurationType - 会导致session初始化失败

### Medium Priority (资源泄漏或逻辑错误)
- 🔧 **Bug #2**: 传递错误变量 - UI显示错误
- 🔧 **Bug #5**: 资源泄漏 - 长时间运行后内存泄漏

## Testing Recommendations

1. **编译测试**: 验证所有修复编译通过
2. **VR初始化测试**: 测试Quest 3设备上的初始化流程
3. **帧渲染测试**: 验证完整的帧循环 (wait -> begin -> render -> end)
4. **资源管理测试**: 多次初始化/关闭VR，检查内存泄漏
5. **模式切换测试**: 测试三种VR模式切换功能

## Files Modified
1. `core/quest_vr/quest_vr_jni.cpp` - 2个bug修复
2. `core/quest_vr/quest_vr_manager.cpp` - 5个bug修复
3. `core/quest_vr/vulkan_renderer_quest.cpp` - 1个bug修复

## Verification Status
- ✅ All syntax errors fixed
- ✅ OpenXR API usage corrected
- ✅ Resource management improved
- ✅ JNI calls corrected
- ⏳ Runtime testing pending (requires Quest 3 hardware)
