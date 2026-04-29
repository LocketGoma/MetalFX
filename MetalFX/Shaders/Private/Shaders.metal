/*
See the LICENSE.txt file for this sample’s licensing information.

Abstract:
This file provides the vertex and fragment shaders for the MetalFX sample.
*/

#include <metal_stdlib>
#include <simd/simd.h>

/// Include the header that this Metal shader code shares with the Swift code.
#import "ShaderTypes.h"

#define AAPL_INITIAL_TIME  5
#define AAPL_DRAW_CHECKER  0
#define AAPL_ANIM_SPEED    1
#define AAPL_DEFAULT_UPSCALE_LINEAR 1

using namespace metal;

/// An input vertex for the scene geometry.
typedef struct
{
    float3 position  [[attribute(AAPLVertexAttributePosition)]];
    float2 texcoord  [[attribute(AAPLVertexAttributeTexcoord)]];
    float3 normal    [[attribute(AAPLVertexAttributeNormal)]];
    float3 tangent   [[attribute(AAPLVertexAttributeTangent)]];
    float3 bitangent [[attribute(AAPLVertexAttributeBitangent)]];
} Vertex;

/// The input to a fragment shader produced by a vertex shader.
typedef struct
{
    float4 position [[position]];
    float2 texcoord;
    float4 prevPosition;
    float3 modelPosition;
    half3 normal;
    half3 tangent;
    half3 bitangent;
    half3 viewDir;
    half3 lightDir;
} FragmentIn;

/// The output of a fragment shader that generates motion vectors.
typedef struct
{
    half4 color        [[color(0)]];
    half2 motionVector [[color(1)]];
} TAAFragmentOut;

constant constexpr half3 LightDirection = half3(10,10,10);
constant constexpr half3 LightIntensity = half3(500);

#pragma mark - Normal rendering shaders.

/// Applies world, camera, and projection transforms to prepare vertices for rasterization.
vertex FragmentIn vertexShader(Vertex in [[stage_in]],
                               constant FrameData & frameData     [[ buffer(AAPLBufferIndexFrameData) ]],
                               constant FrameData & prevFrameData [[ buffer(AAPLBufferIndexPrevFrameData) ]])
{
    FragmentIn out;
    
    float4 position = float4(in.position, 1.0);
    out.modelPosition = in.position.xyz;
    out.position = frameData.projectionMatrix * frameData.modelViewMatrix * position;
    out.texcoord = in.texcoord;
    out.normal = (half3)(frameData.normalMatrix * in.normal).xyz;
    out.tangent = (half3)normalize(in.tangent);
    out.bitangent = (half3)normalize(in.bitangent);
    float3 worldPosition = (frameData.modelMatrix * position).xyz;
    float3 viewPosition = frameData.viewMatrix.columns[3].xyz;
    out.viewDir = (half3)normalize(-viewPosition - worldPosition);
    out.lightDir = normalize(LightDirection);

    out.prevPosition = prevFrameData.projectionMatrix * prevFrameData.modelViewMatrix * position;
    
    return out;
}

half GGX(half NdotH, half roughness)
{
    return (roughness*roughness) / (3.1415926h * pow(NdotH*NdotH * (roughness-1) + 1, 2));
}

half Smith(half NdotV, half roughness4)
{
    return 2 * NdotV / (NdotV + sqrt(roughness4 + (1-roughness4) * NdotV*NdotV));
}

/// Computes an animated texture for the sample geometry using two noisy textures.
///
/// The animation provides extra time and pixel-to-pixel variation to help show how image quality improves using temporal antialiasing.
half4 traditionalSurfaceTexture(FragmentIn in,
                                constant FrameData& frameData,
                                texture2d<half> albedoTexture,
                                texture2d<half> normalTexture,
                                texture2d<half> roughnessTexture)
{
    constexpr sampler linearSampler(mip_filter::linear,
                                    mag_filter::linear,
                                    min_filter::linear,
                                    s_address::clamp_to_edge,
                                    t_address::clamp_to_edge,
                                    max_anisotropy(16));
    
    // Add normal from normal map.
    half3 N = normalize(in.normal);
    half3x3 Nmat(in.tangent, in.bitangent, N);
    half3 normalMap = normalTexture.sample(linearSampler, in.texcoord, bias(frameData.mipmapBias)).rgb * 2 - 1;
    N = normalize(Nmat * normalMap);
    
    // Compute the lighting values.
    half3 V = normalize(in.viewDir);
    half3 L = normalize(in.lightDir);
    half3 H = normalize(L+V);
    half NdotL = max(0.001h, dot(N, L));
    half NdotV = max(0.001h, abs(dot(N, V)));
    half NdotH = max(0.001h, dot(N, H));
    half VdotH = max(0.001h, dot(V, H));
    
    // The Fresnel amount of the specular reflection is F(V, H).
    constexpr half F0 = 0.04;
    half F = F0 + (1.0h - F0) * pow(1.0h - VdotH, 5.0h);
    // The light transmitted for the diffuse reflection is 1 - F(V, H).
    half T = 1.0f - F;

    // Calculate the specular reflection.
    half roughness = roughnessTexture.sample(linearSampler, in.texcoord).r;
    half roughness4 = pow(roughness, 4);
    half G = Smith(NdotL, roughness4) * Smith(NdotV, roughness4);
    half D = GGX(NdotH, roughness);
    half c_factor_num = max(0.0h, F * G * D);
    half c_factor_den = max(0.001h, 4.0h * NdotL * NdotV);
    half c_factor = c_factor_num / c_factor_den;
    half3 c_specular = saturate(c_factor) * NdotL * LightIntensity;

#if 1
    // Calculate the diffuse reflection based on the surface roughness.
    half faceNdotL = max(0.001h, dot(in.normal, L));
    half FD90 = 0.5 + 2 * roughness * VdotH*VdotH;
    half Fl = (1 + (FD90 - 1) * pow(1-faceNdotL, 5));
    half Fv = (1 + (FD90 - 1) * pow(1-NdotV, 5));
    half Fdiffuse = Fl * Fv;
#else
    // Use a simpler Lambertian diffuse surface reflection.
    constexpr half Fdiffuse = 1;
#endif
    half3 albedoMap = albedoTexture.sample(linearSampler, in.texcoord, bias(frameData.mipmapBias)).rgb;
    half k_diffuse = T * Fdiffuse / 3.1415926;
    half3 c_diffuse = albedoMap * k_diffuse * saturate(0.05 + NdotL) * LightIntensity;

    // Add diffuse light to the specular light and tone map with an exposure adjustment and Reinhard filmic adjustment.
    half3 color = (c_diffuse + c_specular);
    color *= pow(2.0h, -6.0h);
    color /= (color + 1);

    return half4(color, 1.0);
}

/// Computes an animated texture for the sample geometry using two noisy textures.
///
/// The animation provides extra time and pixel-to-pixel variation to help show how image quality improves using temporal antialiasing.
half4 proceduralSurfaceTexture(FragmentIn in,
                               constant FrameData& frameData,
                               texture2d<half> diffClouds1,
                               texture2d<half> diffClouds2)
{
    constexpr sampler colorSampler(mip_filter::linear,
                                   mag_filter::linear,
                                   min_filter::linear,
                                   s_address::repeat,
                                   t_address::repeat);
    
    // Get the time offset for the texture animation.
    float t = frameData.timeInSeconds * AAPL_ANIM_SPEED + AAPL_INITIAL_TIME;
    
    // Compute a normal from the model position derivative to calculate a diffuse light value.
    half3 N = (half3)normalize(in.normal);
    half3 L = normalize(half3(1, 1, 1));
    half NdotL = max(0.2h, dot(N, L));
    
#if AAPL_DRAW_CHECKER
    // Calculate the checker pattern and use an offset to create a positive XYZ value.
    float3 q = floor(100 + in.modelPosition.xyz * 0.98);
    float ckr = step(fmod(q.x + q.y + q.z, 2.0), 0.5);
#else
    float ckr = 1.0;
#endif
    
    // Set the move offset to add an oscillating pattern to the texture coordinates.
    float2 moveOffset = float2(sin(t*0.42), cos(t*0.45));

    half3 color1 = diffClouds1.sample(colorSampler, in.texcoord + moveOffset.xy, bias(frameData.mipmapBias)).rgb;
    half3 color2 = diffClouds2.sample(colorSampler, in.texcoord + moveOffset.yx, bias(frameData.mipmapBias)).rgb;

    // Create a banding effect from adding or subtracting the two textures.
    half3 color = abs(color1 - color2);
    color.x = abs(sin(color.x + t));
    color.z = abs(cos(color.y + t));
    color.y = abs(color.x - color.z);
    constexpr float numBands = 2.0;
    color = saturate(fmod(color * numBands, 1.0));
    
    // Band the resulting color as either yellow, magenta, or cyan.
    float avg = (color.x+color.y+color.z)/3;
    if (avg < 0.33)
        color.xyz = half3(color.xy, 0);
    else if (avg < 0.67)
        color.xyz = half3(color.x, 0, color.z);
    else
        color.xyz = half3(0, color.yz);
    
    // Offset the color hue by checker color.
    color = mix(color.xyz, color.yzx, ckr);
    
    // Modulate the diffuse lighting with the color.
    color = saturate(color * NdotL);
    
    return half4(color, 1.0);
}

/// Generates the final color from the animated surface texture.
fragment half4 fragmentShader(FragmentIn in [[stage_in]],
                              constant FrameData& frameData [[ buffer(AAPLBufferIndexFrameData) ]],
                              texture2d<half> colorMap1     [[ texture(AAPLTextureIndexColor1) ]],
                              texture2d<half> colorMap2     [[ texture(AAPLTextureIndexColor2) ]],
                              texture2d<half> colorMap3     [[ texture(AAPLTextureIndexColor3) ]])
{
    if (frameData.proceduralTextureEnabled) {
        return proceduralSurfaceTexture(in, frameData, colorMap1, colorMap2);;
    } else {
        return traditionalSurfaceTexture(in, frameData, colorMap1, colorMap2, colorMap3);
    }
}

/// Generates the motion vectors for the temporal antialiasing effect.
fragment TAAFragmentOut fragmentShaderTAA(FragmentIn in [[stage_in]],
                                          constant FrameData& frameData     [[ buffer(AAPLBufferIndexFrameData) ]],
                                          constant FrameData& prevFrameData [[ buffer(AAPLBufferIndexPrevFrameData) ]],
                                          texture2d<half> colorMap1         [[ texture(AAPLTextureIndexColor1) ]],
                                          texture2d<half> colorMap2         [[ texture(AAPLTextureIndexColor2) ]],
                                          texture2d<half> colorMap3         [[ texture(AAPLTextureIndexColor3) ]])
{
    TAAFragmentOut out;
    if (frameData.proceduralTextureEnabled) {
        out.color = proceduralSurfaceTexture(in, frameData, colorMap1, colorMap2);;
    } else {
        out.color = traditionalSurfaceTexture(in, frameData, colorMap1, colorMap2, colorMap3);
    }

    // Compute the motion vectors.
    float2 motionVector = 0.0f;
    if (frameData.motionVectorFrameIndex > 0) {
        constexpr float2 scale{0.5f, -0.5f};
        constexpr float offset{0.5f};
        
        // Map the current pixel location to 0..1.
        float2 uv = in.position.xy / frameData.renderResolution;
        
        // Unproject the position from the previous frame and transform it from NDC space to 0..1.
        float2 prevUV = in.prevPosition.xy / in.prevPosition.w * scale + offset;
        
        // Remove the jittering that the projection matrix applies from both sets of coordinates.
        uv -= frameData.texelJitter;
        prevUV -= prevFrameData.texelJitter;
        
        // The motion vector is simply the difference between the two.
        motionVector = prevUV - uv;
    }
    out.motionVector = (half2)motionVector;
    
    return out;
}

#pragma mark - Copy to view shaders.

/// An input vertex for a full-screen quad with both position and texture coordinates.
struct AAPLVertexV4T2
{
    float4 position [[position]];
    float2 texcoord;
};

/// Outputs a triangle that covers the full screen.
///
/// The `vid` input should be in the range [0, 2].
vertex AAPLVertexV4T2 FSQ_VS_V4T2(uint vid [[vertex_id]])
{
    // These vertices map a triangle to cover a full-screen quad.
    const float2 vertices[] = {
        float2(-1, -1), // bottom left
        float2(3, -1),  // bottom right
        float2(-1, 3),  // upper left
    };
    
    const float2 texcoords[] = {
        float2(0.0, 1.0),  // bottom left
        float2(2.0, 1.0),  // bottom right
        float2(0.0, -1.0), // upper left
    };
    
    AAPLVertexV4T2 out;
    out.position = float4(vertices[vid], 1.0, 1.0);
    out.texcoord = texcoords[vid];
    return out;
}

/// Copies the input texture to the output.
fragment half4 FSQ_simpleCopy(AAPLVertexV4T2 in [[stage_in]],
                              constant FrameData& frame [[buffer(0)]],
                              texture2d<half> src [[texture(0)]])
{
#if AAPL_DEFAULT_UPSCALE_LINEAR
    constexpr sampler sampler(min_filter::linear, mag_filter::linear);
#else
    constexpr sampler sampler(min_filter::nearest, mag_filter::nearest);
#endif
    
    // Calculate the parameters for the little zoom view.
    uint2 curPixel = uint2(floor(in.texcoord * frame.windowResolution));
    uint2 zoomMin = uint2(floor(frame.windowResolution/2));
    uint2 zoomSize = uint2(128, 128);
    uint2 viewMin = uint2(32, 32);
    uint2 viewSize = zoomSize * 4;
    uint2 viewMax = viewMin + viewSize;
    
    half4 sample;

    // Check whether this pixel is inside the zoom rectangle view.
    if (curPixel.x >= viewMin.x && curPixel.x < viewMax.x && curPixel.y >= viewMin.y && curPixel.y < viewMax.y)
    {
        float2 offset = float2(zoomMin) / frame.windowResolution;
        float2 texcoord = 0.25 * float2(curPixel - viewMin) / frame.windowResolution;
        sample = half4(src.sample(sampler, offset + texcoord));
    }
    // Otherwise, copy the pixel normally.
    else
    {
        sample = src.sample(sampler, in.texcoord);
    }
    return sample;
}
