#include "Triangle.hlsli"

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
    const float2 positions[3] =
    {
        float2(0.0f, 0.5f),
		float2(0.5f, -0.5f),
		float2(-0.5f, -0.5f)
    };

    const float3 colors[3] =
    {
        float3(1.0f, 0.0f, 0.0f),
		float3(0.0f, 1.0f, 0.0f),
		float3(0.0f, 0.0f, 1.0f)
    };

    VertexOutput output;
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    output.color = float4(colors[vertexId], 1.0f);
    return output;
}
