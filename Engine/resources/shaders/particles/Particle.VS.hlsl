cbuffer Camera : register(b0) {
	row_major float4x4 viewProjection;
	float3 cameraRight;
	float cameraPadding0;
	float3 cameraUp;
	float cameraPadding1;
};

struct Input {
	float3 position : POSITION0;
	float size : SIZE0;
	float4 color : COLOR0;
	float rotation : ROTATION0;
	uint circle : CIRCLE0;
};

struct Output {
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
	nointerpolation uint circle : CIRCLE0;
};

Output main(Input input, uint vertex : SV_VertexID) {
	const float2 corners[6] = {
		float2(0, 0), float2(1, 0), float2(0, 1),
		float2(0, 1), float2(1, 0), float2(1, 1)
	};
	float2 uv = corners[vertex];
	float2 offset = (uv - 0.5f) * float2(1, -1) * input.size;
	float sine, cosine;
	sincos(input.rotation, sine, cosine);
	offset = float2(offset.x * cosine - offset.y * sine,
	                offset.x * sine + offset.y * cosine);
	float3 position = input.position + cameraRight * offset.x + cameraUp * offset.y;
	Output output;
	output.position = mul(float4(position, 1), viewProjection);
	output.uv = uv;
	output.color = input.color;
	output.circle = input.circle;
	return output;
}
