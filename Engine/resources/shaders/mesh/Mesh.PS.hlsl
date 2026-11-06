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

struct SpotLight {
	float3 position;
	float range;
	float3 direction;
	float cosInner;
	float3 color;
	float intensity;
	float cosOuter;
	float3 padding;
};

cbuffer SpotLightBuffer : register(b4) {
	uint spotLightCount;
	uint3 spotLightPadding;
	SpotLight spotLights[32];
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
		for (uint j = 0; j < spotLightCount; ++j) {
			float3 offset = spotLights[j].position - input.worldPosition;
			float distance = length(offset);
			if (distance > 0.0f && distance < spotLights[j].range) {
				float3 toSpot = offset / distance;
				float cosine = dot(-toSpot, spotLights[j].direction);
				float cone = 0.0f;
				// 内外角が等しい場合は硬い境界とし、角度差で零除算しない。
				if (cosine >= spotLights[j].cosInner) {
					cone = 1.0f;
				} else if (cosine > spotLights[j].cosOuter) {
					float t = saturate((cosine - spotLights[j].cosOuter) /
					                   (spotLights[j].cosInner - spotLights[j].cosOuter));
					cone = t * t * (3.0f - 2.0f * t);
				}
				float attenuation = 1.0f - distance / spotLights[j].range;
				float spotDiffuse = saturate(dot(normal, toSpot));
				lighting += spotLights[j].color * spotLights[j].intensity *
				            spotDiffuse * cone * attenuation * attenuation;
			}
		}
	}

	float3 color =
	    textureColor.rgb * input.color.rgb * baseColor.rgb * lighting;

	float alpha = textureColor.a * input.color.a * baseColor.a;

	return float4(color, alpha);
}
