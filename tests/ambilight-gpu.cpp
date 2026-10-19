// Run from a VS x64 Native Tools prompt:
// cl /nologo /EHsc /std:c++20 tests\ambilight-gpu.cpp /Fe:_bin\ambilight-gpu.exe
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../Source/Ambilight.h"
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
using Microsoft::WRL::ComPtr;
static void check(HRESULT hr) { if (FAILED(hr)) { printf("HRESULT %08lx\n", hr); exit(1); } }
static void require(bool ok, const char* message) { if (!ok) { printf("FAIL: %s\n", message); exit(1); } }
int main() {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context));
    auto compile = [](const char* source, const char* profile) {
        ComPtr<ID3DBlob> code, error;
        HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr, "main", profile,
            D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &error);
        if (error) printf("%s", (const char*)error->GetBufferPointer());
        check(hr);
        return code;
    };
    auto vertexCode = compile(
        "struct V { float4 p:SV_POSITION; float2 uv:TEXCOORD; };"
        "V main(uint id:SV_VertexID) { V v; v.uv=float2((id<<1)&2,id&2);"
        "v.p=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v; }", "vs_4_0");
    ComPtr<ID3D11VertexShader> vs;
    check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &vs));
    auto makeShader = [&](const char* source) {
        auto code = compile(source, "ps_4_0");
        ComPtr<ID3D11PixelShader> shader;
        check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
        return shader;
    };
    auto blur = makeShader(AmbilightBlurShader);
    auto downsample = makeShader(AmbilightDownsampleShader);
    auto composite = makeShader(AmbilightCompositeShader);
    constexpr UINT width = 64, height = 64;
    std::vector<unsigned> red(width * height, 0xff0000ff);
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    D3D11_SUBRESOURCE_DATA initial = {red.data(), width * 4, 0};
    ComPtr<ID3D11Texture2D> source, target, staging;
    check(device->CreateTexture2D(&desc, &initial, &source));
    check(device->CreateTexture2D(&desc, nullptr, &target));
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(device->CreateTexture2D(&desc, nullptr, &staging));
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;
    check(device->CreateShaderResourceView(source.Get(), nullptr, &srv));
    check(device->CreateRenderTargetView(target.Get(), nullptr, &rtv));
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;
    check(device->CreateSamplerState(&sd, &sampler));
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = 64; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> constants;
    check(device->CreateBuffer(&bd, nullptr, &constants));
    D3D11_VIEWPORT viewport = {0, 0, (float)width, (float)height, 0, 1};
    context->RSSetViewports(1, &viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 1, srv.GetAddressOf());
    context->PSSetSamplers(0, 1, sampler.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, constants.GetAddressOf());
    const float blue[4] = {0, 0, 1, 1};
    auto run = [&](ID3D11PixelShader* shader, const float* values) {
        context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
        context->ClearRenderTargetView(rtv.Get(), blue);
        context->UpdateSubresource(constants.Get(), 0, nullptr, values, 0, 0);
        context->PSSetShader(shader, nullptr, 0);
        context->Draw(3, 0);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped;
        check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        std::vector<unsigned> pixels(width * height);
        for (UINT y = 0; y < height; ++y) memcpy(&pixels[y * width], (char*)mapped.pData + y * mapped.RowPitch, width * 4);
        context->Unmap(staging.Get(), 0);
        return pixels;
    };
    float values[16] = {0.35f, 100, 0.15f, 1, 0, 16, 64, 48, 1, 1, 1, 1, 0.12f, 15, 0, 0};
    auto pixels = run(composite.Get(), values);
    for (UINT y = 16; y < 48; ++y) for (UINT x = 0; x < width; ++x)
        require(pixels[y * width + x] == 0xffff0000, "video pixels changed");
    require((pixels[8 * width + 32] & 255) > 0, "top bar is black");
    require((pixels[55 * width + 32] & 255) > 0, "bottom bar is black");
    values[1] = 2;
    auto narrow = run(composite.Get(), values);
    require((narrow[8 * width + 32] & 255) < (pixels[8 * width + 32] & 255), "spread has no spatial effect");
    values[1] = 100;
    values[13] = 14;
    pixels = run(composite.Get(), values);
    require(pixels[8 * width + 32] == 0xffff0000, "disabled top direction was overwritten");
    values[13] = 15;
    values[10] = 0;
    pixels = run(composite.Get(), values);
    unsigned gray = pixels[8 * width + 32];
    require((gray & 255) == ((gray >> 8) & 255) && (gray & 255) == ((gray >> 16) & 255), "zero saturation is not gray");
    values[10] = 1;
    values[0] = 0;
    pixels = run(composite.Get(), values);
    require((pixels[8 * width + 32] & 0xffffff) == 0, "zero intensity does not produce black");
    values[0] = 1; values[4] = 16; values[5] = 0; values[6] = 48; values[7] = 64;
    pixels = run(composite.Get(), values);
    require((pixels[32 * width + 8] & 255) > 0 && (pixels[32 * width + 55] & 255) > 0, "pillarbox bars are black");
    require(pixels[32 * width + 32] == 0xffff0000, "pillarbox video changed");
    values[0] = 1.0f / width; values[1] = 0;
    pixels = run(blur.Get(), values);
    require(pixels[32 * width + 32] == 0xff0000ff, "blur does not preserve a uniform color");
    puts("PASS: HLSL compilation, sharp video preservation, letterbox, pillarbox, spread, directions, saturation, intensity zero, blur uniform color (D3D11 WARP)");
}
