// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <unordered_map>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/math_util.h"
#include "video_core/gpu.h"
#include "video_core/rasterizer_cache.h"
#include "video_core/renderer_vulkan/declarations.h"
#include "video_core/renderer_vulkan/vk_image.h"
#include "video_core/renderer_vulkan/vk_memory_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/surface_base.h"
#include "video_core/texture_cache/texture_cache.h"
#include "video_core/textures/decoders.h"

namespace Core {
class System;
}

namespace VideoCore {
class RasterizerInterface;
}

namespace Vulkan {

class RasterizerVulkan;
class VKDevice;
class VKResourceManager;
class VKScheduler;
class VKStagingBufferPool;

class CachedSurfaceView;
class CachedSurface;

using Surface = std::shared_ptr<CachedSurface>;
using View = std::shared_ptr<CachedSurfaceView>;
using TextureCacheBase = VideoCommon::TextureCache<Surface, View>;

using VideoCommon::SurfaceParams;
using VideoCommon::ViewParams;

class CachedSurface final : public VideoCommon::SurfaceBase<View> {
    friend CachedSurfaceView;

public:
    explicit CachedSurface(Core::System& system, const VKDevice& device,
                           VKResourceManager& resource_manager, VKMemoryManager& memory_manager,
                           VKScheduler& scheduler, VKStagingBufferPool& staging_pool,
                           GPUVAddr gpu_addr, const SurfaceParams& params);
    ~CachedSurface();

    void UploadTexture(const std::vector<u8>& staging_buffer) override;
    void DownloadTexture(std::vector<u8>& staging_buffer) override;

    void FullTransition(vk::PipelineStageFlags new_stage_mask, vk::AccessFlags new_access,
                        vk::ImageLayout new_layout) {
        image->Transition(0, static_cast<u32>(params.GetNumLayers()), 0, params.num_levels,
                          new_stage_mask, new_access, new_layout);
    }

    void Transition(u32 base_layer, u32 num_layers, u32 base_level, u32 num_levels,
                    vk::PipelineStageFlags new_stage_mask, vk::AccessFlags new_access,
                    vk::ImageLayout new_layout) {
        image->Transition(base_layer, num_layers, base_level, num_levels, new_stage_mask,
                          new_access, new_layout);
    }

    VKImage& GetImage() {
        return *image;
    }

    const VKImage& GetImage() const {
        return *image;
    }

    vk::Image GetImageHandle() const {
        return image->GetHandle();
    }

    vk::ImageAspectFlags GetAspectMask() const {
        return image->GetAspectMask();
    }

    vk::BufferView GetBufferViewHandle() const {
        return *buffer_view;
    }

protected:
    void DecorateSurfaceName();

    View CreateView(const ViewParams& params) override;
    View CreateViewInner(const ViewParams& params, bool is_proxy);

private:
    void UploadBuffer(const std::vector<u8>& staging_buffer);

    void UploadImage(const std::vector<u8>& staging_buffer);

    vk::BufferImageCopy GetBufferImageCopy(u32 level) const;

    vk::ImageSubresourceRange GetImageSubresourceRange() const;

    Core::System& system;
    const VKDevice& device;
    VKResourceManager& resource_manager;
    VKMemoryManager& memory_manager;
    VKScheduler& scheduler;
    VKStagingBufferPool& staging_pool;

    std::optional<VKImage> image;
    UniqueBuffer buffer;
    UniqueBufferView buffer_view;
    VKMemoryCommit commit;

    vk::Format format;
};

class CachedSurfaceView final : public VideoCommon::ViewBase {
public:
    explicit CachedSurfaceView(const VKDevice& device, CachedSurface& surface,
                               const ViewParams& params, bool is_proxy);
    ~CachedSurfaceView();

    vk::ImageView GetHandle(Tegra::Texture::SwizzleSource x_source,
                            Tegra::Texture::SwizzleSource y_source,
                            Tegra::Texture::SwizzleSource z_source,
                            Tegra::Texture::SwizzleSource w_source);

    bool IsSameSurface(const CachedSurfaceView& rhs) const {
        return &surface == &rhs.surface;
    }

    vk::ImageView GetHandle() {
        return GetHandle(Tegra::Texture::SwizzleSource::R, Tegra::Texture::SwizzleSource::G,
                         Tegra::Texture::SwizzleSource::B, Tegra::Texture::SwizzleSource::A);
    }

    u32 GetWidth() const {
        return params.GetMipWidth(base_level);
    }

    u32 GetHeight() const {
        return params.GetMipHeight(base_level);
    }

    u32 GetNumLayers() const {
        return num_layers;
    }

    bool IsBufferView() const {
        return buffer_view;
    }

    vk::Image GetImage() const {
        return image;
    }

    vk::BufferView GetBufferView() const {
        return buffer_view;
    }

    vk::ImageSubresourceRange GetImageSubresourceRange() const {
        return {aspect_mask, base_level, num_levels, base_layer, num_layers};
    }

    vk::ImageSubresourceLayers GetImageSubresourceLayers() const {
        return {surface.GetAspectMask(), base_level, base_layer, num_layers};
    }

    void Transition(vk::ImageLayout new_layout, vk::PipelineStageFlags new_stage_mask,
                    vk::AccessFlags new_access) const {
        surface.Transition(base_layer, num_layers, base_level, num_levels, new_stage_mask,
                           new_access, new_layout);
    }

    void MarkAsModified(u64 tick) {
        surface.MarkAsModified(true, tick);
    }

private:
    static u32 EncodeSwizzle(Tegra::Texture::SwizzleSource x_source,
                             Tegra::Texture::SwizzleSource y_source,
                             Tegra::Texture::SwizzleSource z_source,
                             Tegra::Texture::SwizzleSource w_source) {
        return (static_cast<u32>(x_source) << 24) | (static_cast<u32>(y_source) << 16) |
               (static_cast<u32>(z_source) << 8) | static_cast<u32>(w_source);
    }

    // Store a copy of these values to avoid double dereference when reading them
    const SurfaceParams params;
    const vk::Image image;
    const vk::BufferView buffer_view;
    const vk::ImageAspectFlags aspect_mask;

    const VKDevice& device;
    CachedSurface& surface;
    const u32 base_layer;
    const u32 num_layers;
    const u32 base_level;
    const u32 num_levels;
    const vk::ImageViewType image_view_type;

    vk::ImageView last_image_view;
    u32 last_swizzle{};

    std::unordered_map<u32, UniqueImageView> view_cache;
};

class VKTextureCache final : public TextureCacheBase {
public:
    explicit VKTextureCache(Core::System& system, VideoCore::RasterizerInterface& rasterizer,
                            const VKDevice& device, VKResourceManager& resource_manager,
                            VKMemoryManager& memory_manager, VKScheduler& scheduler,
                            VKStagingBufferPool& staging_pool);
    ~VKTextureCache();

private:
    Surface CreateSurface(GPUVAddr gpu_addr, const SurfaceParams& params) override;

    void ImageCopy(Surface& src_surface, Surface& dst_surface,
                   const VideoCommon::CopyParams& copy_params) override;

    void ImageBlit(View& src_view, View& dst_view,
                   const Tegra::Engines::Fermi2D::Config& copy_config) override;

    void BufferCopy(Surface& src_surface, Surface& dst_surface) override;

    const VKDevice& device;
    VKResourceManager& resource_manager;
    VKMemoryManager& memory_manager;
    VKScheduler& scheduler;
    VKStagingBufferPool& staging_pool;
};

} // namespace Vulkan
