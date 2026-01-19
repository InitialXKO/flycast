#ifdef USE_VULKAN

#include "vulkan_renderer_quest.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace QuestVR {

VulkanRendererQuest::VulkanRendererQuest()
    : vkInstance_(VK_NULL_HANDLE)
    , vkDevice_(VK_NULL_HANDLE)
    , vkPhysicalDevice_(VK_NULL_HANDLE)
    , vkQueue_(VK_NULL_HANDLE)
    , vkQueueFamilyIndex_(0)
    , renderPass_(VK_NULL_HANDLE)
    , msaaSamples_(VK_SAMPLE_COUNT_4_BIT)
    , msaaImage_(VK_NULL_HANDLE)
    , msaaImageMemory_(VK_NULL_HANDLE)
    , msaaImageView_(VK_NULL_HANDLE)
    , depthImage_(VK_NULL_HANDLE)
    , depthImageMemory_(VK_NULL_HANDLE)
    , depthImageView_(VK_NULL_HANDLE)
    , commandPool_(VK_NULL_HANDLE)
    , currentFrame_(0)
    , drawCalls_(0)
    , multiviewEnabled_(false)
    , foveatedEnabled_(false)
    , dynamicResolutionMinScale_(0.7f)
    , dynamicResolutionMaxScale_(1.0f)
{
    memset(viewMatrices_, 0, sizeof(viewMatrices_));
}

VulkanRendererQuest::~VulkanRendererQuest() {
    Shutdown();
}

bool VulkanRendererQuest::Initialize(VkInstance instance, VkDevice device,
                                      VkPhysicalDevice physicalDevice,
                                      uint32_t queueFamilyIndex, VkQueue queue) {
    vkInstance_ = instance;
    vkDevice_ = device;
    vkPhysicalDevice_ = physicalDevice;
    vkQueueFamilyIndex_ = queueFamilyIndex;
    vkQueue_ = queue;

    if (!CreateRenderPass()) {
        return false;
    }

    if (!CreateCommandPool()) {
        return false;
    }

    if (!CreateSynchronizationObjects()) {
        return false;
    }

    // Identity matrices for initial state
    for (int i = 0; i < 2; i++) {
        memset(viewMatrices_[i].viewMatrix, 0, sizeof(viewMatrices_[i].viewMatrix));
        memset(viewMatrices_[i].projectionMatrix, 0, sizeof(viewMatrices_[i].projectionMatrix));

        viewMatrices_[i].viewMatrix[0] = 1.0f;
        viewMatrices_[i].viewMatrix[5] = 1.0f;
        viewMatrices_[i].viewMatrix[10] = 1.0f;
        viewMatrices_[i].viewMatrix[15] = 1.0f;

        viewMatrices_[i].projectionMatrix[0] = 1.0f;
        viewMatrices_[i].projectionMatrix[5] = 1.0f;
        viewMatrices_[i].projectionMatrix[10] = 1.0f;
        viewMatrices_[i].projectionMatrix[15] = 1.0f;
    }

    return true;
}

void VulkanRendererQuest::Shutdown() {
    if (vkDevice_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vkDevice_);

        for (auto& fence : inFlightFences_) {
            if (fence != VK_NULL_HANDLE) {
                vkDestroyFence(vkDevice_, fence, nullptr);
            }
        }

        for (auto& semaphore : renderSemaphores_) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(vkDevice_, semaphore, nullptr);
            }
        }

        for (auto& framebuffer : framebuffers_) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(vkDevice_, framebuffer, nullptr);
            }
        }

        if (commandPool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(vkDevice_, commandPool_, nullptr);
        }

        if (msaaImageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(vkDevice_, msaaImageView_, nullptr);
        }
        if (msaaImage_ != VK_NULL_HANDLE) {
            vkDestroyImage(vkDevice_, msaaImage_, nullptr);
        }
        if (msaaImageMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(vkDevice_, msaaImageMemory_, nullptr);
        }

        if (depthImageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(vkDevice_, depthImageView_, nullptr);
        }
        if (depthImage_ != VK_NULL_HANDLE) {
            vkDestroyImage(vkDevice_, depthImage_, nullptr);
        }
        if (depthImageMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(vkDevice_, depthImageMemory_, nullptr);
        }

        if (renderPass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(vkDevice_, renderPass_, nullptr);
        }
    }
}

bool VulkanRendererQuest::CreateRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_SRGB;
    colorAttachment.samples = msaaSamples_;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription resolveAttachment{};
    resolveAttachment.format = VK_FORMAT_R8G8B8A8_SRGB;
    resolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    resolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    resolveAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    resolveAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    resolveAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D24_UNORM_S8_UINT;
    depthAttachment.samples = msaaSamples_;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference resolveAttachmentRef{};
    resolveAttachmentRef.attachment = 2;
    resolveAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;
    subpass.pResolveAttachments = &resolveAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::vector<VkAttachmentDescription> attachments = {
        colorAttachment, depthAttachment, resolveAttachment
    };

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    return vkCreateRenderPass(vkDevice_, &renderPassInfo, nullptr, &renderPass_) == VK_SUCCESS;
}

bool VulkanRendererQuest::CreateCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = vkQueueFamilyIndex_;

    return vkCreateCommandPool(vkDevice_, &poolInfo, nullptr, &commandPool_) == VK_SUCCESS;
}

bool VulkanRendererQuest::CreateCommandBuffers() {
    commandBuffers_.resize(framebuffers_.size());

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());

    return vkAllocateCommandBuffers(vkDevice_, &allocInfo, commandBuffers_.data()) == VK_SUCCESS;
}

bool VulkanRendererQuest::CreateSynchronizationObjects() {
    const uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    inFlightFences_.resize(MAX_FRAMES_IN_FLIGHT);
    renderSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(vkDevice_, &semaphoreInfo, nullptr, &renderSemaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(vkDevice_, &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

bool VulkanRendererQuest::CreateRenderTargets(uint32_t width, uint32_t height, uint32_t imageCount) {
    swapchainTargets_.resize(imageCount);

    if (!CreateMSAATargets(width, height)) {
        return false;
    }

    if (!CreateDepthTarget(width, height)) {
        return false;
    }

    return CreateFramebuffers(width, height);
}

bool VulkanRendererQuest::CreateMSAATargets(uint32_t width, uint32_t height) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.samples = msaaSamples_;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(vkDevice_, &imageInfo, nullptr, &msaaImage_) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(vkDevice_, msaaImage_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    // Find memory type (simplified)
    allocInfo.memoryTypeIndex = 0;

    if (vkAllocateMemory(vkDevice_, &allocInfo, nullptr, &msaaImageMemory_) != VK_SUCCESS) {
        return false;
    }

    vkBindImageMemory(vkDevice_, msaaImage_, msaaImageMemory_, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = msaaImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    return vkCreateImageView(vkDevice_, &viewInfo, nullptr, &msaaImageView_) == VK_SUCCESS;
}

bool VulkanRendererQuest::CreateDepthTarget(uint32_t width, uint32_t height) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_D24_UNORM_S8_UINT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = msaaSamples_;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(vkDevice_, &imageInfo, nullptr, &depthImage_) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(vkDevice_, depthImage_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = 0;

    if (vkAllocateMemory(vkDevice_, &allocInfo, nullptr, &depthImageMemory_) != VK_SUCCESS) {
        return false;
    }

    vkBindImageMemory(vkDevice_, depthImage_, depthImageMemory_, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_D24_UNORM_S8_UINT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    return vkCreateImageView(vkDevice_, &viewInfo, nullptr, &depthImageView_) == VK_SUCCESS;
}

bool VulkanRendererQuest::CreateFramebuffers(uint32_t width, uint32_t height) {
    framebuffers_.resize(swapchainTargets_.size());

    for (size_t i = 0; i < swapchainTargets_.size(); i++) {
        std::vector<VkImageView> attachments = {
            msaaImageView_,
            depthImageView_,
            swapchainTargets_[i].imageView
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass_;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = width;
        framebufferInfo.height = height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(vkDevice_, &framebufferInfo, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            return false;
        }
    }

    if (!CreateCommandBuffers()) {
        return false;
    }

    return true;
}

void VulkanRendererQuest::SetSwapchainImage(int eye, int imageIndex, VkImage image, VkImageView imageView) {
    // Store swapchain image info
    if (imageIndex < static_cast<int>(swapchainTargets_.size())) {
        swapchainTargets_[imageIndex].image = image;
        swapchainTargets_[imageIndex].imageView = imageView;
    }
}

void VulkanRendererQuest::BeginFrame() {
    drawCalls_ = 0;

    vkWaitForFences(vkDevice_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);
    vkResetFences(vkDevice_, 1, &inFlightFences_[currentFrame_]);

    vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffers_[currentFrame_], &beginInfo);
}

void VulkanRendererQuest::RenderEye(int eye, const XrPosef& eyePose, const XrFovf& fov) {
    if (eye < 0 || eye >= 2) return;

    VkCommandBuffer cmd = commandBuffers_[currentFrame_];

    BeginRenderPass(cmd, eye, currentFrame_);

    // Calculate projection matrix from FOV
    float tanLeft = std::tan(fov.angleLeft);
    float tanRight = std::tan(fov.angleRight);
    float tanUp = std::tan(fov.angleUp);
    float tanDown = std::tan(fov.angleDown);

    float* proj = viewMatrices_[eye].projectionMatrix;
    float tanWidth = tanRight - tanLeft;
    float tanHeight = tanUp - tanDown;

    proj[0] = 2.0f / tanWidth;
    proj[1] = 0.0f;
    proj[2] = (tanRight + tanLeft) / tanWidth;
    proj[3] = 0.0f;

    proj[4] = 0.0f;
    proj[5] = 2.0f / tanHeight;
    proj[6] = (tanUp + tanDown) / tanHeight;
    proj[7] = 0.0f;

    proj[8] = 0.0f;
    proj[9] = 0.0f;
    proj[10] = -1.0f;
    proj[11] = -0.01f; // Near plane

    proj[12] = 0.0f;
    proj[13] = 0.0f;
    proj[14] = 0.0f;
    proj[15] = 1.0f;

    // View matrix from eye pose
    float* view = viewMatrices_[eye].viewMatrix;
    const XrQuaternionf& q = eyePose.orientation;
    const XrVector3f& p = eyePose.position;

    // Convert quaternion to rotation matrix
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    view[0] = 1.0f - 2.0f * (yy + zz);
    view[1] = 2.0f * (xy + wz);
    view[2] = 2.0f * (xz - wy);
    view[3] = 0.0f;

    view[4] = 2.0f * (xy - wz);
    view[5] = 1.0f - 2.0f * (xx + zz);
    view[6] = 2.0f * (yz + wx);
    view[7] = 0.0f;

    view[8] = 2.0f * (xz + wy);
    view[9] = 2.0f * (yz - wx);
    view[10] = 1.0f - 2.0f * (xx + yy);
    view[11] = 0.0f;

    view[12] = p.x;
    view[13] = p.y;
    view[14] = p.z;
    view[15] = 1.0f;

    // Render the scene here (would call Flycast's render functions)
    drawCalls_++;

    EndRenderPass(cmd);
}

void VulkanRendererQuest::BeginRenderPass(VkCommandBuffer cmd, int eye, uint32_t framebufferIndex) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = framebuffers_[framebufferIndex];
    renderPassInfo.renderArea.offset = {0, 0};

    if (!framebuffers_.empty()) {
        VkFramebuffer fb = framebuffers_[framebufferIndex];
        // Get dimensions from render area (simplified)
        renderPassInfo.renderArea.extent = {2000, 2000};
    }

    std::vector<VkClearValue> clearValues(3);
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    clearValues[2].color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanRendererQuest::EndRenderPass(VkCommandBuffer cmd) {
    vkCmdEndRenderPass(cmd);
}

void VulkanRendererQuest::EndFrame() {
    vkEndCommandBuffer(commandBuffers_[currentFrame_]);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {renderSemaphores_[currentFrame_]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[currentFrame_];

    vkQueueSubmit(vkQueue_, 1, &submitInfo, inFlightFences_[currentFrame_]);

    currentFrame_ = (currentFrame_ + 1) % inFlightFences_.size();
}

void VulkanRendererQuest::SetMSAASamples(VkSampleCountFlagBits samples) {
    if (samples != msaaSamples_) {
        msaaSamples_ = samples;
        // Would need to recreate render targets
    }
}

void VulkanRendererQuest::EnableMultiview(bool enable) {
    multiviewEnabled_ = enable;
    // Would need to recreate render pass with VK_KHR_multiview
}

void VulkanRendererQuest::EnableFoveatedRendering(bool enable) {
    foveatedEnabled_ = enable;
    // Quest 3 specific foveated rendering setup
}

void VulkanRendererQuest::SetDynamicResolution(float minScale, float maxScale) {
    dynamicResolutionMinScale_ = std::clamp(minScale, 0.5f, 1.0f);
    dynamicResolutionMaxScale_ = std::clamp(maxScale, 0.5f, 1.0f);
}

void VulkanRendererQuest::SetViewMatrix(int eye, const float* matrix) {
    if (eye >= 0 && eye < 2 && matrix) {
        memcpy(viewMatrices_[eye].viewMatrix, matrix, 16 * sizeof(float));
    }
}

void VulkanRendererQuest::SetProjectionMatrix(int eye, const float* matrix) {
    if (eye >= 0 && eye < 2 && matrix) {
        memcpy(viewMatrices_[eye].projectionMatrix, matrix, 16 * sizeof(float));
    }
}

void VulkanRendererQuest::TransitionImageLayout(VkImage image, VkImageLayout oldLayout,
                                                  VkImageLayout newLayout) {
    VkCommandBuffer cmd = commandBuffers_[currentFrame_];

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage, destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        throw std::invalid_argument("unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        cmd,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

} // namespace QuestVR

#endif // USE_VULKAN
