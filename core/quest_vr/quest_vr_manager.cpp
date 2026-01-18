#ifdef USE_VULKAN

#include "quest_vr_manager.h"
#include "rend/vulkan/vulkan_context.h"
#include <algorithm>
#include <cstring>
#include <cmath>

#define LOG_TAG "QuestVR"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace QuestVR {

static const char* REQUIRED_EXTENSIONS[] = {
    XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
    XR_KHR_VULKAN_ENABLE_EXTENSION_NAME,
    XR_META_PERFORMANCE_METRICS_EXTENSION_NAME,
    XR_FB_PASSTHROUGH_EXTENSION_NAME,
    nullptr
};

QuestVRManager::QuestVRManager()
    : instance_(XR_NULL_HANDLE)
    , session_(XR_NULL_HANDLE)
    , space_(XR_NULL_HANDLE)
    , systemId_(XR_NULL_SYSTEM_ID)
    , sessionRunning_(false)
    , sessionFocused_(false)
    , currentMode_(GameMode::CINEMA)  // 默认影院模式
    , vkInstance_(VK_NULL_HANDLE)
    , vkPhysicalDevice_(VK_NULL_HANDLE)
    , vkDevice_(VK_NULL_HANDLE)
    , vkQueue_(VK_NULL_HANDLE)
    , vkQueueFamilyIndex_(0)
    , frameState_{XR_TYPE_FRAME_STATE}
{
    memset(controllers_, 0, sizeof(controllers_));
}

QuestVRManager::~QuestVRManager() {
    Shutdown();
}

bool QuestVRManager::Initialize() {
    if (!CreateInstance()) {
        LOGE("Failed to create OpenXR instance");
        return false;
    }

    if (!CreateSession()) {
        LOGE("Failed to create OpenXR session");
        return false;
    }

    if (!CreateSwapchains()) {
        LOGE("Failed to create swapchains");
        return false;
    }

    if (!CreateActionSets()) {
        LOGE("Failed to create action sets");
        return false;
    }

    if (!CreateSpaces()) {
        LOGE("Failed to create spaces");
        return false;
    }

    LOGI("Quest 3 VR initialized successfully");
    LOGI("Initial game mode: %s", GetGameModeName(currentMode_));
    return true;
}

void QuestVRManager::Shutdown() {
    if (session_ != XR_NULL_HANDLE) {
        xrDestroySession(session_);
        session_ = XR_NULL_HANDLE;
    }

    if (space_ != XR_NULL_HANDLE) {
        xrDestroySpace(space_);
        space_ = XR_NULL_HANDLE;
    }

    if (instance_ != XR_NULL_HANDLE) {
        xrDestroyInstance(instance_);
        instance_ = XR_NULL_HANDLE;
    }

    sessionRunning_ = false;
    sessionFocused_ = false;
}

// ========== 模式切换实现 ==========

void QuestVRManager::SetGameMode(GameMode mode) {
    if (mode < GameMode::CINEMA || mode > GameMode::IMMERSIVE) {
        LOGE("Invalid game mode: %d", static_cast<int>(mode));
        return;
    }

    if (currentMode_ == mode) {
        LOGI("Game mode already set to: %s", GetGameModeName(mode));
        return;
    }

    currentMode_ = mode;
    LOGI("Game mode changed to: %s", GetGameModeName(mode));
    
    // 根据不同模式设置参数
    switch (mode) {
        case GameMode::CINEMA:
            LOGI("Switched to CINEMA mode - virtual screen at 2.5m");
            break;
            
        case GameMode::ARCADE:
            LOGI("Switched to ARCADE mode - cabinet at 1.5m");
            break;
            
        case GameMode::IMMERSIVE:
            LOGI("Switched to IMMERSIVE mode - full 6DOF");
            break;
    }
}

void QuestVRManager::CycleGameMode() {
    // 循环切换模式：CINEMA → ARCADE → IMMERSIVE → CINEMA
    GameMode nextMode;
    switch (currentMode_) {
        case GameMode::CINEMA:
            nextMode = GameMode::ARCADE;
            break;
        case GameMode::ARCADE:
            nextMode = GameMode::IMMERSIVE;
            break;
        case GameMode::IMMERSIVE:
            nextMode = GameMode::CINEMA;
            break;
    }
    
    SetGameMode(nextMode);
    LOGI("Cycled to: %s", GetGameModeName(nextMode));
}

const char* QuestVRManager::GetGameModeName(GameMode mode) const {
    switch (mode) {
        case GameMode::CINEMA:
            return "Cinema Mode (影院模式)";
        case GameMode::ARCADE:
            return "Arcade Mode (机台模式)";
        case GameMode::IMMERSIVE:
            return "Immersive Mode (沉浸模式)";
        default:
            return "Unknown";
    }
}

// ========== 计算游戏模式的虚拟位置 ==========

XrPosef QuestVRManager::CalculateGameModePose(GameMode mode, const XrPosef& headPose) const {
    XrPosef result = headPose;

    switch (mode) {
        case GameMode::CINEMA:
            // 🎬 虚拟影院模式
            // 在用户正前方 2.5米处放置虚拟屏幕
            result.position.z -= 2.5f;
            result.position.y -= 0.2f;
            result.orientation = headPose.orientation;
            break;

        case GameMode::ARCADE:
            // 🕹️ 虚拟机台模式
            // 1.5米距离，1米高度，模拟街机柜
            result.position.z -= 1.5f;
            result.position.y -= 1.0f;
            result.orientation = headPose.orientation;
            break;

        case GameMode::IMMERSIVE:
            // 🌍 全沉浸 6DOF 模式
            // 完全跟随头部，无需调整位置
            result = headPose;
            break;
    }

    return result;
}

// ========== OpenXR 初始化（简化版）==========

bool QuestVRManager::CreateInstance() {
    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    strcpy(createInfo.applicationInfo.applicationName, "Flycast Quest 3 VR");
    strcpy(createInfo.applicationInfo.engineName, "Flycast VR Engine");

    // Extensions
    std::vector<const char*> enabledExtensions;
    for (int i = 0; REQUIRED_EXTENSIONS[i] != nullptr; i++) {
        enabledExtensions.push_back(REQUIRED_EXTENSIONS[i]);
    }

    createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    createInfo.enabledExtensionNames = enabledExtensions.data();

    // Android platform specific
#ifdef __ANDROID__
    XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    androidInfo.applicationVM = nullptr;
    androidInfo.applicationActivity = nullptr;
    createInfo.next = &androidInfo;
#endif

    XrResult result = xrCreateInstance(&createInfo, &instance_);
    if (result != XR_SUCCESS) {
        LOGE("xrCreateInstance failed: %d", result);
        return false;
    }

    return true;
}

bool QuestVRManager::CreateSession() {
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrResult result = xrGetSystem(instance_, &systemInfo, &systemId_);
    if (result != XR_SUCCESS) {
        LOGE("xrGetSystem failed: %d", result);
        return false;
    }

    // Get system properties
    XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
    result = xrGetSystemProperties(instance_, systemId_, &systemProperties);
    
    LOGI("System properties loaded");

    // Create session (simplified - no graphics binding here)
    XrSessionCreateInfo sessionCreateInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionCreateInfo.systemId = systemId_;

    result = xrCreateSession(instance_, &sessionCreateInfo, &session_);
    if (result != XR_SUCCESS) {
        LOGE("xrCreateSession failed: %d", result);
        return false;
    }

    return true;
}

bool QuestVRManager::CreateSwapchains() {
    // Simplified swapchain creation
    for (int eye = 0; eye < VIEW_COUNT; eye++) {
        XrSwapchainCreateInfo swapchainCreateInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        swapchainCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        swapchainCreateInfo.sampleCount = VK_SAMPLE_COUNT_4_BIT;
        swapchainCreateInfo.width = 2064;  // Quest 3 native resolution
        swapchainCreateInfo.height = 2096;
        swapchainCreateInfo.faceCount = 1;
        swapchainCreateInfo.arraySize = 1;
        swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                                         XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;

        XrResult result = xrCreateSwapchain(session_, &swapchainCreateInfo, &swapchains_[eye].swapchain);
        if (result != XR_SUCCESS) {
            LOGE("xrCreateSwapchain failed for eye %d: %d", eye, result);
            return false;
        }

        swapchains_[eye].width = 2064;
        swapchains_[eye].height = 2096;
    }

    return true;
}

bool QuestVRManager::CreateActionSets() {
    // Create action sets for each controller
    for (int i = 0; i < 2; i++) {
        XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
        sprintf(actionSetInfo.actionSetName, "controller%d_action_set", i);
        sprintf(actionSetInfo.localizedActionSetName, "Controller %d Action Set", i);
        actionSetInfo.priority = 0;

        XrResult result = xrCreateActionSet(instance_, &actionSetInfo, &controllers_[i].actionSet);
        if (result != XR_SUCCESS) {
            LOGE("xrCreateActionSet failed: %d", result);
            return false;
        }
    }

    return true;
}

bool QuestVRManager::CreateSpaces() {
    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;

    XrResult result = xrCreateReferenceSpace(session_, &spaceInfo, &space_);
    if (result != XR_SUCCESS) {
        LOGE("xrCreateReferenceSpace failed: %d", result);
        return false;
    }

    return true;
}

// ========== Frame management ==========

bool QuestVRManager::BeginFrame() {
    if (!sessionRunning_) {
        XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
        XrResult result = xrBeginSession(session_, &beginInfo);
        if (result != XR_SUCCESS) {
            LOGE("xrBeginSession failed: %d", result);
            return false;
        }
        sessionRunning_ = true;
    }

    XrFrameWaitInfo frameWaitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrResult result = xrWaitFrame(session_, &frameWaitInfo, &frameState_);

    if (result != XR_SUCCESS || !frameState_.shouldRender) {
        return false;
    }

    return true;
}

void QuestVRManager::EndFrame() {
    XrFrameEndInfo frameEndInfo{XR_TYPE_FRAME_END_INFO};
    frameEndInfo.displayTime = frameState_.predictedDisplayTime;
    frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

    xrEndFrame(session_, &frameEndInfo);
}

void QuestVRManager::GetViewTransforms(int viewCount, XrView* views) {
    XrViewState viewState{XR_TYPE_VIEW_STATE};
    XrViewLocateInfo viewLocateInfo{XR_TYPE_VIEW_LOCATE_INFO};
    viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    viewLocateInfo.displayTime = frameState_.predictedDisplayTime;
    viewLocateInfo.space = space_;

    XrResult result = xrLocateViews(session_, &viewLocateInfo, &viewState, 
                                       static_cast<uint32_t>(viewCount), views);
    if (result != XR_SUCCESS) {
        LOGE("xrLocateViews failed: %d", result);
        return;
    }

    // Apply game mode transform to views
    for (int i = 0; i < viewCount; i++) {
        views[i].pose = CalculateGameModePose(currentMode_, views[i].pose);
    }
}

XrSwapchain QuestVRManager::GetSwapchain(int eye) {
    if (eye >= 0 && eye < VIEW_COUNT) {
        return swapchains_[eye].swapchain;
    }
    return XR_NULL_HANDLE;
}

uint32_t QuestVRManager::GetSwapchainWidth(int eye) const {
    if (eye >= 0 && eye < VIEW_COUNT) {
        return swapchains_[eye].width;
    }
    return 0;
}

uint32_t QuestVRManager::GetSwapchainHeight(int eye) const {
    if (eye >= 0 && eye < VIEW_COUNT) {
        return swapchains_[eye].height;
    }
    return 0;
}

// ========== Input handling ==========

void QuestVRManager::UpdateInput() {
    // Update controller states (simplified)
    for (int i = 0; i < 2; i++) {
        XrActiveActionSet activeActionSet{XR_TYPE_ACTIVE_ACTION_SET};
        activeActionSet.actionSet = controllers_[i].actionSet;
        activeActionSet.subactionPath = XR_NULL_PATH;

        xrSyncActions(session_, &activeActionSet);
    }
}

const ControllerState& QuestVRManager::GetControllerState(int index) const {
    if (index >= 0 && index < 2) {
        return controllers_[index];
    }
    static ControllerState empty{};
    return empty;
}

// ========== Dreamcast 控制器映射 ==========

QuestVRManager::DreamcastInput QuestVRManager::GetDreamcastInput() const {
    DreamcastInput dcInput{};

    // Map right controller (primary)
    const ControllerState& right = controllers_[1];
    const ControllerState& left = controllers_[0];

    // Button mapping
    dcInput.a = right.triggerValue > 0.5f;
    dcInput.b = right.gripValue > 0.5f;
    dcInput.x = right.buttonAPressed;
    dcInput.y = right.buttonBPressed;
    dcInput.start = right.menuPressed;

    // D-pad from right thumbstick
    dcInput.dPadUp = right.thumbstickY > 0.7f;
    dcInput.dPadDown = right.thumbstickY < -0.7f;
    dcInput.dPadLeft = right.thumbstickX < -0.7f;
    dcInput.dPadRight = right.thumbstickX > 0.7f;

    // Analog from right thumbstick
    dcInput.analogX = right.thumbstickX;
    dcInput.analogY = right.thumbstickY;

    // Triggers
    dcInput.lt = left.triggerValue;
    dcInput.rt = right.triggerValue;

    return dcInput;
}

// ========== Vulkan 初始化占位 ==========

bool QuestVRManager::CreateVulkanInstance() {
    // Placeholder - would need actual Vulkan context
    return true;
}

bool QuestVRManager::CreateVulkanDevice() {
    // Placeholder
    return true;
}

bool QuestVRManager::CreateVulkanSwapchainImages() {
    // Placeholder
    return true;
}

} // namespace QuestVR

#endif // USE_VULKAN
