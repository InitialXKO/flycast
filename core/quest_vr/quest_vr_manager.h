#ifndef QUEST_VR_MANAGER_H
#define QUEST_VR_MANAGER_H

#ifdef USE_VULKAN

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace QuestVR {

// VR Game Modes - 模式切换的枚举
enum class GameMode {
    CINEMA = 0,      // 虚拟影院模式
    ARCADE = 1,      // 虚拟机台模式
    IMMERSIVE = 2    // 全沉浸 6DOF 模式
};

struct ControllerState {
    XrSpace space;
    XrActionSet actionSet;
    XrAction poseAction;
    XrAction triggerAction;
    XrAction gripAction;
    XrAction thumbstickAction;
    XrAction buttonAAction;
    XrAction buttonBAction;
    XrAction menuAction;

    // Current values
    float triggerValue;
    float gripValue;
    float thumbstickX;
    float thumbstickY;
    bool buttonAPressed;
    bool buttonBPressed;
    bool menuPressed;

    // Pose data
    XrPosef pose;
    XrVector3f position;
    XrQuaternionf orientation;
};

class QuestVRManager {
public:
    QuestVRManager();
    ~QuestVRManager();

    // Initialization
    bool Initialize();
    void Shutdown();

    // Frame management
    bool BeginFrame();
    void EndFrame();

    // Render targets
    void GetViewTransforms(int viewCount, XrView* views);
    XrSwapchain GetSwapchain(int eye);
    uint32_t GetSwapchainWidth(int eye) const;
    uint32_t GetSwapchainHeight(int eye) const;

    // Space management
    XrSpace GetSpace() const { return space_; }
    XrSession GetSession() const { return session_; }
    XrInstance GetInstance() const { return instance_; }

    // Input handling
    void UpdateInput();
    const ControllerState& GetControllerState(int index) const;

    // ========== 模式切换相关代码 ==========
    
    // 设置游戏模式
    void SetGameMode(GameMode mode);
    
    // 获取当前游戏模式
    GameMode GetGameMode() const { return currentMode_; }
    
    // 切换到下一个模式（循环切换）
    void CycleGameMode();
    
    // 获取模式名称（用于 UI 显示）
    const char* GetGameModeName(GameMode mode) const;
    
    // ========== Dreamcast 控制器映射 ==========
    
    struct DreamcastInput {
        bool a;
        bool b;
        bool x;
        bool y;
        bool start;
        bool dPadUp;
        bool dPadDown;
        bool dPadLeft;
        bool dPadRight;
        float analogX;
        float analogY;
        float lt;
        float rt;
    };
    
    DreamcastInput GetDreamcastInput() const;

    // Utility functions
    bool IsSessionRunning() const { return sessionRunning_; }
    bool IsFocused() const { return sessionFocused_; }

private:
    // OpenXR initialization
    bool CreateInstance();
    bool CreateSession();
    bool CreateSwapchains();
    bool CreateActionSets();
    bool CreateSpaces();

    // Vulkan integration
    bool CreateVulkanInstance();
    bool CreateVulkanDevice();
    bool CreateVulkanSwapchainImages();

    // Frame state
    XrFrameState frameState_;

    // OpenXR objects
    XrInstance instance_;
    XrSession session_;
    XrSpace space_;
    XrSystemId systemId_;

    // Swapchain data
    static constexpr int VIEW_COUNT = 2;
    struct SwapchainData {
        XrSwapchain swapchain;
        uint32_t width;
        uint32_t height;
        int arraySize;
        std::vector<XrSwapchainImageVulkanKHR> images;
    };
    SwapchainData swapchains_[VIEW_COUNT];

    // View configuration
    XrViewConfigurationView viewConfigViews_[VIEW_COUNT];

    // Input
    ControllerState controllers_[2];

    // State
    bool sessionRunning_;
    bool sessionFocused_;
    
    // ========== 模式切换相关私有成员 ==========
    
    GameMode currentMode_;  // 当前游戏模式
    
    // 根据模式计算虚拟屏幕位置
    XrPosef CalculateGameModePose(GameMode mode, const XrPosef& headPose) const;
};

} // namespace QuestVR

#endif // USE_VULKAN
