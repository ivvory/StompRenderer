#include <cmath>
#include <stdexcept>
#include <memory>
#include "Texture.h"
#include "Logs.h"
#include "imgui_impl_vulkan.h"
#include "stb_image.h"

omp::Texture::Texture(const std::shared_ptr<VulkanContext>& helper)
        : m_VulkanContext(helper)
{

}

void omp::Texture::destroyVkObjects()
{
    vkDestroySampler(m_VulkanContext.lock()->logical_device, m_TextureSampler, nullptr);
    vkDestroyImageView(m_VulkanContext.lock()->logical_device, m_TextureImageView, nullptr);
    vkDestroyImage(m_VulkanContext.lock()->logical_device, m_TextureImage, nullptr);
    vkFreeMemory(m_VulkanContext.lock()->logical_device, m_TextureImageMemory, nullptr);
}

uint64_t omp::Texture::getTextureId()
{
    if (!hasFlags(LoadedToUI))
    {
        loadToUi();
    }
    return m_Id;
}

void omp::Texture::loadTextureToCpu(const std::string& path)
{
    removeFlags(LoadedToGPU | LoadedToCPU | LoadedToUI);

    m_ContentPath = path;
    int tex_channels;
    m_Pixels = stbi_load(path.c_str(), &m_Width, &m_Height, &tex_channels, STBI_rgb_alpha);
    m_Size = m_Width * m_Height * 4;

    m_MipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(m_Width, m_Height)))) + 1;

    if (!m_Pixels)
    {
        throw std::runtime_error("Failed to load texture image!");
    }

    addFlags(LoadedToCPU);
}

void omp::Texture::loadToGpu()
{
    if (hasFlags(LoadedToGPU))
    {
        return;
    }

    if (!hasFlags(LoadedToCPU))
    {
        VERROR(Rendering, "Texture not loaded to CPU {1} ", m_ContentPath);
    }

    createSampler();
    createImage();
    createImageView();

    addFlags(LoadedToGPU);
}

void omp::Texture::createSampler()
{
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;

    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    sampler_info.anisotropyEnable = VK_TRUE;
    sampler_info.maxAnisotropy = 16;

    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    sampler_info.unnormalizedCoordinates = VK_FALSE;

    sampler_info.compareEnable = VK_FALSE;
    sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;

    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.mipLodBias = 0.f;
    sampler_info.minLod = 0;
    sampler_info.maxLod = static_cast<float>(m_MipLevels);

    if (vkCreateSampler(m_VulkanContext.lock()->logical_device, &sampler_info, nullptr, &m_TextureSampler) !=
        VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create sampler");
    }
}

void omp::Texture::createImage()
{
    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;
    m_VulkanContext.lock()->createBuffer(m_Size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                         staging_buffer, staging_buffer_memory);

    void* data;
    vkMapMemory(m_VulkanContext.lock()->logical_device, staging_buffer_memory, 0, m_Size, 0, &data);
    memcpy(data, m_Pixels, static_cast<size_t>(m_Size));
    vkUnmapMemory(m_VulkanContext.lock()->logical_device, staging_buffer_memory);

    stbi_image_free(m_Pixels);

    m_VulkanContext.lock()->createImage(m_Width, m_Height, m_MipLevels, VK_FORMAT_R8G8B8A8_SRGB,
                                        VK_IMAGE_TILING_OPTIMAL,
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                        VK_IMAGE_USAGE_SAMPLED_BIT,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_TextureImage, m_TextureImageMemory,
                                        VK_SAMPLE_COUNT_1_BIT);

    m_VulkanContext.lock()->transitionImageLayout(m_TextureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED,
                                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_MipLevels);
    m_VulkanContext.lock()->copyBufferToImage(staging_buffer, m_TextureImage, static_cast<uint32_t>(m_Width),
                                              static_cast<uint32_t>(m_Height));
    //transitionImageLayout(m_TextureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED,
    //                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_MipLevels);
    m_VulkanContext.lock()->generateMipmaps(m_TextureImage, VK_FORMAT_R8G8B8A8_SRGB, m_Width, m_Height, m_MipLevels);

    vkDestroyBuffer(m_VulkanContext.lock()->logical_device, staging_buffer, nullptr);
    vkFreeMemory(m_VulkanContext.lock()->logical_device, staging_buffer_memory, nullptr);
}

void omp::Texture::createImageView()
{
    m_TextureImageView = m_VulkanContext.lock()->createImageView(m_TextureImage, VK_FORMAT_R8G8B8A8_SRGB,
                                                                 VK_IMAGE_ASPECT_COLOR_BIT, m_MipLevels);
}

void omp::Texture::fullLoad(const std::string& path)
{
    if (hasFlags(LoadedToGPU))
    {
        destroyVkObjects();
    }
    loadTextureToCpu(path);
    loadToGpu();
}

void omp::Texture::lazyLoad(const std::string& path)
{
    loadTextureToCpu(path);
}

void omp::Texture::removeFlags(uint16_t flags)
{
    m_Flags = (m_Flags & flags) ^ m_Flags;
}

void omp::Texture::addFlags(uint16_t flags)
{
    m_Flags = m_Flags | flags;
}

bool omp::Texture::hasFlags(uint16_t flags) const
{
    return m_Flags & flags;
}

void omp::Texture::loadToUi()
{
    if (!hasFlags(LoadedToGPU))
    {
        loadToGpu();
    }
    m_Id = ImGui_ImplVulkan_AddTexture(m_TextureSampler, m_TextureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    addFlags(LoadedToUI);
}

VkImageView omp::Texture::getImageView()
{
    if (!hasFlags(LoadedToGPU))
    {
        loadToGpu();
    }
    return m_TextureImageView;
}

VkImage omp::Texture::getImage()
{
    if (!hasFlags(LoadedToGPU))
    {
        loadToGpu();
    }
    return m_TextureImage;
}

VkSampler omp::Texture::getSampler()
{
    if (!hasFlags(LoadedToGPU))
    {
        loadToGpu();
    }
    return m_TextureSampler;
}

omp::Texture::Texture(const std::string& inPath)
{

}

