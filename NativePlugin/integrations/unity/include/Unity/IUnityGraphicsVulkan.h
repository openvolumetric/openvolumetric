// Unity Native Plugin API copyright © 2015 Unity Technologies ApS
//
// Licensed under the Unity Companion License for Unity-dependent projects.
#pragma once

#include "IUnityInterface.h"
#include <stdbool.h>
#include <vulkan/vulkan.h>

typedef struct UnityVulkanInstance
{
    VkPipelineCache pipelineCache;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr;
    unsigned int queueFamilyIndex;
    void* reserved[8];
} UnityVulkanInstance;

typedef struct UnityVulkanMemory
{
    VkDeviceMemory memory;
    VkDeviceSize offset;
    VkDeviceSize size;
    void* mapped;
    VkMemoryPropertyFlags flags;
    unsigned int memoryTypeIndex;
    void* reserved[4];
} UnityVulkanMemory;

typedef enum UnityVulkanResourceAccessMode
{
    kUnityVulkanResourceAccess_ObserveOnly,
    kUnityVulkanResourceAccess_PipelineBarrier,
    kUnityVulkanResourceAccess_Recreate,
} UnityVulkanResourceAccessMode;

typedef struct UnityVulkanImage
{
    UnityVulkanMemory memory;
    VkImage image;
    VkImageLayout layout;
    VkImageAspectFlags aspect;
    VkImageUsageFlags usage;
    VkFormat format;
    VkExtent3D extent;
    VkImageTiling tiling;
    VkImageType type;
    VkSampleCountFlagBits samples;
    int layers;
    int mipCount;
    void* reserved[4];
} UnityVulkanImage;

typedef struct UnityVulkanBuffer
{
    UnityVulkanMemory memory;
    VkBuffer buffer;
    size_t sizeInBytes;
    VkBufferUsageFlags usage;
    void* reserved[4];
} UnityVulkanBuffer;

typedef struct UnityVulkanRecordingState
{
    VkCommandBuffer commandBuffer;
    VkCommandBufferLevel commandBufferLevel;
    VkRenderPass renderPass;
    VkFramebuffer framebuffer;
    int subPassIndex;
    unsigned long long currentFrameNumber;
    unsigned long long safeFrameNumber;
    void* reserved[4];
} UnityVulkanRecordingState;

typedef enum UnityVulkanEventRenderPassPreCondition
{
    kUnityVulkanRenderPass_DontCare,
    kUnityVulkanRenderPass_EnsureInside,
    kUnityVulkanRenderPass_EnsureOutside
} UnityVulkanEventRenderPassPreCondition;

typedef enum UnityVulkanGraphicsQueueAccess
{
    kUnityVulkanGraphicsQueueAccess_DontCare,
    kUnityVulkanGraphicsQueueAccess_Allow,
} UnityVulkanGraphicsQueueAccess;

typedef struct UnityVulkanPluginEventConfig
{
    UnityVulkanEventRenderPassPreCondition renderPassPrecondition;
    UnityVulkanGraphicsQueueAccess graphicsQueueAccess;
    uint32_t flags;
} UnityVulkanPluginEventConfig;

const VkImageSubresource* const UnityVulkanWholeImage = NULL;

UNITY_DECLARE_INTERFACE(IUnityGraphicsVulkan)
{
    void* InterceptInitialization;
    void* InterceptVulkanAPI;
    void(UNITY_INTERFACE_API * ConfigureEvent)(
        int eventID, const UnityVulkanPluginEventConfig* config);
    UnityVulkanInstance(UNITY_INTERFACE_API * Instance)();
    bool(UNITY_INTERFACE_API * CommandRecordingState)(
        UnityVulkanRecordingState* state,
        UnityVulkanGraphicsQueueAccess queueAccess);
    bool(UNITY_INTERFACE_API * AccessTexture)(
        void* nativeTexture,
        const VkImageSubresource* subResource,
        VkImageLayout layout,
        VkPipelineStageFlags pipelineStageFlags,
        VkAccessFlags accessFlags,
        UnityVulkanResourceAccessMode accessMode,
        UnityVulkanImage* outImage);
    void* AccessRenderBufferTexture;
    void* AccessRenderBufferResolveTexture;
    bool(UNITY_INTERFACE_API * AccessBuffer)(
        void* nativeBuffer,
        VkPipelineStageFlags pipelineStageFlags,
        VkAccessFlags accessFlags,
        UnityVulkanResourceAccessMode accessMode,
        UnityVulkanBuffer* outBuffer);
    void(UNITY_INTERFACE_API * EnsureOutsideRenderPass)();
    void(UNITY_INTERFACE_API * EnsureInsideRenderPass)();
};

UNITY_REGISTER_INTERFACE_GUID(
    0x95355348d4ef4e11ULL,
    0x9789313dfcffcc87ULL,
    IUnityGraphicsVulkan)
