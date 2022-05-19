
cbuffer RootConstants : register(b0)
{
    uint Width;
    uint Height;
};

Texture2D<float4>   SrcMip  : register(t0);
RWTexture2D<float4> DestMip : register(u0);
sampler             Sampler : register(s0);

float4 LinearToGamma(float4 color)
{
    return float4(pow(color.xyz, 1.0f / 2.2f), color.w);
}

// TODO: Optional SRGB

[numthreads(8, 8, 1)]
void MipGenerationPass(uint3 pos : SV_DispatchThreadID)
{
    if (pos.x < Width && pos.y < Height)
    {
        DestMip[pos.xy] = LinearToGamma(SrcMip.SampleLevel(Sampler, (pos.xy + 0.5f) / float2(Width, Height), 0));
    }
}
