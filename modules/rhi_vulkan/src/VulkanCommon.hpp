#pragma once

#include <vulkan/vulkan.h>

#include <vk_mem_alloc.h>

#include <phi/core/Assert.hpp>
#include <phi/core/Log.hpp>
#include <phi/core/Types.hpp>
#include <phi/rhi/Types.hpp>

namespace phi::vulkan {

/// Frames the CPU may record ahead of the GPU.
inline constexpr u32 kFramesInFlight = 2;

[[nodiscard]] const char* resultString(VkResult result);

// --- RHI -> Vulkan conversions -------------------------------------------------

[[nodiscard]] VkFormat toVkFormat(rhi::Format format);
/// Returns Format::Undefined for formats the RHI does not expose.
[[nodiscard]] rhi::Format fromVkFormat(VkFormat format);
[[nodiscard]] VkImageAspectFlags aspectMask(rhi::Format format);
[[nodiscard]] VkBufferUsageFlags toVkBufferUsage(rhi::BufferUsage usage);
[[nodiscard]] VkImageUsageFlags toVkImageUsage(rhi::TextureUsage usage);
[[nodiscard]] VkShaderStageFlags toVkShaderStages(rhi::ShaderStage stages);
[[nodiscard]] VkFilter toVkFilter(rhi::Filter filter);
[[nodiscard]] VkSamplerAddressMode toVkAddressMode(rhi::AddressMode mode);
[[nodiscard]] VkFormat toVkVertexFormat(rhi::VertexFormat format);
[[nodiscard]] VkPrimitiveTopology toVkTopology(rhi::PrimitiveTopology topology);
[[nodiscard]] VkCullModeFlags toVkCullMode(rhi::CullMode mode);
[[nodiscard]] VkFrontFace toVkFrontFace(rhi::FrontFace face);
[[nodiscard]] VkCompareOp toVkCompareOp(rhi::CompareOp op);
[[nodiscard]] VkAttachmentLoadOp toVkLoadOp(rhi::LoadOp op);
[[nodiscard]] VkAttachmentStoreOp toVkStoreOp(rhi::StoreOp op);
[[nodiscard]] VkDescriptorType toVkDescriptorType(rhi::BindingType type);

}  // namespace phi::vulkan

/// For Vulkan calls that can only fail on unrecoverable conditions (device
/// lost, out of memory) or API misuse: logs the failing call and aborts.
#define PHI_VK_CHECK(call)                                                          \
    do {                                                                             \
        const VkResult phiVkResult = (call);                                        \
        if (phiVkResult != VK_SUCCESS) [[unlikely]] {                               \
            ::phi::log::error("vulkan", "{} failed: {}", #call,                     \
                               ::phi::vulkan::resultString(phiVkResult));          \
            PHI_ASSERT(false, "unrecoverable Vulkan error");                        \
        }                                                                            \
    } while (false)
