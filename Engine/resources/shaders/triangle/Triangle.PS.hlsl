#include "Triangle.hlsli"

Texture2D<float4> texture0 : register(t0);
SamplerState sampler0 : register(s0);

cbuffer DirectionalLightBuffer : register(b1)
{
	float3 lightDirection;
	float lightIntensity;

	float3 lightColor;
	float ambient;
};

cbuffer MaterialBuffer : register(b2)
{
	float4 baseColor;
};

float4 PSMain(VertexOutput input) : SV_Target0
{
	float4 textureColor =
	    texture0.Sample(sampler0, input.texcoord);

	float3 normal = normalize(input.normal);
	float3 toLight = -normalize(lightDirection);
	float diffuse = saturate(dot(normal, toLight));

	float3 lighting =
	    lightColor * lightIntensity * diffuse + ambient;

	float3 color =
	    textureColor.rgb *
	    input.color.rgb *
	    baseColor.rgb *
	    lighting;

	float alpha =
	    textureColor.a * input.color.a * baseColor.a;

	return float4(color, alpha);
}
