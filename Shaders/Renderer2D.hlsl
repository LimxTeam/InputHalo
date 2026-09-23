/*******************************************************************************
 * 文件: Renderer2D.hlsl
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   2D 矢量渲染的顶点/像素着色器(Vulkan HLSL)。
 *   顶点着色器按命令实例化六顶点四边形并做 DIP→裁剪空间变换；
 *   像素着色器按图元类型(文本/辉光/圆环/直线/圆角矩形/SDF)计算覆盖率，输出预乘 alpha。
 *   复杂度: 每像素一次 SDF 求值 O(1)。
 *
 * 设计哲学:
 *   命令式绘制：一个 StructuredBuffer<Command> 描述所有图元，实例化管线零顶点缓冲；
 *   SDF 抗锯齿用 fwidth 推导平滑带宽，保证任意缩放下边缘一致锐利。
 *
 * 技术特性:
 *   - parameters.x 区分图元类型；glyph 走 Texture2DArray 单通道采样
 *   - 可选 sRGB→linear 转换由 push constant 控制
 *   - 输出预乘 alpha，适配透明合成
 *
 * 依赖关系:
 *   Vulkan HLSL 绑定(vk::binding / push_constant)
 *   图集为单通道 float 数组纹理
 *
 * 注意事项:
 *   输出为预乘 alpha；线性输出开关适配 sRGB 交换链。
 ******************************************************************************/
// 绘制命令：一个命令描述一个图元的包围盒、颜色、参数、端点。
struct Command { float4 bounds; float4 color; float4 parameters; float4 endpoints; };
// 绑定 0：所有图元命令的结构化缓冲(实例化索引即命令下标)。
[[vk::binding(0, 0)]] StructuredBuffer<Command> commands;
// 绑定 1：字形图集(数组纹理，单通道灰度)。
[[vk::binding(1, 0)]] Texture2DArray<float> glyphAtlas;
// 绑定 2：字形采样器。
[[vk::binding(2, 0)]] SamplerState glyphSampler;
// 视图 push constant：视口尺寸、DPI 缩放、是否输出线性空间。
struct View { float2 extent; float scale; uint linearOutput; };
// push constant 块(高效，无需常量缓冲)。
[[vk::push_constant]] ConstantBuffer<View> view;
// 顶点着色器输出结构。
struct Vertex {
// 裁剪空间位置。
    float4 position : SV_Position;
// 图元本地坐标(供像素着色器求 SDF)。
    float2 localPosition : TEXCOORD0;
// 实例索引(不插值)，用于取回该图元命令。
    nointerpolation uint index : TEXCOORD1;
};
// 顶点着色器：由 SV_VertexID/SV_InstanceID 生成一个四边形。
Vertex vsMain(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
// 两个三角形组成四边形的六个角点。
    const float2 corners[6] = {float2(0,0), float2(1,0), float2(0,1), float2(0,1), float2(1,0), float2(1,1)};
// 取本实例对应的图元命令。
    Command shape = commands[instance];
// 文本图元不加边距；其余图元按 2 物理像素留抗锯齿边距。
    float padding = shape.parameters.x == 4 ? 0 : 2.0 / view.scale;
// 计算本地坐标(包围盒 + 边距)。
    float2 localPosition = shape.bounds.xy - padding + corners[vertex] * (shape.bounds.zw + 2 * padding);
    Vertex result;
// DIP 本地坐标 -> 裁剪空间 [-1,1]。
    result.position = float4(localPosition * view.scale / view.extent * 2 - 1, 0, 1);
    result.localPosition = localPosition;
    result.index = instance;
    return result;
}
// 像素着色器：按图元类型计算覆盖率并输出预乘 alpha。
float4 psMain(Vertex input) : SV_Target {
    Command shape = commands[input.index];
// 图元中心与半径。
    float2 center = shape.bounds.xy + shape.bounds.zw * .5;
    float radius = shape.parameters.y;
    float distance;
    float coverage;
// type==4：文本字形，采样图集 UV。
    if (shape.parameters.x == 4) {
        float2 fraction = (input.localPosition - shape.bounds.xy) / shape.bounds.zw;
        float2 uv = lerp(shape.endpoints.xy, shape.endpoints.zw, fraction);
        coverage = glyphAtlas.Sample(glyphSampler, float3(uv, shape.parameters.z));
// type==6：辉光(距离平方衰减)。
    } else if (shape.parameters.x == 6) {
        float normalized = length(input.localPosition - center) / max(radius, .001);
        coverage = pow(saturate(1 - normalized), 2);
    } else {
// type==5：圆环(带描边厚度)。
    if (shape.parameters.x == 5) distance = abs(length(input.localPosition - center) - max(0, radius - shape.parameters.z * .5)) - shape.parameters.z * .5;
    else
// type==2：实心圆。
    if (shape.parameters.x == 2) distance = length(input.localPosition - center) - radius;
// type==3：线段胶囊体。
    else if (shape.parameters.x == 3) {
        float2 segment = shape.endpoints.zw - shape.endpoints.xy;
        float lengthSquared = dot(segment, segment);
        float t = lengthSquared > 0 ? saturate(dot(input.localPosition - shape.endpoints.xy, segment) / lengthSquared) : 0;
        distance = length(input.localPosition - shape.endpoints.xy - t * segment) - radius;
    } else {
// 默认(type==1)：带圆角的矩形 SDF。
        float2 q = abs(input.localPosition - center) - shape.bounds.zw * .5 + radius;
        distance = length(max(q, 0)) + min(max(q.x, q.y), 0) - radius;
    }
// 用屏幕空间导数求抗锯齿带宽。
    float aa = max(fwidth(distance), .0001);
// SDF 覆盖率：在 0 附近平滑过渡实现抗锯齿。
    coverage = 1 - smoothstep(-aa * .5, aa * .5, distance);
    }
// 预乘 alpha：颜色乘以覆盖率。
    float alpha = shape.color.a * coverage;
    float3 color = shape.color.rgb;
    // SRGB attachments expect linear shader output; composition UNORM uses sRGB values.
// 若交换链为 sRGB，把已存的 sRGB 值转到线性后再输出。
    if (view.linearOutput != 0) color = select(color <= .04045, color / 12.92, pow((color + .055) / 1.055, 2.4));
    return float4(color * alpha, alpha);
}

