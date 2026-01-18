#ifndef VULKAN_RENDERER_QUEST_H
#define VULKAN_RENDERER_QUEST_H

#ifdef USE_VULKAN

#include "quest_vr_manager.h"
#include <vulkan/vulkan.h>
#include <vector>

namespace QuestVR {

class VulkanRendererQuest {
public:
    VulkanRendererQuest();
    ~VulkanRendererQuest();

    // Initialization
    bool Initialize(VkInstance instance, VkDevice device, VkPhysicalDevice physicalDevice,
                    uint32_t queueFamilyIndex, VkQueue queue);
    void Shutdown();

    // Frame rendering
    void BeginFrame();
    void RenderEye(int eye, const XrPosef& eyePose, const XrFovf& fov);
    void EndFrame();

    // Render targets
    bool CreateRenderTargets(uint32_t width, uint32_t height, uint32_t imageCount);
    void SetSwapchainImage(int eye, int imageIndex, VkImage image, VkImageView imageView);

    // Rendering control
    void SetMSAASamples(VkSampleCountFlagBits samples);
    VkSampleCountFlagBits GetMSAASamples() const { return msaaSamples_; }

    // Quest 3 optimizations
    void EnableMultiview(bool enable);
    void EnableFoveatedRendering(bool enable);
    void SetDynamicResolution(float minScale, float maxScale);

    // View matrices
    void SetViewMatrix(int eye, const float* matrix);
    void SetProjectionMatrix(int eye, const float* matrix);

    // Rendering stats
    uint32_t GetDrawCalls() const { return drawCalls_; }
    void ResetDrawCalls() { drawCalls_ = 0; }

private:
    // Vulkan objects
    VkInstance vkInstance_;
    VkDevice vkDevice_;
    VkPhysicalDevice vkPhysicalDevice_;
    VkQueue vkQueue_;
    uint32_t vkQueueFamilyIndex_;

    // Render pass and framebuffers
    VkRenderPass renderPass_;
    std::vector<VkFramebuffer> framebuffers_;

    // MSAA
    VkSampleCountFlagBits msaaSamples_;
    VkImage msaaImage_;
    VkDeviceMemory msaaImageMemory_;
    VkImageView msaaImageView_;
    VkImage depthImage_;
    VkDeviceMemory depthImageMemory_;
    VkImageView depthImageView_;

    // Command buffers
    VkCommandPool commandPool_;
    std::vector<VkCommandBuffer> commandBuffers_;

    // Synchronization
    std::vector<VkSemaphore> renderSemaphores_;
    std::vector<VkFence> inFlightFences_;

    // Render targets
    struct SwapchainTarget {
        VkImage image;
        VkImageView imageView;
    };
    std::vector<SwapchainTarget> swapchainTargets_;

    // View data
    struct ViewMatrices {
        float viewMatrix[16];
        float projectionMatrix[16];
    };
    ViewMatrices viewMatrices_[2];

    // State
    uint32_t currentFrame_;
    uint32_t drawCalls_;
    bool multiviewEnabled_;
    bool foveatedEnabled_;
    float dynamicResolutionMinScale_;
    float dynamicResolutionMaxScale_;

    // Helper functions
    bool CreateRenderPass();
    bool CreateCommandPool();
    bool CreateCommandBuffers();
    bool CreateSynchronizationObjects();
    bool CreateMSAATargets(uint32_t width, uint32_t height);
    bool CreateDepthTarget(uint32_t width, uint32_t height);
    bool CreateFramebuffers(uint32_t width, uint32_t height);
    void TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    void BeginRenderPass(VkCommandBuffer cmd, int eye, uint32_t framebufferIndex);
    void EndRenderPass(VkCommandBuffer cmd);
};

} // namespace QuestVR

#endif // USE_VULKAN

#endif // VULKAN_RENDERER_QUEST_H
