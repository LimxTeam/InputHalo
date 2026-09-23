#include "Core/Logging/Logging.h"
#include "Platform/Windows/Win32Dpi.h"
#include "Platform/Windows/Win32Paths.h"
#include "Platform/Windows/Win32Window.h"
#include "Render2D/Painter.h"
#include "RHI/Vulkan/VulkanShapeBuffer.h"
#include "Text/TextEngine.h"
#include "Platform/Windows/Win32Fonts.h"
#include <fstream>
#include <iostream>
#include <cmath>

namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class T> void Check(const T& result) { if (!result) throw std::runtime_error(result.error()); }
struct Offscreen {
    ih::render::VulkanContext& context;
    VkImage image{}; VkDeviceMemory imageMemory{}; VkImageView view{};
    VkBuffer readback{}; VkDeviceMemory readbackMemory{};
    VkCommandPool pool{}; VkCommandBuffer command{}; VkFence fence{};
    uint32_t width{}, height{};
    explicit Offscreen(ih::render::VulkanContext& value) : context(value) {}
    ~Offscreen() {
        const auto device = context.Device();
        vkDeviceWaitIdle(device);
        if (fence) vkDestroyFence(device, fence, nullptr);
        if (pool) vkDestroyCommandPool(device, pool, nullptr);
        if (view) vkDestroyImageView(device, view, nullptr);
        if (image) vkDestroyImage(device, image, nullptr);
        if (imageMemory) vkFreeMemory(device, imageMemory, nullptr);
        if (readback) vkDestroyBuffer(device, readback, nullptr);
        if (readbackMemory) vkFreeMemory(device, readbackMemory, nullptr);
    }
    VkDeviceMemory Allocate(VkMemoryRequirements requirements, VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(context.PhysicalDevice(), &memory);
        for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
            if (!(requirements.memoryTypeBits & (1u << i)) || (memory.memoryTypes[i].propertyFlags & flags) != flags) continue;
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = i;
            VkDeviceMemory result{};
            context.Check(vkAllocateMemory(context.Device(), &allocation, nullptr, &result), "Test memory");
            return result;
        }
        throw std::runtime_error("Missing test memory type");
    }
    void Initialize(uint32_t w, uint32_t h, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM) {
        width = w; height = h;
        const auto device = context.Device();
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D; info.format = format;
        info.extent = {w, h, 1}; info.mipLevels = info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        context.Check(vkCreateImage(device, &info, nullptr, &image), "Test image");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, image, &requirements);
        imageMemory = Allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        context.Check(vkBindImageMemory(device, image, imageMemory, 0), "Test image memory");
        VkImageViewCreateInfo imageView{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        imageView.image = image; imageView.viewType = VK_IMAGE_VIEW_TYPE_2D; imageView.format = info.format;
        imageView.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        context.Check(vkCreateImageView(device, &imageView, nullptr, &view), "Test view");
        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer.size = static_cast<VkDeviceSize>(w) * h * 4; buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        context.Check(vkCreateBuffer(device, &buffer, nullptr, &readback), "Test readback");
        vkGetBufferMemoryRequirements(device, readback, &requirements);
        readbackMemory = Allocate(requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        context.Check(vkBindBufferMemory(device, readback, readbackMemory, 0), "Test readback memory");
        VkCommandPoolCreateInfo commandPool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        commandPool.queueFamilyIndex = context.QueueFamily();
        context.Check(vkCreateCommandPool(device, &commandPool, nullptr, &pool), "Test command pool");
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = pool; allocate.commandBufferCount = 1;
        context.Check(vkAllocateCommandBuffers(device, &allocate, &command), "Test commands");
        VkFenceCreateInfo finished{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        context.Check(vkCreateFence(device, &finished, nullptr, &fence), "Test fence");
    }
    std::vector<uint8_t> Render(ih::render::VulkanRenderer2D& renderer, const ih::render2d::DrawList& list, float scale) {
        ih::render::VulkanShapeBuffer instances(context);
        instances.Initialize(renderer.SetLayout()); instances.Upload(list);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        context.Check(vkBeginCommandBuffer(command, &begin), "Test begin");
        instances.RecordUploads(command);
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.image = image; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1; dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(command, &dependency);
        VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        attachment.imageView = view; attachment.imageLayout = barrier.newLayout;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea.extent = {width, height}; rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1; rendering.pColorAttachments = &attachment;
        vkCmdBeginRendering(command, &rendering);
        renderer.Record(command, instances.Set(), list, width, height, scale);
        vkCmdEndRendering(command);
        barrier.oldLayout = barrier.newLayout; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT; barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier2(command, &dependency);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {width, height, 1};
        vkCmdCopyImageToBuffer(command, image, barrier.newLayout, readback, 1, &copy);
        VkMemoryBarrier2 host{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        host.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT; host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT; host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        dependency.imageMemoryBarrierCount = 0; dependency.memoryBarrierCount = 1; dependency.pMemoryBarriers = &host;
        vkCmdPipelineBarrier2(command, &dependency);
        context.Check(vkEndCommandBuffer(command), "Test end");
        VkCommandBufferSubmitInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO}; commands.commandBuffer = command;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2}; submit.commandBufferInfoCount = 1; submit.pCommandBufferInfos = &commands;
        context.Check(vkQueueSubmit2(context.Queue(), 1, &submit, fence), "Test submit");
        const auto waited = vkWaitForFences(context.Device(), 1, &fence, VK_TRUE, 10'000'000'000);
        if (waited != VK_SUCCESS) vkDeviceWaitIdle(context.Device()); // Preserve instance lifetime on timeout.
        context.Check(waited, "Test completion");
        void* mapped{};
        const size_t bytes = static_cast<size_t>(width) * height * 4;
        context.Check(vkMapMemory(context.Device(), readbackMemory, 0, bytes, 0, &mapped), "Test map");
        std::vector<uint8_t> pixels(static_cast<uint8_t*>(mapped), static_cast<uint8_t*>(mapped) + bytes);
        vkUnmapMemory(context.Device(), readbackMemory);
        return pixels;
    }
};
ih::render2d::DrawList Scene() {
    ih::render2d::Painter painter(320, 200);
    painter.FillRect({10, 10, 50, 40}, {1, 0, 0, 1});
    painter.RoundedRect({80, 10, 60, 40}, 12, {0, 1, 0, 1});
    painter.Circle({180, 30}, 20, {0, 0, 1, 1});
    painter.Line({220, 10}, {280, 50}, 6, {1, 1, 0, 1});
    painter.Line({300, 30}, {300, 30}, 8, {1, 0, 1, 1});
    painter.FillRect({10, 80, 60, 60}, {1, 0, 0, .5f});
    painter.FillRect({30, 100, 60, 60}, {0, 0, 1, .5f});
    painter.PushClip({110, 80, 40, 40});
    painter.FillRect({90, 60, 100, 100}, {0, 1, 1, 1});
    painter.PushClip({130, 100, 40, 40});
    painter.FillRect({90, 60, 100, 100}, {1, 1, 1, 1});
    painter.PopClip(); painter.PopClip();
    painter.Circle({240, 120}, 23.25f, {1, .5f, 0, 1});
    return painter.List();
}
void Save(const std::filesystem::path& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& pixels) {
    // BMP preview composites premultiplied pixels over a checkerboard; tests use raw RGBA.
    BITMAPFILEHEADER file{}; file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + width * height * 4;
    BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = width; info.biHeight = -static_cast<LONG>(height);
    info.biPlanes = 1; info.biBitCount = 32;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(&file), sizeof(file)); output.write(reinterpret_cast<const char*>(&info), sizeof(info));
    for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
        const auto* p = pixels.data() + (static_cast<size_t>(y) * width + x) * 4;
        const unsigned background = ((x / 12 + y / 12) % 2) ? 52 : 40;
        uint8_t color[4]{};
        for (unsigned c = 0; c < 3; ++c) color[2 - c] = static_cast<uint8_t>(p[c] + background * (255 - p[3]) / 255);
        output.write(reinterpret_cast<const char*>(color), 4);
    }
    Require(static_cast<bool>(output), "Cannot save preview");
}
void CheckGlyphPixels(const std::vector<uint8_t>& pixels, uint32_t width, const ih::text::Layout& layout,
    float scale, float clipRight) {
    Require(layout.glyphs.size() == 1, "Reference test requires one glyph");
    const auto& glyph = layout.glyphs[0];
    const auto& atlas = *layout.atlas;
    const auto& page = atlas.pages[glyph.page];
    unsigned visible = 0;
    for (uint32_t y = 0; y < static_cast<uint32_t>(48 * scale); ++y)
        for (uint32_t x = 0; x < static_cast<uint32_t>(40 * scale); ++x) {
            const float dipX = (x + .5f) / scale, dipY = (y + .5f) / scale;
            const float left = 10 + glyph.x, top = 8 + glyph.y;
            float coverage = 0;
            if (dipX >= left && dipX < left + glyph.width && dipY >= top && dipY < top + glyph.height && dipX < clipRight) {
                const float u = (glyph.u + (dipX - left) / glyph.width * (glyph.uEnd - glyph.u)) * atlas.size - .5f;
                const float v = (glyph.v + (dipY - top) / glyph.height * (glyph.vEnd - glyph.v)) * atlas.size - .5f;
                const auto ix = static_cast<uint32_t>(std::floor(u)), iy = static_cast<uint32_t>(std::floor(v));
                const float fx = u - std::floor(u), fy = v - std::floor(v);
                const auto sample = [&](uint32_t a, uint32_t b) { return page[static_cast<size_t>(b) * atlas.size + a] / 255.f; };
                coverage = (sample(ix, iy) * (1 - fx) + sample(ix + 1, iy) * fx) * (1 - fy) +
                    (sample(ix, iy + 1) * (1 - fx) + sample(ix + 1, iy + 1) * fx) * fy;
            }
            const float alpha = coverage * .75f;
            const float expected[]{alpha * .4f, alpha * .8f, alpha, alpha};
            const auto offset = (static_cast<size_t>(y) * width + x) * 4;
            for (unsigned channel = 0; channel < 4; ++channel)
                Require(std::abs(static_cast<int>(pixels[offset + channel]) - static_cast<int>(std::round(expected[channel] * 255))) <= 2,
                    "GPU glyph sampling/clip/premultiplied-alpha differs from CPU atlas reference");
            visible += coverage > .1f;
        }
    Require(visible > 10, "Text reference must contain visible glyph pixels");
}
}
int main() {
    try {
        Check(ih::platform::InitializeDpi());
        ih::Logging logging(std::filesystem::current_path() / "renderer2d-smoke-logs");
        ih::platform::Win32Window window; Check(window.Create(GetModuleHandleW(nullptr)));
        ih::render::VulkanContext context; Check(context.Initialize(GetModuleHandleW(nullptr), window.Handle()));
        const auto directory = ih::platform::ExecutableDirectory(); Check(directory);
        const auto results = std::filesystem::current_path() / "renderer2d-results";
        std::filesystem::create_directories(results);
        {
            ih::render::VulkanRenderer2D renderer(context);
            renderer.Initialize(*directory / "Shaders/Renderer2D.vert.spv");
            const auto list = Scene();
            Require(list.commands.size() == 10 && list.batches.size() == 4, "Scene must use four ordered batches");
            for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
                Offscreen target(context);
                target.Initialize(static_cast<uint32_t>(320 * scale), static_cast<uint32_t>(200 * scale));
                const auto pixels = target.Render(renderer, list, scale);
                const auto sample = [&](float x, float y, std::array<int, 4> expected) {
                    const auto offset = (static_cast<size_t>(y * scale) * target.width + static_cast<size_t>(x * scale)) * 4;
                    for (size_t c = 0; c < 4; ++c) if (std::abs(static_cast<int>(pixels[offset + c]) - expected[c]) > 2)
                        throw std::runtime_error("Pixel mismatch at scale " + std::to_string(scale) + " point " + std::to_string(x) + "," + std::to_string(y));
                };
                sample(2, 2, {0, 0, 0, 0}); sample(20, 20, {255, 0, 0, 255});
                sample(100, 25, {0, 255, 0, 255}); sample(80, 10, {0, 0, 0, 0});
                sample(180, 30, {0, 0, 255, 255}); sample(250, 30, {255, 255, 0, 255});
                sample(300, 30, {255, 0, 255, 255}); sample(20, 90, {128, 0, 0, 128});
                sample(40, 110, {64, 0, 128, 192}); sample(100, 90, {0, 0, 0, 0});
                sample(120, 90, {0, 255, 255, 255}); sample(140, 110, {255, 255, 255, 255});
                sample(151, 110, {0, 0, 0, 0}); sample(240, 120, {255, 128, 0, 255});
                unsigned partial = 0;
                for (uint32_t y = static_cast<uint32_t>(95 * scale); y < static_cast<uint32_t>(145 * scale); ++y)
                    for (uint32_t x = static_cast<uint32_t>(215 * scale); x < static_cast<uint32_t>(265 * scale); ++x) {
                        const auto alpha = pixels[(static_cast<size_t>(y) * target.width + x) * 4 + 3];
                        partial += alpha > 0 && alpha < 255;
                    }
                Require(partial > 20, "SDF edge must have fractional coverage");
                Save(results / (std::to_string(static_cast<int>(scale * 100)) + ".bmp"), target.width, target.height, pixels);
                std::cout << "DPI " << scale * 100 << "%: 14 pixel probes, alpha ordering, nested scissor, AA passed; 4 draws.\n";
            }
        }
        {
            ih::render::VulkanRenderer2D renderer(context);
            renderer.Initialize(*directory / "Shaders/Renderer2D.vert.spv", VK_FORMAT_R8G8B8A8_SRGB);
            Offscreen target(context);
            target.Initialize(320, 200, VK_FORMAT_R8G8B8A8_SRGB);
            const auto pixels = target.Render(renderer, Scene(), 1);
            const auto orange = (120 * 320 + 240) * 4;
            Require(std::abs(static_cast<int>(pixels[orange + 1]) - 128) <= 2, "sRGB color conversion");
            const auto overlap = (110 * 320 + 40) * 4;
            Require(std::abs(static_cast<int>(pixels[overlap]) - 137) <= 2 &&
                std::abs(static_cast<int>(pixels[overlap + 2]) - 188) <= 2 &&
                std::abs(static_cast<int>(pixels[overlap + 3]) - 192) <= 2, "sRGB attachment must blend in linear space");
            std::cout << "sRGB attachment: color roundtrip and linear alpha blending passed.\n";
        }
        {
            const auto fonts = ih::platform::DefaultFontPaths();
            ih::text::TextEngine text;
            for (const auto& font : fonts) text.AddFont(font);
            ih::render::VulkanRenderer2D renderer(context);
            renderer.Initialize(*directory / "Shaders/Renderer2D.vert.spv");
            for (const float scale : {1.f, 1.25f, 1.5f, 2.f}) {
                ih::render2d::Painter painter(640, 220);
                const auto reference = text.Shape("H", 24, scale);
                const float clipRight = 10 + reference.glyphs[0].x + reference.glyphs[0].width * .8f;
                painter.PushClip({0, 0, clipRight, 220});
                painter.Text(reference, {10, 8}, {.4f, .8f, 1, .75f});
                painter.PopClip();
                painter.Text(text.Shape("InputHalo • office AV 0123456789", 24, scale), {50, 8}, {.9f, .95f, 1, 1});
                const auto chinese = text.Shape("输入光晕 · 中文与高 DPI", 24, scale);
                Require(!chinese.missingGlyphs, "Chinese fallback missing");
                painter.Text(chinese, {50, 58}, {.46f, .90f, .78f, 1});
                painter.Text(text.Shape("Καλημέρα — café — e\xcc\x81", 24, scale), {50, 108}, {1, .8f, .45f, 1});
                painter.Text(text.Shape("العربية", 24, scale), {50, 158}, {1, .7f, .8f, 1});
                Offscreen target(context);
                target.Initialize(static_cast<uint32_t>(640 * scale), static_cast<uint32_t>(220 * scale));
                const auto pixels = target.Render(renderer, painter.List(), scale);
                CheckGlyphPixels(pixels, target.width, reference, scale, clipRight);
                Save(results / ("text-" + std::to_string(static_cast<int>(scale * 100)) + ".bmp"), target.width, target.height, pixels);
                std::cout << "Text DPI " << scale * 100 << "%: atlas pixels, clipping and alpha reference passed.\n";
            }
            ih::text::TextEngine small(64, 8);
            small.AddFont(fonts.front());
            const auto preload = small.Shape("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", 18);
            const auto laterPage = small.Shape("z", 18);
            Require(laterPage.glyphs[0].page > 0, "Expected glyph on later atlas page");
            ih::render2d::Painter painter(640, 220);
            painter.Text(laterPage, {10, 8}, {.4f, .8f, 1, .75f});
            Offscreen target(context); target.Initialize(640, 220);
            const auto pixels = target.Render(renderer, painter.List(), 1);
            CheckGlyphPixels(pixels, target.width, laterPage, 1, 640);
            std::cout << "Multi-page R8 atlas GPU sampling passed.\n";
        }
        context.Shutdown();
        Require(context.ValidationErrors() == 0 && context.ValidationWarnings() == 0, "Vulkan validation not clean");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
