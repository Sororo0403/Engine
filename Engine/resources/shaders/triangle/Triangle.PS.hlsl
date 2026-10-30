#include "Triangle.hlsli"

Texture2D<float4> texture0 : register(t0);
SamplerState sampler0 : register(s0);

float4 PSMain(VertexOutput input) : SV_Target0
{
	float4 textureColor =
	    texture0.Sample(sampler0, input.texcoord);

	// 法線の-1～1を、色の0～1に変換する
	float3 normalColor = normalize(input.normal) * 0.5f + 0.5f;

	return textureColor * float4(normalColor, 1.0f);
}
