cbuffer Element : register(b0) {
    float4 rectangle;
    float4 tint;
    float4 uvRect;
};
Texture2D image : register(t0);
SamplerState imageSampler : register(s0);
float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET0 {
    return image.Sample(imageSampler, uv) * tint;
}
