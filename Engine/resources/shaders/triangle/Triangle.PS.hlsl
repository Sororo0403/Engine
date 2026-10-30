#include "Triangle.hlsli"

Texture2D<float4> texture0 : register(t0);
SamplerState sampler0 : register(s0);

float4 PSMain(VertexOutput input) : SV_Target0
{
	float4 textureColor =
	    texture0.Sample(sampler0, input.texcoord);

	// 補間された法線を長さ1にする
	float3 normal = normalize(input.normal);

	// ワールド座標での、光が進む方向
	float3 lightDirection =
	    normalize(float3(1.0f, -1.0f, 1.0f));

	// 面から光源へ向かう方向と比較する
	float diffuse = saturate(dot(normal, -lightDirection));

	// 光が当たらない面にも少し明るさを残す
	float ambient = 0.2f;
	float brightness = ambient + diffuse * 0.8f;

	float3 color =
	    textureColor.rgb * input.color.rgb * brightness;

	return float4(color, textureColor.a * input.color.a);
}
