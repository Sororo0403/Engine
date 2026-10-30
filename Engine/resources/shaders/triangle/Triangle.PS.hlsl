#include "Triangle.hlsli"

Texture2D<float4> texture0 : register(t0);
SamplerState sampler0 : register(s0);

float4 PSMain(VertexOutput input) : SV_Target0
{
	return texture0.Sample(sampler0, input.texcoord);
}
