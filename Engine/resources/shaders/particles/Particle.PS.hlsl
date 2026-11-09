Texture2D<float4> image : register(t0);
SamplerState imageSampler : register(s0);

struct Input {
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
	nointerpolation uint circle : CIRCLE0;
};

float4 main(Input input) : SV_Target0 {
	float4 color = image.Sample(imageSampler, input.uv) * input.color;

	if (input.circle != 0) {
		color.a *= 1.0f - smoothstep(0.2f, 0.5f, length(input.uv - 0.5f));
	}

	return color;
}
