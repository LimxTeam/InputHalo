[[vk::binding(0, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> outputImage;
struct Circle { float2 center; float radius; float opacity; };
[[vk::push_constant]] ConstantBuffer<Circle> circle;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    outputImage.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    float distance = length(float2(id.xy) + 0.5 - circle.center);
    float alpha = circle.opacity * (1.0 - smoothstep(circle.radius - 1.0, circle.radius + 1.0, distance));
    outputImage[id.xy] = float4(float3(117.0, 230.0, 200.0) / 255.0 * alpha, alpha);
}
