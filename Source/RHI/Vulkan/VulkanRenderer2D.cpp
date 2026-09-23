/*******************************************************************************
 * 文件: VulkanRenderer2D.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   共享 SDF 形状管线实现：从 SPIR-V 创建 2D 图形管线（无顶点输入，靠 storage buffer 实例化），
 *   并把 DrawList 的批次翻译为 scissor + draw 调用。
 *   不涉及复杂算法；录制为 O(批次数)，每个批次一次 draw。
 *   所有目标共享一条管线，仅各帧自带实例缓冲，最大化管线复用。
 *
 * 设计哲学:
 *   采用"无传统顶点输入"设计：顶点着色器按 gl_VertexIndex(6 个顶点画一个 quad)
 *   与 gl_InstanceIndex 索引 storage buffer 中的 Command，形状类型在着色器内分支计算 SDF。
 *   混合用 One/One-Minus-SrcAlpha（预乘 alpha），viewport/scissor 动态以支持 DPI 缩放与裁剪。
 *
 * 技术特性:
 *   - SPIR-V 校验: 读取时检查魔数 0x07230203 与 4 字节对齐。
 *   - 动态渲染: VkPipelineRenderingCreateInfo 直接指定附件格式，无需 RenderPass。
 *   - 预乘混合: 颜色/alpha 均 ONE / ONE_MINUS_SRC_ALPHA。
 *   - Push constant: 16 字节视图参数（宽高/缩放/是否线性输出）。
 *
 * 依赖关系:
 *   内部: Limx::ih::render、Limx::render2d::DrawList
 *   外部: Vulkan 1.4、SPIR-V 着色器(.spv)、<fstream>/<vector>
 *
 * 注意事项:
 *   着色器路径缺失或 SPIR-V 非法会抛异常；frag 失败时需回收已建的 vertex module。
 *   线性输出标志依据附件格式是否为 sRGB 推断，影响着色器 gamma 处理。
 *   管线在 Renderer2D 生命周期内共享，目标通过描述符集传入各自的实例缓冲与图集。
 *
 ******************************************************************************/
#include "RHI/Vulkan/VulkanRenderer2D.h"
#include <fstream>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace ih::render {
namespace {
// (匿名) Load: 从文件读取 SPIR-V 并校验魔数/对齐/大小，创建 VkShaderModule。
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
// Initialize: 创建描述符集布局、管线布局、加载着色器并构建图形管线。
void VulkanRenderer2D::Initialize(const std::filesystem::path& shader, VkFormat format) {
// 根据附件是否 sRGB 决定是否告知着色器做线性输出。
    m_linearOutput = format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB;
// 描述符绑定: 0=实例 storage buffer, 1=图集采样图像, 2=图集采样器。
    const VkDescriptorSetLayoutBinding bindings[]{
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set.bindingCount = 3; set.pBindings = bindings;
    m_context.Check(vkCreateDescriptorSetLayout(m_context.Device(), &set, nullptr, &m_setLayout), "2D descriptor layout");
// Push constant: 跨顶点/片元阶段传递 16 字节视图参数。
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1; layout.pSetLayouts = &m_setLayout;
    layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
    m_context.Check(vkCreatePipelineLayout(m_context.Device(), &layout, nullptr, &m_pipelineLayout), "2D pipeline layout");
// 加载顶点着色器；片元失败时回收顶点 module 再抛出，避免泄漏。
    const auto vertex = Load(m_context, shader);
    VkShaderModule fragment{};
    try { fragment = Load(m_context, shader.parent_path() / "Renderer2D.frag.spv"); }
    catch (...) { vkDestroyShaderModule(m_context.Device(), vertex, nullptr); throw; }
// 两个着色阶段：顶点 vsMain、片元 psMain。
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (auto& stage : stages) stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertex; stages[0].pName = "vsMain";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragment; stages[1].pName = "psMain";
// 无顶点输入（实例化由 storage buffer 提供），图元为三角形列表。
    VkPipelineVertexInputStateCreateInfo vertices{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
// 颜色混合：预乘 alpha（One / One-Minus-SrcAlpha），写 RGBA 全通道。
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.blendEnable = VK_TRUE;
    attachment.srcColorBlendFactor = attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstColorBlendFactor = attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorWriteMask = 15;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1; blend.pAttachments = &attachment;
// viewport 与 scissor 设为动态，便于运行时按 DPI 与批次裁剪调整。
    const VkDynamicState dynamicStates[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = dynamicStates;
// 动态渲染信息：单颜色附件，格式与目标一致。
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline.pNext = &rendering; pipeline.layout = m_pipelineLayout;
    pipeline.stageCount = 2; pipeline.pStages = stages;
    pipeline.pVertexInputState = &vertices; pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &viewport; pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &samples; pipeline.pColorBlendState = &blend; pipeline.pDynamicState = &dynamic;
// 创建管线后立即销毁临时 shader module（管线已克隆其内容）。
    const auto result = vkCreateGraphicsPipelines(m_context.Device(), VK_NULL_HANDLE, 1, &pipeline, nullptr, &m_pipeline);
    vkDestroyShaderModule(m_context.Device(), vertex, nullptr);
    vkDestroyShaderModule(m_context.Device(), fragment, nullptr);
    m_context.Check(result, "Renderer2D graphics pipeline");
    m_context.Name(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_pipeline), "InputHalo.Renderer2D.Shapes");
}
// Record: 绑定管线/描述符、下发视图 push constant，按批次设 scissor 并 draw。
void VulkanRenderer2D::Record(VkCommandBuffer command, VkDescriptorSet set, const render2d::DrawList& list,
    uint32_t width, uint32_t height, float scale) const {
    if (!width || !height || !std::isfinite(scale) || scale <= 0)
        throw std::invalid_argument("Invalid Renderer2D viewport");
    const VkViewport viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &set, 0, nullptr);
// View push constant: 宽高/DPI 缩放/是否线性输出。
    struct View { float width, height, scale; uint32_t linear; };
    const View view{static_cast<float>(width), static_cast<float>(height), scale, m_linearOutput ? 1u : 0u};
    vkCmdPushConstants(command, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(view), &view);
// 遍历批次：把 DIP 裁剪矩形按 scale 对齐到像素 scissor，跳过空区间后绘制。
    for (const auto& batch : list.batches) {
// edge: DIP->像素的 rounding 对齐（ceil-0.5），避免裁剪边缘漏像素。
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
// 析构：释放管线、管线布局与描述符集布局。
VulkanRenderer2D::~VulkanRenderer2D() {
    if (m_pipeline) vkDestroyPipeline(m_context.Device(), m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(m_context.Device(), m_pipelineLayout, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(m_context.Device(), m_setLayout, nullptr);
}
}

