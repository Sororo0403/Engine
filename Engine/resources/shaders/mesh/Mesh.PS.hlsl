#include "Mesh.hlsli"

Texture2D<float4> texture0 : register(t0);

SamplerState sampler0 : register(s0);

Texture2D<float> shadowMap : register(t1);
SamplerComparisonState shadowSampler : register(s1);

cbuffer ShadowBuffer : register(b5) {
	row_major float4x4 shadowViewProjection;
	float shadowStrength;
	float shadowBias;
	float shadowNormalBias;
	uint shadowEnabled;
	float shadowTexelSize;
	float3 shadowPadding;
};

float ShadowVisibility(float3 position, float3 normal) {
	if (shadowEnabled == 0) {
		return 1.0f;
	}

	float4 projected = mul(float4(position + normal * shadowNormalBias, 1.0f),
	                       shadowViewProjection);
	float3 p = projected.xyz / projected.w;
	float2 uv = p.xy * float2(0.5f, -0.5f) + 0.5f;
	// 比較する画素ごとに受光面の深度を求め、斜面を平均した際の自己遮蔽を抑える。
	float3 dx = ddx(float3(uv, p.z));
	float3 dy = ddy(float3(uv, p.z));
	float determinant = dx.x * dy.y - dx.y * dy.x;
	float2 gradient = float2(0.0f, 0.0f);
	if (abs(determinant) > 0.0000000001f) {
		gradient = float2(dx.z * dy.y - dy.z * dx.y,
		                  dx.x * dy.z - dy.x * dx.z) / determinant;
	}
	float filterBias = 0.5f * shadowTexelSize * (abs(gradient.x) + abs(gradient.y));

	if (p.z <= 0.0f || p.z >= 1.0f || any(abs(p.xy) >= 1.0f)) {
		return 1.0f;
	}

	float visible = 0.0f;
	[unroll]
	for (int y = -1; y <= 1; ++y) {
		[unroll]
		for (int x = -1; x <= 1; ++x) {
			float2 offset = float2(x, y) * shadowTexelSize;
			float reference = p.z + dot(gradient, offset) - shadowBias - filterBias;
			visible += shadowMap.SampleCmpLevelZero(shadowSampler, uv + offset, reference);
		}
	}

	float fade = saturate((1.0f - max(abs(p.x), abs(p.y))) * 10.0f);
	return lerp(1.0f, visible / 9.0f, shadowStrength * fade);
}

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
		lighting = lightColor * lightIntensity * diffuse *
		           ShadowVisibility(input.worldPosition, normal) + ambient;
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
