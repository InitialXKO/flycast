#ifdef USE_VULKAN

#include "quest_vr_manager.h"
#include "rend/vulkan/vulkan_context.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <android/log.h>

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
    // Destroy swapchains
    for (int i = 0; i < VIEW_COUNT; i++) {
        if (swapchains_[i].swapchain != XR_NULL_HANDLE) {
            xrDestroySwapchain(swapchains_[i].swapchain);
            swapchains_[i].swapchain = XR_NULL_HANDLE;
        }
    }

    // Destroy controller spaces and action sets
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

    if (space_ != XR_NULL_HANDLE) {
        xrDestroySpace(space_);
        space_ = XR_NULL_HANDLE;
    }

    if (session_ != XR_NULL_HANDLE) {
        xrDestroySession(session_);
        session_ = XR_NULL_HANDLE;
    }

    if (instance_ != XR_NULL_HANDLE) {
        xrDestroyInstance(instance_);
        instance_ = XR_NULL_HANDLE;
    }

    // Destroy Vulkan resources
    // TODO: Re-enable when Vulkan initialization is implemented
    // if (vkDevice_ != VK_NULL_HANDLE) {
    //     vkDestroyDevice(vkDevice_, nullptr);
    //     vkDevice_ = VK_NULL_HANDLE;
    // }
    //
    // if (vkInstance_ != VK_NULL_HANDLE) {
    //     vkDestroyInstance(vkInstance_, nullptr);
    //     vkInstance_ = VK_NULL_HANDLE;
    // }

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
        beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
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

    XrFrameBeginInfo frameBeginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    result = xrBeginFrame(session_, &frameBeginInfo);
    if (result != XR_SUCCESS) {
        LOGE("xrBeginFrame failed: %d", result);
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

    uint32_t viewCountOutput = 0;
    XrResult result = xrLocateViews(session_, &viewLocateInfo, &viewState, 
                                       static_cast<uint32_t>(viewCount), &viewCountOutput, views);
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
    XrActiveActionSet activeActionSets[2];
    for (int i = 0; i < 2; i++) {
        activeActionSets[i].actionSet = controllers_[i].actionSet;
        activeActionSets[i].subactionPath = XR_NULL_PATH;
    }

    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 2;
    syncInfo.activeActionSets = activeActionSets;

    xrSyncActions(session_, &syncInfo);
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

// ========== Vulkan 初始化 ==========

bool QuestVRManager::CreateVulkanInstance() {
    PFN_xrGetVulkanGraphicsRequirementsKHR xrGetVulkanGraphicsRequirementsKHR = nullptr;
    xrGetInstanceProcAddr(instance_, "xrGetVulkanGraphicsRequirementsKHR",
                         (PFN_xrVoidFunction*)&xrGetVulkanGraphicsRequirementsKHR);

    if (!xrGetVulkanGraphicsRequirementsKHR) {
        LOGE("xrGetVulkanGraphicsRequirementsKHR not available");
        return false;
    }

    XrGraphicsRequirementsVulkanKHR graphicsRequirements{};
    graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR;
    XrResult result = xrGetVulkanGraphicsRequirementsKHR(instance_, systemId_, &graphicsRequirements);
    if (result != XR_SUCCESS) {
        LOGE("Failed to get Vulkan graphics requirements: %d", result);
        return false;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Flycast Quest 3 VR";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Flycast VR Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = graphicsRequirements.maxApiVersionSupported;

    VkInstanceCreateInfo instanceCreateInfo{};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pApplicationInfo = &appInfo;

    PFN_xrGetVulkanInstanceExtensionsKHR xrGetVulkanInstanceExtensionsKHR = nullptr;
    xrGetInstanceProcAddr(instance_, "xrGetVulkanInstanceExtensionsKHR",
                         (PFN_xrVoidFunction*)&xrGetVulkanInstanceExtensionsKHR);

    if (xrGetVulkanInstanceExtensionsKHR) {
        uint32_t extensionCount = 0;
        result = xrGetVulkanInstanceExtensionsKHR(instance_, systemId_, 0, &extensionCount, nullptr);
        if (result == XR_SUCCESS) {
            std::vector<char> extensions(extensionCount * 256);
            result = xrGetVulkanInstanceExtensionsKHR(instance_, systemId_, extensionCount * 256,
                                                     &extensionCount, extensions.data());
            if (result == XR_SUCCESS) {
                instanceCreateInfo.enabledExtensionCount = 0;
                instanceCreateInfo.ppEnabledExtensionNames = nullptr;
            }
        }
    }

    // TODO: Use xrCreateVulkanInstanceKHR instead of direct Vulkan calls
    // VkResult vkResult = vkCreateInstance(&instanceCreateInfo, nullptr, &vkInstance_);
    // if (vkResult != VK_SUCCESS) {
    //     LOGE("Failed to create Vulkan instance: %d", vkResult);
    //     return false;
    // }

    LOGE("CreateVulkanInstance not fully implemented - use existing Vulkan context");
    return true;
}

bool QuestVRManager::CreateVulkanDevice() {
    PFN_xrGetVulkanGraphicsDeviceKHR xrGetVulkanGraphicsDeviceKHR = nullptr;
    xrGetInstanceProcAddr(instance_, "xrGetVulkanGraphicsDeviceKHR",
                         (PFN_xrVoidFunction*)&xrGetVulkanGraphicsDeviceKHR);

    if (!xrGetVulkanGraphicsDeviceKHR) {
        LOGE("xrGetVulkanGraphicsDeviceKHR not available");
        return false;
    }

    XrResult result = xrGetVulkanGraphicsDeviceKHR(instance_, systemId_, vkInstance_, &vkPhysicalDevice_);
    if (result != XR_SUCCESS) {
        LOGE("Failed to get Vulkan physical device: %d", result);
        return false;
    }

    // TODO: Use xrCreateVulkanDeviceKHR instead of direct Vulkan calls
    // uint32_t queueFamilyCount = 0;
    // vkGetPhysicalDeviceQueueFamilyProperties(vkPhysicalDevice_, &queueFamilyCount, nullptr);
    //
    // std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
    // vkGetPhysicalDeviceQueueFamilyProperties(vkPhysicalDevice_, &queueFamilyCount, queueFamilyProperties.data());
    //
    // for (uint32_t i = 0; i < queueFamilyCount; i++) {
    //     if (queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
    //         vkQueueFamilyIndex_ = i;
    //         break;
    //     }
    // }
    //
    // VkDeviceQueueCreateInfo queueCreateInfo{};
    // queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    // queueCreateInfo.queueFamilyIndex = vkQueueFamilyIndex_;
    // queueCreateInfo.queueCount = 1;
    // float queuePriority = 1.0f;
    // queueCreateInfo.pQueuePriorities = &queuePriority;
    //
    // VkDeviceCreateInfo deviceCreateInfo{};
    // deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    // deviceCreateInfo.queueCreateInfoCount = 1;
    // deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    //
    // VkResult vkResult = vkCreateDevice(vkPhysicalDevice_, &deviceCreateInfo, nullptr, &vkDevice_);
    // if (vkResult != VK_SUCCESS) {
    //     LOGE("Failed to create Vulkan device: %d", vkResult);
    //     return false;
    // }
    //
    // vkGetDeviceQueue(vkDevice_, vkQueueFamilyIndex_, 0, &vkQueue_);

    LOGE("CreateVulkanDevice not fully implemented - use existing Vulkan context");
    return true;
}

bool QuestVRManager::CreateVulkanSwapchainImages() {
    for (int eye = 0; eye < VIEW_COUNT; eye++) {
        uint32_t imageCount = 0;
        XrResult result = xrEnumerateSwapchainImages(swapchains_[eye].swapchain, 0, &imageCount, nullptr);
        if (result != XR_SUCCESS) {
            LOGE("Failed to enumerate swapchain images: %d", result);
            return false;
        }

        std::vector<XrSwapchainImageBaseHeader*> images(imageCount);
        std::vector<XrSwapchainImageVulkanKHR> vulkanImages(imageCount);

        for (uint32_t i = 0; i < imageCount; i++) {
            vulkanImages[i].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
            vulkanImages[i].next = nullptr;
            images[i] = reinterpret_cast<XrSwapchainImageBaseHeader*>(&vulkanImages[i]);
        }

        result = xrEnumerateSwapchainImages(swapchains_[eye].swapchain, imageCount, &imageCount,
                                           reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
        if (result != XR_SUCCESS) {
            LOGE("Failed to get swapchain images: %d", result);
            return false;
        }

        swapchains_[eye].images.assign(vulkanImages.begin(), vulkanImages.end());
    }

    return true;
}

} // namespace QuestVR

#endif // USE_VULKAN
