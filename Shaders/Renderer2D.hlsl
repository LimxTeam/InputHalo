struct Command { float4 bounds; float4 color; float4 parameters; float4 endpoints; };
[[vk::binding(0, 0)]] StructuredBuffer<Command> commands;
[[vk::binding(1, 0)]] Texture2DArray<float> glyphAtlas;
[[vk::binding(2, 0)]] SamplerState glyphSampler;
struct View { float2 extent; float scale; uint linearOutput; };
[[vk::push_constant]] ConstantBuffer<View> view;
struct Vertex {
    float4 position : SV_Position;
    float2 localPosition : TEXCOORD0;
    nointerpolation uint index : TEXCOORD1;
};
Vertex vsMain(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
    const float2 corners[6] = {float2(0,0), float2(1,0), float2(0,1), float2(0,1), float2(1,0), float2(1,1)};
    Command shape = commands[instance];
    float padding = shape.parameters.x == 4 ? 0 : 2.0 / view.scale;
    float2 localPosition = shape.bounds.xy - padding + corners[vertex] * (shape.bounds.zw + 2 * padding);
    Vertex result;
    result.position = float4(localPosition * view.scale / view.extent * 2 - 1, 0, 1);
    result.localPosition = localPosition;
    result.index = instance;
    return result;
}
float4 psMain(Vertex input) : SV_Target {
    Command shape = commands[input.index];
    float2 center = shape.bounds.xy + shape.bounds.zw * .5;
    float radius = shape.parameters.y;
    float distance;
    float coverage;
    if (shape.parameters.x == 4) {
        float2 fraction = (input.localPosition - shape.bounds.xy) / shape.bounds.zw;
        float2 uv = lerp(shape.endpoints.xy, shape.endpoints.zw, fraction);
        coverage = glyphAtlas.Sample(glyphSampler, float3(uv, shape.parameters.z));
    } else if (shape.parameters.x == 6) {
        float normalized = length(input.localPosition - center) / max(radius, .001);
        coverage = pow(saturate(1 - normalized), 2);
    } else {
    if (shape.parameters.x == 5) distance = abs(length(input.localPosition - center) - max(0, radius - shape.parameters.z * .5)) - shape.parameters.z * .5;
    else
    if (shape.parameters.x == 2) distance = length(input.localPosition - center) - radius;
    else if (shape.parameters.x == 3) {
        float2 segment = shape.endpoints.zw - shape.endpoints.xy;
        float lengthSquared = dot(segment, segment);
        float t = lengthSquared > 0 ? saturate(dot(input.localPosition - shape.endpoints.xy, segment) / lengthSquared) : 0;
        distance = length(input.localPosition - shape.endpoints.xy - t * segment) - radius;
    } else {
        float2 q = abs(input.localPosition - center) - shape.bounds.zw * .5 + radius;
        distance = length(max(q, 0)) + min(max(q.x, q.y), 0) - radius;
    }
    float aa = max(fwidth(distance), .0001);
    coverage = 1 - smoothstep(-aa * .5, aa * .5, distance);
    }
    float alpha = shape.color.a * coverage;
    float3 color = shape.color.rgb;
    // SRGB attachments expect linear shader output; composition UNORM uses sRGB values.
    if (view.linearOutput != 0) color = select(color <= .04045, color / 12.92, pow((color + .055) / 1.055, 2.4));
    return float4(color * alpha, alpha);
}

