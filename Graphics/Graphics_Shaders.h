// ============================================================================
// Conquer Kayank Engine
// ============================================================================
#pragma once

namespace Graphics {

    const char* vertexShader2DCode = R"(
        cbuffer ConstantBuffer : register(b0) { 
            matrix WVP; 
            matrix Bones[128];
            int HasAnimation;
            float3 padding;
        }
        struct VOut { float4 position : SV_POSITION; float2 tex : TEXCOORD; };
        VOut main(float3 pos : POSITION, float2 tex : TEXCOORD) {
            VOut output;
            output.position = mul(float4(pos, 1.0f), WVP);
            output.tex = tex;
            return output;
        }
    )";

    const char* pixelShader2DCode = R"(
        Texture2D shaderTexture : register(t0);
        SamplerState sampleType : register(s0);
        struct VOut { float4 position : SV_POSITION; float2 tex : TEXCOORD; };
        float4 main(VOut input) : SV_TARGET {
            float4 color = shaderTexture.Sample(sampleType, input.tex);
            clip(color.a - 0.1f); 
            return color;
        }
    )";

    const char* vertexShader3DCode = R"(
        cbuffer ConstantBuffer : register(b0) { 
            matrix WVP; 
            matrix Bones[128];
            int HasAnimation;
            float Alpha;
            float2 padding;
        }
        struct VOut { float4 position : SV_POSITION; float2 tex : TEXCOORD; float alpha : COLOR; };
        
        VOut main(float3 pos : POSITION, float2 tex : TEXCOORD, uint2 boneIdx : BLENDINDICES, float2 boneWt : BLENDWEIGHT) {
            VOut output;
            
            if (HasAnimation == 1) {
                if (boneWt.x > 0.0f) {
                    output.position = mul(float4(pos, 1.0f), Bones[boneIdx.x]);
                } else if (boneWt.y > 0.0f) {
                    output.position = mul(float4(pos, 1.0f), Bones[boneIdx.y]);
                } else {
                    output.position = mul(float4(pos, 1.0f), WVP);
                }
            } else {
                output.position = mul(float4(pos, 1.0f), WVP);
            }
            
            output.tex = tex;
            output.alpha = Alpha;
            return output;
        }
    )";

    const char* pixelShader3DCode = R"(
        Texture2D shaderTexture : register(t0);
        SamplerState sampleType : register(s0);
        struct VOut { float4 position : SV_POSITION; float2 tex : TEXCOORD; };
        float4 main(VOut input) : SV_TARGET {
            float4 color = shaderTexture.Sample(sampleType, input.tex);
            clip(color.a - 0.1f); 
            return color; 
        }
    )";

    const char* vertexShaderPtclCode = R"(
        cbuffer ConstantBuffer : register(b0) { matrix WVP; }
        struct VOut { float4 position : SV_POSITION; float4 color : COLOR; float2 tex : TEXCOORD; };
        
        VOut main(float3 pos : POSITION, float4 color : COLOR, float2 tex : TEXCOORD) {
            VOut output;
            output.position = mul(float4(pos, 1.0f), WVP);
            output.color = color;
            output.tex = tex;
            return output;
        }
    )";

    const char* pixelShaderPtclCode = R"(
        Texture2D shaderTexture : register(t0);
        SamplerState sampleType : register(s0);
        struct VOut { float4 position : SV_POSITION; float4 color : COLOR; float2 tex : TEXCOORD; };
        
        float4 main(VOut input) : SV_TARGET {
            float4 texColor = shaderTexture.Sample(sampleType, input.tex);
            // Multiplica a cor da textura pelo Alpha (idade) da particula!
            return texColor * input.color; 
        }
    )";
}