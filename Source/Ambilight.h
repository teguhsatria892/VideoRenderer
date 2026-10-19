#pragma once

#include "AmbilightSettings.h"

inline constexpr char AmbilightDownsampleShader[] = R"hlsl(
Texture2D tex : register(t0);
SamplerState samp : register(s0);
cbuffer Params : register(b0) { float4 step; };
float3 readColor(float2 uv) {
    float3 color = max(tex.Sample(samp, uv).rgb, 0);
    return step.z > 0 ? pow(color, 2.2) : color;
}
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD) : SV_Target {
    float3 color = readColor(uv + step.xy) + readColor(uv - step.xy)
        + readColor(uv + step.xy * float2(1, -1))
        + readColor(uv + step.xy * float2(-1, 1));
    return float4(color * 0.25, 1);
}
)hlsl";

inline constexpr char AmbilightBlurShader[] = R"hlsl(
Texture2D tex : register(t0);
SamplerState samp : register(s0);
cbuffer Params : register(b0) { float4 step; float4 videoRect; };
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD) : SV_Target {
    float3 color = 0;
    float weightSum = 0;
    [unroll] for (int i = -6; i <= 6; ++i) {
        float weight = exp(-float(i * i) / 18.0);
        color += tex.Sample(samp, saturate(uv + i * step.xy)).rgb * weight;
        weightSum += weight;
    }
    return float4(color / weightSum, 1);
}
)hlsl";

inline constexpr char AmbilightCompositeShader[] = R"hlsl(
Texture2D tex : register(t0);
Texture2D nextLevel : register(t1);
SamplerState samp : register(s0);
cbuffer Params : register(b0) {
    float4 settings; // intensity, spread pixels, fade start, fade exponent
    float4 videoRect;
    float4 filters; // brightness, contrast, saturation, vibrance
    float4 details; // edge fraction, directions mask, noise strength, level blend
};
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD) : SV_Target {
    // Never touch the sharp video, even when it is cropped or off-centre.
    if (pos.x >= videoRect.x && pos.y >= videoRect.y &&
        pos.x < videoRect.z && pos.y < videoRect.w) discard;
    float2 edge = max(videoRect.xy - pos.xy, pos.xy - videoRect.zw);
    float distance = max(max(edge.x, edge.y), 0);
    uint directions = (uint)details.y;
    if ((pos.y < videoRect.y && !(directions & 1)) ||
        (pos.x >= videoRect.z && !(directions & 2)) ||
        (pos.y >= videoRect.w && !(directions & 4)) ||
        (pos.x < videoRect.x && !(directions & 8))) discard;
    float fade = pow(saturate(1 - smoothstep(settings.z, 1, distance / max(settings.y, 0.001))), settings.w);
    float2 sampleUV = saturate((pos.xy - videoRect.xy) / max(videoRect.zw - videoRect.xy, 1));
    float2 inset = details.x * 0.5;
    sampleUV = clamp(sampleUV, inset, 1 - inset);
    float3 color = tex.Sample(samp, sampleUV).rgb;
    if (details.w > 0) color = lerp(color, nextLevel.Sample(samp, sampleUV).rgb, details.w);
    color = pow(max(color, 0), 1.0 / 2.2);
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    float chroma = max(max(color.r, color.g), color.b) - min(min(color.r, color.g), color.b);
    color = lerp(luma.xxx, color, max(0, 1 + (filters.w - 1) * (1 - chroma)));
    color = lerp(luma.xxx, color, filters.z);
    color = max(0, (color - 0.5) * filters.y + 0.5) * filters.x;
    float noise = (frac(sin(dot(pos.xy, float2(12.9898, 78.233))) * 43758.5453) - 0.5) * details.z / 255;
    return float4(saturate(color + noise) * settings.x * fade, 1);
}
)hlsl";
