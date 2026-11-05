#include "Mesh.hlsli"

Texture2D<float4> texture0 : register(t0);
SamplerState sampler0 : register(s0);

cbuffer DirectionalLightBuffer : register(b1) {
	float3 lightDirection;
	float lightIntensity;

	float3 lightColor;
	float ambient;
};

struct PointLight {
	float3 position;
	float range;
	float3 color;
	float intensity;
};

cbuffer PointLightBuffer : register(b3) {
	uint pointLightCount;
	uint3 pointLightPadding;
	PointLight pointLights[32];
};

cbuffer MaterialBuffer : register(b2) {
	float4 baseColor;
	float2 uvScale;
	float2 uvOffset;
	uint shadingMode;
	uint3 materialPadding;
};

float4 PSMain(VertexOutput input) : SV_Target0 {
	float2 texcoord = input.texcoord * uvScale + uvOffset;

	float4 textureColor = texture0.Sample(sampler0, texcoord);

	float3 lighting = float3(1.0f, 1.0f, 1.0f);

	if (shadingMode == 0) {
		float3 normal = normalize(input.normal);
		float3 toLight = -normalize(lightDirection);
		float diffuse = saturate(dot(normal, toLight));
		lighting = lightColor * lightIntensity * diffuse + ambient;
		// 範囲端で滑らかに0へ近づけ、光源位置そのものでは零除算を避ける。
		for (uint i = 0; i < pointLightCount; ++i) {
			float3 offset = pointLights[i].position - input.worldPosition;
			float distanceSquared = dot(offset, offset);
			float distance = sqrt(distanceSquared);
			if (distance > 0.0f && distance < pointLights[i].range) {
				float attenuation = 1.0f - distance / pointLights[i].range;
				float3 direction = offset / distance;
				float pointDiffuse = saturate(dot(normal, direction));
				lighting += pointLights[i].color * pointLights[i].intensity *
				            pointDiffuse * attenuation * attenuation;
			}
		}
	}

	float3 color =
	    textureColor.rgb * input.color.rgb * baseColor.rgb * lighting;

	float alpha = textureColor.a * input.color.a * baseColor.a;

	return float4(color, alpha);
}
