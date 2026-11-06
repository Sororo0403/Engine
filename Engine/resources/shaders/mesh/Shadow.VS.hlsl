cbuffer Transformation : register(b0) {
	row_major float4x4 worldViewProjection;
	row_major float4x4 worldInverseTranspose;
	row_major float4x4 world;
};

cbuffer ShadowBuffer : register(b5) {
	row_major float4x4 shadowViewProjection;
};

float4 VSMain(float3 position : POSITION0) : SV_Position {
	return mul(mul(float4(position, 1.0f), world), shadowViewProjection);
}
