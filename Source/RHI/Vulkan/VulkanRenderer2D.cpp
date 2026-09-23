#include "RHI/Vulkan/VulkanRenderer2D.h"
#include <fstream>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace ih::render {
namespace {
VkShaderModule Load(VulkanContext& context, const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Missing Renderer2D shader: " + path.string());
    const auto bytes = input.tellg();
    if (bytes < 20 || bytes % 4 != 0 || bytes > 1024 * 1024) throw std::runtime_error("Invalid Renderer2D SPIR-V size");
    std::vector<uint32_t> code(static_cast<size_t>(bytes) / 4);
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(code.data()), bytes) || code.front() != 0x07230203)
        throw std::runtime_error("Invalid Renderer2D SPIR-V data");
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = static_cast<size_t>(bytes); info.pCode = code.data();
    VkShaderModule module{};
    context.Check(vkCreateShaderModule(context.Device(), &info, nullptr, &module), "2D shader");
    return module;
}
}
void VulkanRenderer2D::Initialize(const std::filesystem::path& shader, VkFormat format) {
    m_linearOutput = format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB;
    const VkDescriptorSetLayoutBinding bindings[]{
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set.bindingCount = 3; set.pBindings = bindings;
    m_context.Check(vkCreateDescriptorSetLayout(m_context.Device(), &set, nullptr, &m_setLayout), "2D descriptor layout");
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1; layout.pSetLayouts = &m_setLayout;
    layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
    m_context.Check(vkCreatePipelineLayout(m_context.Device(), &layout, nullptr, &m_pipelineLayout), "2D pipeline layout");
    const auto vertex = Load(m_context, shader);
    VkShaderModule fragment{};
    try { fragment = Load(m_context, shader.parent_path() / "Renderer2D.frag.spv"); }
    catch (...) { vkDestroyShaderModule(m_context.Device(), vertex, nullptr); throw; }
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (auto& stage : stages) stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertex; stages[0].pName = "vsMain";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragment; stages[1].pName = "psMain";
    VkPipelineVertexInputStateCreateInfo vertices{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.blendEnable = VK_TRUE;
    attachment.srcColorBlendFactor = attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstColorBlendFactor = attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorWriteMask = 15;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1; blend.pAttachments = &attachment;
    const VkDynamicState dynamicStates[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = dynamicStates;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline.pNext = &rendering; pipeline.layout = m_pipelineLayout;
    pipeline.stageCount = 2; pipeline.pStages = stages;
    pipeline.pVertexInputState = &vertices; pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &viewport; pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &samples; pipeline.pColorBlendState = &blend; pipeline.pDynamicState = &dynamic;
    const auto result = vkCreateGraphicsPipelines(m_context.Device(), VK_NULL_HANDLE, 1, &pipeline, nullptr, &m_pipeline);
    vkDestroyShaderModule(m_context.Device(), vertex, nullptr);
    vkDestroyShaderModule(m_context.Device(), fragment, nullptr);
    m_context.Check(result, "Renderer2D graphics pipeline");
    m_context.Name(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_pipeline), "InputHalo.Renderer2D.Shapes");
}
void VulkanRenderer2D::Record(VkCommandBuffer command, VkDescriptorSet set, const render2d::DrawList& list,
    uint32_t width, uint32_t height, float scale) const {
    if (!width || !height || !std::isfinite(scale) || scale <= 0)
        throw std::invalid_argument("Invalid Renderer2D viewport");
    const VkViewport viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &set, 0, nullptr);
    struct View { float width, height, scale; uint32_t linear; };
    const View view{static_cast<float>(width), static_cast<float>(height), scale, m_linearOutput ? 1u : 0u};
    vkCmdPushConstants(command, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(view), &view);
    for (const auto& batch : list.batches) {
        const auto edge = [scale](float value, uint32_t limit) {
            return static_cast<int32_t>(std::clamp(std::ceil(value * scale - .5f), 0.f, static_cast<float>(limit)));
        };
        const auto left = edge(batch.clip.x, width), top = edge(batch.clip.y, height);
        const auto right = edge(batch.clip.x + batch.clip.width, width), bottom = edge(batch.clip.y + batch.clip.height, height);
        if (right <= left || bottom <= top) continue;
        const VkRect2D scissor{{left, top}, {static_cast<uint32_t>(right - left), static_cast<uint32_t>(bottom - top)}};
        vkCmdSetScissor(command, 0, 1, &scissor);
        vkCmdDraw(command, 6, batch.count, 0, batch.first);
    }
}
VulkanRenderer2D::~VulkanRenderer2D() {
    if (m_pipeline) vkDestroyPipeline(m_context.Device(), m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(m_context.Device(), m_pipelineLayout, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(m_context.Device(), m_setLayout, nullptr);
}
}

