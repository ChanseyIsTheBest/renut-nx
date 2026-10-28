/**
 * @file    gpu/d3d/hooks/state.cpp
 * @brief   N&B's D3D state calls, hooked with the native ports (d3d_native.h).
 *
 * The render and sampler state setters are the ones N&B's state tables name
 * (D3D::InitializeApiState puts them into every device's dispatch table,
 * which the game's inline SetRenderState / SetSamplerState call through).
 * The rest are D3DDevice_Set* and the CDevice internals that write the ring.
 */
#include <cstring>

#include <rex/hook.h>

#include "renut_engine/gpu/banjo_renut_select.h"
#include <rex/types.h>

#include "renut_engine/gpu/d3d/d3d_native.h"
#include "renut_engine/gpu/guest/guest_memory.h"

using namespace renut::gpu;

namespace {

template <u32 State>
void SetRenderStateHook(u32 device, u32 value) {
  native::SetRenderState(native::DeviceAt(device), State, value);
}

template <u32 State>
void SetSamplerStateHook(u32 device, u32 sampler, u32 value) {
  native::SetSamplerState(native::DeviceAt(device), sampler, State, value);
}

template <u32 State>
u32 GetRenderStateHook(u32 device) {
  return native::GetRenderState(native::DeviceAt(device), State);
}

template <u32 State>
u32 GetSamplerStateHook(u32 device, u32 sampler) {
  return native::GetSamplerState(native::DeviceAt(device), sampler, State);
}

// The device's AddRef of a resource it hands out (D3DResource_AddRef,
// resource.cpp).
u32 AddRefResource(u32 resource) {
  return native::AddRefResource(resource);
}

// D3DSurface *GetRenderTarget(D3DDevice*, DWORD Index) [0x82636ED0]
u32 D3DDevice_GetRenderTarget_hook(u32 device, u32 index) {
  const native::Device d = native::DeviceAt(device);
  const u32 surface = d && index < 4 ? d.W(native::off::RenderTargets + index * 4) : 0;
  if (surface)
    AddRefResource(surface);
  return surface;
}

// D3DSurface *GetDepthStencilSurface(D3DDevice*) [0x82636F10]
u32 D3DDevice_GetDepthStencilSurface_hook(u32 device) {
  const native::Device d = native::DeviceAt(device);
  const u32 surface = d ? d.W(native::off::DepthStencil) : 0;
  if (surface)
    AddRefResource(surface);
  return surface;
}

// D3DVertexBuffer *GetStreamSource(D3DDevice*, UINT StreamNumber,
//     UINT *pOffsetInBytes, UINT *pStride) [0x82636E40]: the offset is the
// stream's fetch constant address less the buffer's.
u32 D3DDevice_GetStreamSource_hook(u32 device, u32 stream, mapped_u32 offset, mapped_u32 stride) {
  const native::Device d = native::DeviceAt(device);
  if (!d || stream >= 16)
    return 0;
  const u32 buffer = d.W(native::off::StreamSources + stream * 4);
  u32 bytes = 0;
  if (buffer) {
    const u32 fetch = d.W(native::off::TextureFetch + (95 - stream) * 8);
    bytes = fetch - native::GpuAddress(GuestPtr<const be_u32>(buffer)[6]);
    AddRefResource(buffer);
  }
  if (offset)
    *offset = bytes;
  if (stride)
    *stride = u32(d.B(native::off::StreamStrides + stream)) << 2;
  return buffer;
}

// void D3DDevice_GetViewport(D3DDevice*, D3DVIEWPORT9*) [0x82244B18]
void D3DDevice_GetViewport_hook(u32 device, mapped_u32 viewport) {
  const native::Device d = native::DeviceAt(device);
  if (!d || !viewport)
    return;
  for (u32 i = 0; i < 4; ++i)  // fctidz, the low word
    viewport[i] = u32(native::TruncateToI64(d.F(0x3170 + i * 4)));
  viewport[4] = d.W(0x3180);
  viewport[5] = d.W(0x3184);
}

// void D3DDevice_SetClipPlane(D3DDevice*, DWORD Index, const float *pPlane) [0x826370E8]
void D3DDevice_SetClipPlane_hook(u32 device, u32 index, mapped_u32 plane) {
  native::Device d = native::DeviceAt(device);
  if (!d || !plane || index >= 6)
    return;
  for (u32 i = 0; i < 4; ++i)
    d.SetW(0x2820 + index * 16 + i * 4, plane[i]);
  d.Dirty(4, u64(1) << (54 - index));
}

// void D3DDevice_SetVertexFetchConstant(D3DDevice*, UINT VertexFetchRegister,
//     D3DVertexBuffer*, UINT Offset) [0x82637138]: a vertex fetch slot of the
// 3 in a fetch constant, made a vertex fetch constant first.
void D3DDevice_SetVertexFetchConstant_hook(u32 device, u32 reg, u32 vertex_buffer, u32 offset) {
  native::Device d = native::DeviceAt(device);
  const auto *vb = vertex_buffer ? GuestPtr<const be_u32>(vertex_buffer) : nullptr;
  if (!d || !vb || reg > 0x5F)
    return;
  const u32 slot = (0x5F - reg) / 3;
  const u32 part = (0x5F - reg) - slot * 3;
  const u32 constant = native::off::TextureFetch + slot * 24;
  const u32 type = d.W(constant) & 3;
  if (type == 0 || type == 2)
    std::memset(d.bytes + constant, 0, 24);
  d.TouchRange(constant, 24);  // banjo-nx (2026-10-26, banjo_renut_tags)
  d.SetW(constant, native::Insert(d.W(constant), 1, 0, 2));
  d.Dirty(3, u64(1) << (31 - slot));
  d.SetW(constant + part * 8, native::GpuAddress(u32(vb[6]) + offset));
  d.SetW(constant + part * 8 + 4, native::Insert(u32(vb[7]) - offset, 1, 28, 2));
}

// The boolean and integer shader constants (D3DDevice_Set{Vertex,Pixel}
// ShaderConstant{B,I}): bits from 0x2780 (vertex) / 0x2790 (pixel), a word
// of x, y, z bytes each from 0x27A0 / 0x27E0.
void SetShaderConstantsB(u32 device, u32 base, u32 start, mapped_u32 data, u32 count) {
  native::Device d = native::DeviceAt(device);
  if (!d || !data)
    return;
  for (u32 i = 0; i < count; ++i, ++start) {
    const u32 word = base + (start >> 5) * 4;
    const u32 bit = 1u << (start & 31);
    d.SetW(word, (d.W(word) & ~bit) | ((u32(data[i]) & 1) << (start & 31)));
  }
  d.Dirty(4, u64(1) << 56);
}

void SetShaderConstantsI(u32 device, u32 base, u32 start, mapped_u32 data, u32 count) {
  native::Device d = native::DeviceAt(device);
  if (!d || !data)
    return;
  for (u32 i = 0; i < count; ++i) {
    const u32 x = u32(data[i * 4]) & 0xFF, y = u32(data[i * 4 + 1]) & 0xFF;
    const u32 z = u32(data[i * 4 + 2]) & 0xFF;
    d.SetW(base + (start + i) * 4, (z << 16) | (y << 8) | x);
  }
  d.Dirty(4, u64(1) << 56);
}

// void D3DDevice_SetVertexShaderConstantB(D3DDevice*, UINT StartRegister,
//     const BOOL*, UINT BoolCount) [0x821F0B40], and the others alike.
void D3DDevice_SetVertexShaderConstantB_hook(u32 device, u32 start, mapped_u32 data, u32 count) {
  SetShaderConstantsB(device, 0x2780, start, data, count);
}
void D3DDevice_SetPixelShaderConstantB_hook(u32 device, u32 start, mapped_u32 data, u32 count) {
  SetShaderConstantsB(device, 0x2790, start, data, count);
}
void D3DDevice_SetVertexShaderConstantI_hook(u32 device, u32 start, mapped_u32 data, u32 count) {
  SetShaderConstantsI(device, 0x27A0, start, data, count);
}
void D3DDevice_SetPixelShaderConstantI_hook(u32 device, u32 start, mapped_u32 data, u32 count) {
  SetShaderConstantsI(device, 0x27E0, start, data, count);
}

// void D3DDevice_EndIndexedVertices(D3DDevice*) [0x8263ED90]: the inline end
// of BeginIndexedVertices, back to the ring pointer it saved.
void D3DDevice_EndIndexedVertices_hook(u32 device) {
  native::Device d = native::DeviceAt(device);
  if (d)
    d.SetW(native::off::Ring, d.W(0x3484));
}

// void D3DDevice_SetTexture(D3DDevice*, DWORD Sampler, D3DBaseTexture*,
//     UINT64 PendingMask3) [0x821FE558]; the mask is the sampler's fetch
// constant bit, which the port derives from Sampler.
void D3DDevice_SetTexture_hook(u32 device, u32 sampler, u32 texture) {
  native::SetTexture(native::DeviceAt(device), sampler, texture);
}

// void D3DDevice_SetStreamSource(D3DDevice*, UINT StreamNumber, D3DVertexBuffer*,
//     UINT OffsetInBytes, UINT Stride, UINT64 PendingMask3) [0x821F9438]
void D3DDevice_SetStreamSource_hook(u32 device, u32 stream, u32 vertex_buffer, u32 offset,
                                    u32 stride) {
  native::SetStreamSource(native::DeviceAt(device), stream, vertex_buffer, offset, stride);
}

// void D3DDevice_SetIndices(D3DDevice*, D3DIndexBuffer*) [0x821DE438]
void D3DDevice_SetIndices_hook(u32 device, u32 index_buffer) {
  native::SetIndices(native::DeviceAt(device), index_buffer);
}

// void D3DDevice_SetVertexDeclaration(D3DDevice*, D3DVertexDeclaration*) [0x821D5060]
void D3DDevice_SetVertexDeclaration_hook(u32 device, u32 declaration) {
  native::SetVertexDeclaration(native::DeviceAt(device), declaration);
}

// void D3DDevice_SetViewport(D3DDevice*, const D3DVIEWPORT9*) [0x821F2090]
void D3DDevice_SetViewport_hook(u32 device, u32 viewport) {
  native::SetViewport(native::DeviceAt(device), viewport);
}

// void D3D::SetViewport(D3DDevice*, float X, float Y, float Width, float Height,
//     float MinZ, float MaxZ) [0x821F2110]
void D3D_SetViewport_hook(u32 device, float x, float y, float width, float height, float min_z,
                          float max_z) {
  native::SetViewport(native::DeviceAt(device), x, y, width, height, min_z, max_z);
}

// void D3DDevice_SetScissorRect(D3DDevice*, const RECT*) [0x821F2368]
void D3DDevice_SetScissorRect_hook(u32 device, mapped_u32 rect) {
  if (!rect)
    return;
  native::SetScissorRect(native::DeviceAt(device), i32(u32(rect[0])), i32(u32(rect[1])),
                         i32(u32(rect[2])), i32(u32(rect[3])));
}

// void D3D::SetSurfaceClip(CDevice*, int, int, int, int) [0x821F2460]: the
// window scissor and per-tile clip packets, nothing else.
void D3D_SetSurfaceClip_hook(u32 /*device*/, u32, u32, u32, u32) {}

// void D3DDevice_SetRenderTarget(D3DDevice*, DWORD Index, D3DSurface*) [0x82248BD0]
void D3DDevice_SetRenderTarget_hook(u32 device, u32 index, u32 surface) {
  native::SetRenderTarget(native::DeviceAt(device), index, surface);
}

// void D3DDevice_SetDepthStencilSurface(D3DDevice*, D3DSurface*) [0x82248FB0]
void D3DDevice_SetDepthStencilSurface_hook(u32 device, u32 surface) {
  native::SetDepthStencilSurface(native::DeviceAt(device), surface);
}

// void D3D::SetSurfaceInfo(CDevice*, D3DSurface*) [0x82248F20]
void D3D_SetSurfaceInfo_hook(u32 device, u32 surface) {
  native::SetSurfaceInfo(native::DeviceAt(device), surface);
}

// void D3DDevice_SetPredication(D3DDevice*, DWORD PredicationMask) [0x821F9A48]
void D3DDevice_SetPredication_hook(u32 device, u32 mask) {
  native::SetPredication(native::DeviceAt(device), mask);
}

// void D3DDevice_SetCommandBufferPredication(D3DDevice*, DWORD TilePredication,
//     DWORD RunPredication) [0x826411C0]
void D3DDevice_SetCommandBufferPredication_hook(u32 device, u32 tile_predication,
                                                u32 run_predication) {
  native::SetCommandBufferPredication(native::DeviceAt(device), tile_predication,
                                      run_predication);
}

// void D3DDevice_SetShaderGPRAllocation(D3DDevice*, DWORD Flags,
//     DWORD VertexShaderCount, DWORD PixelShaderCount) [0x821EFD38]
void D3DDevice_SetShaderGPRAllocation_hook(u32 device, u32 flags, u32 vertex_count,
                                           u32 pixel_count) {
  native::SetShaderGPRAllocation(native::DeviceAt(device), flags, vertex_count, pixel_count);
}

// void D3D::CleanupGprAllocationSetup(CDevice*) [0x82637208]
void D3D_CleanupGprAllocationSetup_hook(u32 device) {
  native::CleanupGprAllocationSetup(native::DeviceAt(device));
}

// void D3DDevice_SetScreenExtentQueryMode(D3DDevice*, D3DSCREENEXTENTQUERYMODE)
// [0x826414C0]
void D3DDevice_SetScreenExtentQueryMode_hook(u32 device, u32 mode) {
  native::SetScreenExtentQueryMode(native::DeviceAt(device), mode);
}

// void D3DDevice_UnsetAll(D3DDevice*) [0x82634C10]
void D3DDevice_UnsetAll_hook(u32 device) {
  native::UnsetAll(native::DeviceAt(device));
}

// void D3D::ResetAllState(CDevice*, BOOL) [0x82255C98]
void D3D_ResetAllState_hook(u32 device, u32 /*skip_packets*/) {
  native::ResetAllState(native::DeviceAt(device));
}

// void D3D::InvalidateConstants(D3DDevice*) [0x82634B90]
void D3D_InvalidateConstants_hook(u32 device) {
  native::InvalidateConstants(native::DeviceAt(device));
}

// void D3DDevice_BlockUntilIdle(D3DDevice*) [0x82640010] (IDA: the second
// D3D::InvalidateConstants): waits for the device's current fence (N&B calls
// it before releasing a device).
void D3DDevice_BlockUntilIdle_hook(u32 device) {
  native::InvalidateConstantsAndWait(native::DeviceAt(device));
}

} // namespace

// Render states, by their index in N&B's table (off_82E506F0).
BANJO_RENUT_HOOK(sub_82635740, SetRenderStateHook<10>);  // D3DDevice_SetRenderState_ZEnable
BANJO_RENUT_HOOK(sub_821CB040, SetRenderStateHook<11>);  // D3DDevice_SetRenderState_ZFunc
BANJO_RENUT_HOOK(sub_82635780, SetRenderStateHook<12>);  // D3DDevice_SetRenderState_ZWriteEnable
BANJO_RENUT_HOOK(sub_8225D148, SetRenderStateHook<13>);  // D3DDevice_SetRenderState_FillMode
BANJO_RENUT_HOOK(sub_8225D168, SetRenderStateHook<14>);  // D3DDevice_SetRenderState_CullMode
BANJO_RENUT_HOOK(sub_821F9748, SetRenderStateHook<15>);  // D3DDevice_SetRenderState_AlphaBlendEnable
BANJO_RENUT_HOOK(sub_821F96C0, SetRenderStateHook<16>);  // D3DDevice_SetRenderState_SeparateAlphaBlendEnable
BANJO_RENUT_HOOK(sub_8223D118, SetRenderStateHook<17>);  // D3DDevice_SetRenderState_BlendFactor
BANJO_RENUT_HOOK(sub_821FF1D0, SetRenderStateHook<18>);  // D3DDevice_SetRenderState_SrcBlend
BANJO_RENUT_HOOK(sub_821FF8D8, SetRenderStateHook<19>);  // D3DDevice_SetRenderState_DestBlend
BANJO_RENUT_HOOK(sub_821F54F0, SetRenderStateHook<20>);  // D3DDevice_SetRenderState_BlendOp
BANJO_RENUT_HOOK(sub_82235CD8, SetRenderStateHook<21>);  // D3DDevice_SetRenderState_SrcBlendAlpha
BANJO_RENUT_HOOK(sub_821EFB00, SetRenderStateHook<22>);  // D3DDevice_SetRenderState_DestBlendAlpha
BANJO_RENUT_HOOK(sub_821EE830, SetRenderStateHook<23>);  // D3DDevice_SetRenderState_BlendOpAlpha
BANJO_RENUT_HOOK(sub_82635498, SetRenderStateHook<24>);  // D3DDevice_SetRenderState_AlphaTestEnable
BANJO_RENUT_HOOK(sub_82635550, SetRenderStateHook<25>);  // D3DDevice_SetRenderState_AlphaRef
BANJO_RENUT_HOOK(sub_826355B8, SetRenderStateHook<26>);  // D3DDevice_SetRenderState_AlphaFunc
BANJO_RENUT_HOOK(sub_826357C0, SetRenderStateHook<27>);  // D3DDevice_SetRenderState_StencilEnable
BANJO_RENUT_HOOK(sub_82635800, SetRenderStateHook<28>);  // D3DDevice_SetRenderState_TwoSidedStencilMode
BANJO_RENUT_HOOK(sub_82635868, SetRenderStateHook<29>);  // D3DDevice_SetRenderState_StencilFail
BANJO_RENUT_HOOK(sub_826358A0, SetRenderStateHook<30>);  // D3DDevice_SetRenderState_StencilZFail
BANJO_RENUT_HOOK(sub_826358D8, SetRenderStateHook<31>);  // D3DDevice_SetRenderState_StencilPass
BANJO_RENUT_HOOK(sub_82635838, SetRenderStateHook<32>);  // D3DDevice_SetRenderState_StencilFunc
BANJO_RENUT_HOOK(sub_826359D8, SetRenderStateHook<33>);  // D3DDevice_SetRenderState_StencilRef
BANJO_RENUT_HOOK(sub_826359F8, SetRenderStateHook<34>);  // D3DDevice_SetRenderState_StencilMask
BANJO_RENUT_HOOK(sub_82635A18, SetRenderStateHook<35>);  // D3DDevice_SetRenderState_StencilWriteMask
BANJO_RENUT_HOOK(sub_82635938, SetRenderStateHook<36>);  // D3DDevice_SetRenderState_CCWStencilFail
BANJO_RENUT_HOOK(sub_82635970, SetRenderStateHook<37>);  // D3DDevice_SetRenderState_CCWStencilZFail
BANJO_RENUT_HOOK(sub_826359A8, SetRenderStateHook<38>);  // D3DDevice_SetRenderState_CCWStencilPass
BANJO_RENUT_HOOK(sub_82635908, SetRenderStateHook<39>);  // D3DDevice_SetRenderState_CCWStencilFunc
BANJO_RENUT_HOOK(sub_82635A38, SetRenderStateHook<40>);  // D3DDevice_SetRenderState_CCWStencilRef
BANJO_RENUT_HOOK(sub_82635A58, SetRenderStateHook<41>);  // D3DDevice_SetRenderState_CCWStencilMask
BANJO_RENUT_HOOK(sub_82635A78, SetRenderStateHook<42>);  // D3DDevice_SetRenderState_CCWStencilWriteMask
BANJO_RENUT_HOOK(sub_8225D188, SetRenderStateHook<43>);  // D3DDevice_SetRenderState_ClipPlaneEnable
BANJO_RENUT_HOOK(sub_82635CC0, SetRenderStateHook<44>);  // D3DDevice_SetRenderState_PointSize
BANJO_RENUT_HOOK(sub_82635D20, SetRenderStateHook<45>);  // D3DDevice_SetRenderState_PointSizeMin
BANJO_RENUT_HOOK(sub_82635CB0, SetRenderStateHook<46>);  // D3DDevice_SetRenderState_PointSpriteEnable
BANJO_RENUT_HOOK(sub_822371C0, SetRenderStateHook<47>);  // D3DDevice_SetRenderState_PointSizeMax
BANJO_RENUT_HOOK(sub_82635B98, SetRenderStateHook<48>);  // D3DDevice_SetRenderState_MultiSampleAntiAlias
BANJO_RENUT_HOOK(sub_82635BC8, SetRenderStateHook<49>);  // D3DDevice_SetRenderState_MultiSampleMask
BANJO_RENUT_HOOK(sub_82635AA8, SetRenderStateHook<50>);  // D3DDevice_SetRenderState_ScissorTestEnable
BANJO_RENUT_HOOK(sub_82635AC0, SetRenderStateHook<51>);  // D3DDevice_SetRenderState_SlopeScaleDepthBias
BANJO_RENUT_HOOK(sub_821CAFA8, SetRenderStateHook<52>);  // D3DDevice_SetRenderState_DepthBias
BANJO_RENUT_HOOK(sub_821CAF70, SetRenderStateHook<53>);  // D3DDevice_SetRenderState_ColorWriteEnable
BANJO_RENUT_HOOK(sub_82635BF0, SetRenderStateHook<54>);  // D3DDevice_SetRenderState_ColorWriteEnable1
BANJO_RENUT_HOOK(sub_82635C30, SetRenderStateHook<55>);  // D3DDevice_SetRenderState_ColorWriteEnable2
BANJO_RENUT_HOOK(sub_82635C70, SetRenderStateHook<56>);  // D3DDevice_SetRenderState_ColorWriteEnable3
BANJO_RENUT_HOOK(sub_826363C8, SetRenderStateHook<57>);  // D3DDevice_SetRenderState_TessellationMode
BANJO_RENUT_HOOK(sub_82636358, SetRenderStateHook<58>);  // D3DDevice_SetRenderState_MinTessellationLevel
BANJO_RENUT_HOOK(sub_82636390, SetRenderStateHook<59>);  // D3DDevice_SetRenderState_MaxTessellationLevel
BANJO_RENUT_HOOK(sub_82635D88, SetRenderStateHook<60>);  // D3DDevice_SetRenderState_Wrap0
BANJO_RENUT_HOOK(sub_82635DA8, SetRenderStateHook<61>);  // D3DDevice_SetRenderState_Wrap1
BANJO_RENUT_HOOK(sub_82635DD0, SetRenderStateHook<62>);  // D3DDevice_SetRenderState_Wrap2
BANJO_RENUT_HOOK(sub_82635DF8, SetRenderStateHook<63>);  // D3DDevice_SetRenderState_Wrap3
BANJO_RENUT_HOOK(sub_82635E20, SetRenderStateHook<64>);  // D3DDevice_SetRenderState_Wrap4
BANJO_RENUT_HOOK(sub_82635E48, SetRenderStateHook<65>);  // D3DDevice_SetRenderState_Wrap5
BANJO_RENUT_HOOK(sub_82635E70, SetRenderStateHook<66>);  // D3DDevice_SetRenderState_Wrap6
BANJO_RENUT_HOOK(sub_82635E98, SetRenderStateHook<67>);  // D3DDevice_SetRenderState_Wrap7
BANJO_RENUT_HOOK(sub_82635EB8, SetRenderStateHook<68>);  // D3DDevice_SetRenderState_Wrap8
BANJO_RENUT_HOOK(sub_82635ED8, SetRenderStateHook<69>);  // D3DDevice_SetRenderState_Wrap9
BANJO_RENUT_HOOK(sub_82635F00, SetRenderStateHook<70>);  // D3DDevice_SetRenderState_Wrap10
BANJO_RENUT_HOOK(sub_82635F28, SetRenderStateHook<71>);  // D3DDevice_SetRenderState_Wrap11
BANJO_RENUT_HOOK(sub_82635F50, SetRenderStateHook<72>);  // D3DDevice_SetRenderState_Wrap12
BANJO_RENUT_HOOK(sub_82635F78, SetRenderStateHook<73>);  // D3DDevice_SetRenderState_Wrap13
BANJO_RENUT_HOOK(sub_82635FA0, SetRenderStateHook<74>);  // D3DDevice_SetRenderState_Wrap14
BANJO_RENUT_HOOK(sub_82635FC8, SetRenderStateHook<75>);  // D3DDevice_SetRenderState_Wrap15
BANJO_RENUT_HOOK(sub_821D53E8, SetRenderStateHook<76>);  // D3DDevice_SetRenderState_ViewportEnable
BANJO_RENUT_HOOK(sub_826360F8, SetRenderStateHook<77>);  // D3DDevice_SetRenderState_HighPrecisionBlendEnable
BANJO_RENUT_HOOK(sub_82636190, SetRenderStateHook<78>);  // D3DDevice_SetRenderState_HighPrecisionBlendEnable1
BANJO_RENUT_HOOK(sub_82636228, SetRenderStateHook<79>);  // D3DDevice_SetRenderState_HighPrecisionBlendEnable2
BANJO_RENUT_HOOK(sub_826362C0, SetRenderStateHook<80>);  // D3DDevice_SetRenderState_HighPrecisionBlendEnable3
BANJO_RENUT_HOOK(sub_82636400, SetRenderStateHook<81>);  // D3DDevice_SetRenderState_HalfPixelOffset
BANJO_RENUT_HOOK(sub_82636438, SetRenderStateHook<82>);  // D3DDevice_SetRenderState_PrimitiveResetEnable
BANJO_RENUT_HOOK(sub_82636468, SetRenderStateHook<83>);  // D3DDevice_SetRenderState_PrimitiveResetIndex
BANJO_RENUT_HOOK(sub_82636490, SetRenderStateHook<84>);  // D3DDevice_SetRenderState_AlphaToMaskEnable
BANJO_RENUT_HOOK(sub_826364C0, SetRenderStateHook<85>);  // D3DDevice_SetRenderState_AlphaToMaskOffsets
BANJO_RENUT_HOOK(sub_826364E8, SetRenderStateHook<86>);  // D3DDevice_SetRenderState_GuardBand_X
BANJO_RENUT_HOOK(sub_82636518, SetRenderStateHook<87>);  // D3DDevice_SetRenderState_GuardBand_Y
BANJO_RENUT_HOOK(sub_82636548, SetRenderStateHook<88>);  // D3DDevice_SetRenderState_DiscardBand_X
BANJO_RENUT_HOOK(sub_82636570, SetRenderStateHook<89>);  // D3DDevice_SetRenderState_DiscardBand_Y
BANJO_RENUT_HOOK(sub_826365E0, SetRenderStateHook<90>);  // D3DDevice_SetRenderState_HiStencilEnable
BANJO_RENUT_HOOK(sub_82636610, SetRenderStateHook<91>);  // D3DDevice_SetRenderState_HiStencilWriteEnable
BANJO_RENUT_HOOK(sub_82636640, SetRenderStateHook<92>);  // D3DDevice_SetRenderState_HiStencilFunc
BANJO_RENUT_HOOK(sub_82636670, SetRenderStateHook<93>);  // D3DDevice_SetRenderState_HiStencilRef
BANJO_RENUT_HOOK(sub_82636690, SetRenderStateHook<94>);  // D3DDevice_SetRenderState_PresentInterval
BANJO_RENUT_HOOK(sub_826366A0, SetRenderStateHook<95>);  // D3DDevice_SetRenderState_PresentImmediateThreshold
BANJO_RENUT_HOOK(sub_826365A0, SetRenderStateHook<96>);  // D3DDevice_SetRenderState_HiZEnable
BANJO_RENUT_HOOK(sub_82257938, SetRenderStateHook<97>);  // D3DDevice_SetRenderState_HiZWriteEnable
BANJO_RENUT_HOOK(sub_82635660, SetRenderStateHook<98>);  // D3DDevice_SetRenderState_LastPixel
BANJO_RENUT_HOOK(sub_82635698, SetRenderStateHook<99>);  // D3DDevice_SetRenderState_LineWidth
BANJO_RENUT_HOOK(sub_82635710, SetRenderStateHook<100>);  // D3DDevice_SetRenderState_Buffer2Frames

// Sampler states, by their index in N&B's table (off_82E50BD0).
BANJO_RENUT_HOOK(sub_8224B2C0, SetSamplerStateHook<0>);  // D3DDevice_SetSamplerState_AddressU
BANJO_RENUT_HOOK(sub_821DB8C8, SetSamplerStateHook<1>);  // D3DDevice_SetSamplerState_AddressV
BANJO_RENUT_HOOK(sub_82636C30, SetSamplerStateHook<2>);  // D3DDevice_SetSamplerState_AddressW
BANJO_RENUT_HOOK(sub_82636B90, SetSamplerStateHook<3>);  // D3DDevice_SetSamplerState_BorderColor
BANJO_RENUT_HOOK(sub_821FE308, SetSamplerStateHook<4>);  // D3DDevice_SetSamplerState_MagFilter
BANJO_RENUT_HOOK(sub_821FE3D0, SetSamplerStateHook<5>);  // D3DDevice_SetSamplerState_MinFilter
BANJO_RENUT_HOOK(sub_8225D108, SetSamplerStateHook<6>);  // D3DDevice_SetSamplerState_MipFilter
BANJO_RENUT_HOOK(sub_82636A58, SetSamplerStateHook<7>);  // D3DDevice_SetSamplerState_MipMapLodBias
BANJO_RENUT_HOOK(sub_821FE6D8, SetSamplerStateHook<8>);  // D3DDevice_SetSamplerState_MaxMipLevel
BANJO_RENUT_HOOK(sub_82636938, SetSamplerStateHook<9>);  // D3DDevice_SetSamplerState_MaxAnisotropy
BANJO_RENUT_HOOK(sub_826367D8, SetSamplerStateHook<10>);  // D3DDevice_SetSamplerState_MagFilterZ
BANJO_RENUT_HOOK(sub_826366F8, SetSamplerStateHook<11>);  // D3DDevice_SetSamplerState_MinFilterZ
BANJO_RENUT_HOOK(sub_82636890, SetSamplerStateHook<12>);  // D3DDevice_SetSamplerState_SeparateZFilterEnable
BANJO_RENUT_HOOK(sub_82636B10, SetSamplerStateHook<13>);  // D3DDevice_SetSamplerState_MinMipLevel
BANJO_RENUT_HOOK(sub_82636C80, SetSamplerStateHook<14>);  // D3DDevice_SetSamplerState_TrilinearThreshold
BANJO_RENUT_HOOK(sub_826369B0, SetSamplerStateHook<15>);  // D3DDevice_SetSamplerState_AnisotropyBias
BANJO_RENUT_HOOK(sub_82636CD8, SetSamplerStateHook<16>);  // D3DDevice_SetSamplerState_HGradientExpBias
BANJO_RENUT_HOOK(sub_82636D30, SetSamplerStateHook<17>);  // D3DDevice_SetSamplerState_VGradientExpBias
BANJO_RENUT_HOOK(sub_82636D88, SetSamplerStateHook<18>);  // D3DDevice_SetSamplerState_WhiteBorderColorW
BANJO_RENUT_HOOK(sub_82636DE0, SetSamplerStateHook<19>);  // D3DDevice_SetSamplerState_PointBorderEnable

BANJO_RENUT_HOOK(sub_821FE558, D3DDevice_SetTexture_hook);  // D3DDevice_SetTexture
BANJO_RENUT_HOOK(sub_82BF2578, D3DDevice_SetTexture_hook);  // D3DDevice_SetTexture_Inline
BANJO_RENUT_HOOK(sub_821F9438, D3DDevice_SetStreamSource_hook);  // D3DDevice_SetStreamSource
BANJO_RENUT_HOOK(sub_821DE438, D3DDevice_SetIndices_hook);  // D3DDevice_SetIndices
BANJO_RENUT_HOOK(sub_821D5060, D3DDevice_SetVertexDeclaration_hook);  // D3DDevice_SetVertexDeclaration
BANJO_RENUT_HOOK(sub_821F2090, D3DDevice_SetViewport_hook);  // D3DDevice_SetViewport
BANJO_RENUT_HOOK(sub_821F2110, D3D_SetViewport_hook);  // D3D_SetViewport
BANJO_RENUT_HOOK(sub_821F2368, D3DDevice_SetScissorRect_hook);  // D3DDevice_SetScissorRect
BANJO_RENUT_HOOK_SHARED(sub_821F2460, D3D_SetSurfaceClip_hook);  // D3D_SetSurfaceClip
BANJO_RENUT_HOOK(sub_82248BD0, D3DDevice_SetRenderTarget_hook);  // D3DDevice_SetRenderTarget
BANJO_RENUT_HOOK(sub_82248FB0, D3DDevice_SetDepthStencilSurface_hook);  // D3DDevice_SetDepthStencilSurface
BANJO_RENUT_HOOK(sub_82248F20, D3D_SetSurfaceInfo_hook);  // D3D_SetSurfaceInfo
BANJO_RENUT_HOOK_SHARED(sub_821F9A48, D3DDevice_SetPredication_hook);  // D3DDevice_SetPredication
BANJO_RENUT_HOOK_SHARED(sub_826411C0, D3DDevice_SetCommandBufferPredication_hook);  // D3DDevice_SetCommandBufferPredication
BANJO_RENUT_HOOK_SHARED(sub_821EFD38, D3DDevice_SetShaderGPRAllocation_hook);  // D3DDevice_SetShaderGPRAllocation
BANJO_RENUT_HOOK(sub_82637208, D3D_CleanupGprAllocationSetup_hook);  // D3D_CleanupGprAllocationSetup
BANJO_RENUT_HOOK_SHARED(sub_826414C0, D3DDevice_SetScreenExtentQueryMode_hook);  // D3DDevice_SetScreenExtentQueryMode
BANJO_RENUT_HOOK(sub_82634C10, D3DDevice_UnsetAll_hook);  // D3DDevice_UnsetAll
BANJO_RENUT_HOOK_SHARED(sub_82255C98, D3D_ResetAllState_hook);  // D3D_ResetAllState
BANJO_RENUT_HOOK_SHARED(sub_82634B90, D3D_InvalidateConstants_hook);  // D3D_InvalidateConstants
BANJO_RENUT_HOOK_SHARED(sub_82640010, D3DDevice_BlockUntilIdle_hook);  // D3DDevice_BlockUntilIdle
// Through a thunk (j_D3DDevice_SetRenderTarget 0x82636EC8).
BANJO_RENUT_HOOK(sub_82636EC8, D3DDevice_SetRenderTarget_hook);  // D3DDevice_SetRenderTarget_Thunk
BANJO_RENUT_HOOK(sub_82636ED0, D3DDevice_GetRenderTarget_hook);  // D3DDevice_GetRenderTarget
BANJO_RENUT_HOOK(sub_82636F10, D3DDevice_GetDepthStencilSurface_hook);  // D3DDevice_GetDepthStencilSurface
BANJO_RENUT_HOOK(sub_82636E40, D3DDevice_GetStreamSource_hook);  // D3DDevice_GetStreamSource
BANJO_RENUT_HOOK(sub_82244B18, D3DDevice_GetViewport_hook);  // D3DDevice_GetViewport
BANJO_RENUT_HOOK(sub_826370E8, D3DDevice_SetClipPlane_hook);  // D3DDevice_SetClipPlane
BANJO_RENUT_HOOK(sub_82637138, D3DDevice_SetVertexFetchConstant_hook);  // D3DDevice_SetVertexFetchConstant
BANJO_RENUT_HOOK(sub_821F0B40, D3DDevice_SetVertexShaderConstantB_hook);  // D3DDevice_SetVertexShaderConstantB
BANJO_RENUT_HOOK(sub_82237208, D3DDevice_SetPixelShaderConstantB_hook);  // D3DDevice_SetPixelShaderConstantB
BANJO_RENUT_HOOK(sub_826374B0, D3DDevice_SetVertexShaderConstantI_hook);  // D3DDevice_SetVertexShaderConstantI
BANJO_RENUT_HOOK(sub_82637500, D3DDevice_SetPixelShaderConstantI_hook);  // D3DDevice_SetPixelShaderConstantI
BANJO_RENUT_HOOK(sub_8263ED90, D3DDevice_EndIndexedVertices_hook);  // D3DDevice_EndIndexedVertices

// Render state getters, by their index in N&B's table (off_82E506F0, field 0).
BANJO_RENUT_HOOK(sub_82635778, GetRenderStateHook<10>);  // D3DDevice_GetRenderState_ZEnable
BANJO_RENUT_HOOK(sub_826357B0, GetRenderStateHook<11>);  // D3DDevice_GetRenderState_ZFunc
BANJO_RENUT_HOOK(sub_826357A0, GetRenderStateHook<12>);  // D3DDevice_GetRenderState_ZWriteEnable
BANJO_RENUT_HOOK(sub_82635488, GetRenderStateHook<13>);  // D3DDevice_GetRenderState_FillMode
BANJO_RENUT_HOOK(sub_82635478, GetRenderStateHook<14>);  // D3DDevice_GetRenderState_CullMode
BANJO_RENUT_HOOK(sub_826354D0, GetRenderStateHook<15>);  // D3DDevice_GetRenderState_AlphaBlendEnable
BANJO_RENUT_HOOK(sub_82635540, GetRenderStateHook<16>);  // D3DDevice_GetRenderState_SeparateAlphaBlendEnable
BANJO_RENUT_HOOK(sub_826355E8, GetRenderStateHook<17>);  // D3DDevice_GetRenderState_BlendFactor
BANJO_RENUT_HOOK(sub_826354F0, GetRenderStateHook<18>);  // D3DDevice_GetRenderState_SrcBlend
BANJO_RENUT_HOOK(sub_82635500, GetRenderStateHook<19>);  // D3DDevice_GetRenderState_DestBlend
BANJO_RENUT_HOOK(sub_826354E0, GetRenderStateHook<20>);  // D3DDevice_GetRenderState_BlendOp
BANJO_RENUT_HOOK(sub_82635520, GetRenderStateHook<21>);  // D3DDevice_GetRenderState_SrcBlendAlpha
BANJO_RENUT_HOOK(sub_82635530, GetRenderStateHook<22>);  // D3DDevice_GetRenderState_DestBlendAlpha
BANJO_RENUT_HOOK(sub_82635510, GetRenderStateHook<23>);  // D3DDevice_GetRenderState_BlendOpAlpha
BANJO_RENUT_HOOK(sub_826354C0, GetRenderStateHook<24>);  // D3DDevice_GetRenderState_AlphaTestEnable
BANJO_RENUT_HOOK(sub_82635588, GetRenderStateHook<25>);  // D3DDevice_GetRenderState_AlphaRef
BANJO_RENUT_HOOK(sub_826355D8, GetRenderStateHook<26>);  // D3DDevice_GetRenderState_AlphaFunc
BANJO_RENUT_HOOK(sub_826357F8, GetRenderStateHook<27>);  // D3DDevice_GetRenderState_StencilEnable
BANJO_RENUT_HOOK(sub_82635828, GetRenderStateHook<28>);  // D3DDevice_GetRenderState_TwoSidedStencilMode
BANJO_RENUT_HOOK(sub_82635890, GetRenderStateHook<29>);  // D3DDevice_GetRenderState_StencilFail
BANJO_RENUT_HOOK(sub_826358C8, GetRenderStateHook<30>);  // D3DDevice_GetRenderState_StencilZFail
BANJO_RENUT_HOOK(sub_826358F8, GetRenderStateHook<31>);  // D3DDevice_GetRenderState_StencilPass
BANJO_RENUT_HOOK(sub_82635858, GetRenderStateHook<32>);  // D3DDevice_GetRenderState_StencilFunc
BANJO_RENUT_HOOK(sub_826359F0, GetRenderStateHook<33>);  // D3DDevice_GetRenderState_StencilRef
BANJO_RENUT_HOOK(sub_82635A10, GetRenderStateHook<34>);  // D3DDevice_GetRenderState_StencilMask
BANJO_RENUT_HOOK(sub_82635A30, GetRenderStateHook<35>);  // D3DDevice_GetRenderState_StencilWriteMask
BANJO_RENUT_HOOK(sub_82635960, GetRenderStateHook<36>);  // D3DDevice_GetRenderState_CCWStencilFail
BANJO_RENUT_HOOK(sub_82635998, GetRenderStateHook<37>);  // D3DDevice_GetRenderState_CCWStencilZFail
BANJO_RENUT_HOOK(sub_826359C8, GetRenderStateHook<38>);  // D3DDevice_GetRenderState_CCWStencilPass
BANJO_RENUT_HOOK(sub_82635928, GetRenderStateHook<39>);  // D3DDevice_GetRenderState_CCWStencilFunc
BANJO_RENUT_HOOK(sub_82635A50, GetRenderStateHook<40>);  // D3DDevice_GetRenderState_CCWStencilRef
BANJO_RENUT_HOOK(sub_82635A70, GetRenderStateHook<41>);  // D3DDevice_GetRenderState_CCWStencilMask
BANJO_RENUT_HOOK(sub_82635A90, GetRenderStateHook<42>);  // D3DDevice_GetRenderState_CCWStencilWriteMask
BANJO_RENUT_HOOK(sub_82635A98, GetRenderStateHook<43>);  // D3DDevice_GetRenderState_ClipPlaneEnable
BANJO_RENUT_HOOK(sub_82635D10, GetRenderStateHook<44>);  // D3DDevice_GetRenderState_PointSize
BANJO_RENUT_HOOK(sub_82635D68, GetRenderStateHook<45>);  // D3DDevice_GetRenderState_PointSizeMin
BANJO_RENUT_HOOK(sub_82635CB8, GetRenderStateHook<46>);  // D3DDevice_GetRenderState_PointSpriteEnable
BANJO_RENUT_HOOK(sub_82635D78, GetRenderStateHook<47>);  // D3DDevice_GetRenderState_PointSizeMax
BANJO_RENUT_HOOK(sub_82635BB8, GetRenderStateHook<48>);  // D3DDevice_GetRenderState_MultiSampleAntiAlias
BANJO_RENUT_HOOK(sub_82635BE0, GetRenderStateHook<49>);  // D3DDevice_GetRenderState_MultiSampleMask
BANJO_RENUT_HOOK(sub_82635AB8, GetRenderStateHook<50>);  // D3DDevice_GetRenderState_ScissorTestEnable
BANJO_RENUT_HOOK(sub_82635B68, GetRenderStateHook<51>);  // D3DDevice_GetRenderState_SlopeScaleDepthBias
BANJO_RENUT_HOOK(sub_82635B88, GetRenderStateHook<52>);  // D3DDevice_GetRenderState_DepthBias
BANJO_RENUT_HOOK(sub_82635BE8, GetRenderStateHook<53>);  // D3DDevice_GetRenderState_ColorWriteEnable
BANJO_RENUT_HOOK(sub_82635C28, GetRenderStateHook<54>);  // D3DDevice_GetRenderState_ColorWriteEnable1
BANJO_RENUT_HOOK(sub_82635C68, GetRenderStateHook<55>);  // D3DDevice_GetRenderState_ColorWriteEnable2
BANJO_RENUT_HOOK(sub_82635CA8, GetRenderStateHook<56>);  // D3DDevice_GetRenderState_ColorWriteEnable3
BANJO_RENUT_HOOK(sub_826363F0, GetRenderStateHook<57>);  // D3DDevice_GetRenderState_TessellationMode
BANJO_RENUT_HOOK(sub_82636380, GetRenderStateHook<58>);  // D3DDevice_GetRenderState_MinTessellationLevel
BANJO_RENUT_HOOK(sub_826363B8, GetRenderStateHook<59>);  // D3DDevice_GetRenderState_MaxTessellationLevel
BANJO_RENUT_HOOK(sub_82635FE8, GetRenderStateHook<60>);  // D3DDevice_GetRenderState_Wrap0
BANJO_RENUT_HOOK(sub_82635FF8, GetRenderStateHook<61>);  // D3DDevice_GetRenderState_Wrap1
BANJO_RENUT_HOOK(sub_82636008, GetRenderStateHook<62>);  // D3DDevice_GetRenderState_Wrap2
BANJO_RENUT_HOOK(sub_82636018, GetRenderStateHook<63>);  // D3DDevice_GetRenderState_Wrap3
BANJO_RENUT_HOOK(sub_82636028, GetRenderStateHook<64>);  // D3DDevice_GetRenderState_Wrap4
BANJO_RENUT_HOOK(sub_82636038, GetRenderStateHook<65>);  // D3DDevice_GetRenderState_Wrap5
BANJO_RENUT_HOOK(sub_82636048, GetRenderStateHook<66>);  // D3DDevice_GetRenderState_Wrap6
BANJO_RENUT_HOOK(sub_82636058, GetRenderStateHook<67>);  // D3DDevice_GetRenderState_Wrap7
BANJO_RENUT_HOOK(sub_82636068, GetRenderStateHook<68>);  // D3DDevice_GetRenderState_Wrap8
BANJO_RENUT_HOOK(sub_82636078, GetRenderStateHook<69>);  // D3DDevice_GetRenderState_Wrap9
BANJO_RENUT_HOOK(sub_82636088, GetRenderStateHook<70>);  // D3DDevice_GetRenderState_Wrap10
BANJO_RENUT_HOOK(sub_82636098, GetRenderStateHook<71>);  // D3DDevice_GetRenderState_Wrap11
BANJO_RENUT_HOOK(sub_826360A8, GetRenderStateHook<72>);  // D3DDevice_GetRenderState_Wrap12
BANJO_RENUT_HOOK(sub_826360B8, GetRenderStateHook<73>);  // D3DDevice_GetRenderState_Wrap13
BANJO_RENUT_HOOK(sub_826360C8, GetRenderStateHook<74>);  // D3DDevice_GetRenderState_Wrap14
BANJO_RENUT_HOOK(sub_826360D8, GetRenderStateHook<75>);  // D3DDevice_GetRenderState_Wrap15
BANJO_RENUT_HOOK(sub_826360E8, GetRenderStateHook<76>);  // D3DDevice_GetRenderState_ViewportEnable
BANJO_RENUT_HOOK(sub_82636188, GetRenderStateHook<77>);  // D3DDevice_GetRenderState_HighPrecisionBlendEnable
BANJO_RENUT_HOOK(sub_82636220, GetRenderStateHook<78>);  // D3DDevice_GetRenderState_HighPrecisionBlendEnable1
BANJO_RENUT_HOOK(sub_826362B8, GetRenderStateHook<79>);  // D3DDevice_GetRenderState_HighPrecisionBlendEnable2
BANJO_RENUT_HOOK(sub_82636350, GetRenderStateHook<80>);  // D3DDevice_GetRenderState_HighPrecisionBlendEnable3
BANJO_RENUT_HOOK(sub_82636428, GetRenderStateHook<81>);  // D3DDevice_GetRenderState_HalfPixelOffset
BANJO_RENUT_HOOK(sub_82636458, GetRenderStateHook<82>);  // D3DDevice_GetRenderState_PrimitiveResetEnable
BANJO_RENUT_HOOK(sub_82636488, GetRenderStateHook<83>);  // D3DDevice_GetRenderState_PrimitiveResetIndex
BANJO_RENUT_HOOK(sub_826364B0, GetRenderStateHook<84>);  // D3DDevice_GetRenderState_AlphaToMaskEnable
BANJO_RENUT_HOOK(sub_826364E0, GetRenderStateHook<85>);  // D3DDevice_GetRenderState_AlphaToMaskOffsets
BANJO_RENUT_HOOK(sub_82636510, GetRenderStateHook<86>);  // D3DDevice_GetRenderState_GuardBand_X
BANJO_RENUT_HOOK(sub_82636540, GetRenderStateHook<87>);  // D3DDevice_GetRenderState_GuardBand_Y
BANJO_RENUT_HOOK(sub_82636568, GetRenderStateHook<88>);  // D3DDevice_GetRenderState_DiscardBand_X
BANJO_RENUT_HOOK(sub_82636598, GetRenderStateHook<89>);  // D3DDevice_GetRenderState_DiscardBand_Y
BANJO_RENUT_HOOK(sub_82636600, GetRenderStateHook<90>);  // D3DDevice_GetRenderState_HiStencilEnable
BANJO_RENUT_HOOK(sub_82636630, GetRenderStateHook<91>);  // D3DDevice_GetRenderState_HiStencilWriteEnable
BANJO_RENUT_HOOK(sub_82636660, GetRenderStateHook<92>);  // D3DDevice_GetRenderState_HiStencilFunc
BANJO_RENUT_HOOK(sub_82636688, GetRenderStateHook<93>);  // D3DDevice_GetRenderState_HiStencilRef
BANJO_RENUT_HOOK(sub_82636698, GetRenderStateHook<94>);  // D3DDevice_GetRenderState_PresentInterval
BANJO_RENUT_HOOK(sub_826366B0, GetRenderStateHook<95>);  // D3DDevice_GetRenderState_PresentImmediateThreshold
BANJO_RENUT_HOOK(sub_826365C0, GetRenderStateHook<96>);  // D3DDevice_GetRenderState_HiZEnable
BANJO_RENUT_HOOK(sub_826365D0, GetRenderStateHook<97>);  // D3DDevice_GetRenderState_HiZWriteEnable
BANJO_RENUT_HOOK(sub_82635688, GetRenderStateHook<98>);  // D3DDevice_GetRenderState_LastPixel
BANJO_RENUT_HOOK(sub_826356D8, GetRenderStateHook<99>);  // D3DDevice_GetRenderState_LineWidth
BANJO_RENUT_HOOK(sub_82635730, GetRenderStateHook<100>);  // D3DDevice_GetRenderState_Buffer2Frames

// Sampler state getters (off_82E50BD0, field 0).
BANJO_RENUT_HOOK(sub_82636C00, GetSamplerStateHook<0>);  // D3DDevice_GetSamplerState_AddressU
BANJO_RENUT_HOOK(sub_82636C18, GetSamplerStateHook<1>);  // D3DDevice_GetSamplerState_AddressV
BANJO_RENUT_HOOK(sub_82636C68, GetSamplerStateHook<2>);  // D3DDevice_GetSamplerState_AddressW
BANJO_RENUT_HOOK(sub_82636BD8, GetSamplerStateHook<3>);  // D3DDevice_GetSamplerState_BorderColor
BANJO_RENUT_HOOK(sub_826367A0, GetSamplerStateHook<4>);  // D3DDevice_GetSamplerState_MagFilter
BANJO_RENUT_HOOK(sub_826366C0, GetSamplerStateHook<5>);  // D3DDevice_GetSamplerState_MinFilter
BANJO_RENUT_HOOK(sub_82636878, GetSamplerStateHook<6>);  // D3DDevice_GetSamplerState_MipFilter
BANJO_RENUT_HOOK(sub_82636AB8, GetSamplerStateHook<7>);  // D3DDevice_GetSamplerState_MipMapLodBias
BANJO_RENUT_HOOK(sub_82636B00, GetSamplerStateHook<8>);  // D3DDevice_GetSamplerState_MaxMipLevel
BANJO_RENUT_HOOK(sub_826369A0, GetSamplerStateHook<9>);  // D3DDevice_GetSamplerState_MaxAnisotropy
BANJO_RENUT_HOOK(sub_82636868, GetSamplerStateHook<10>);  // D3DDevice_GetSamplerState_MagFilterZ
BANJO_RENUT_HOOK(sub_82636790, GetSamplerStateHook<11>);  // D3DDevice_GetSamplerState_MinFilterZ
BANJO_RENUT_HOOK(sub_82636928, GetSamplerStateHook<12>);  // D3DDevice_GetSamplerState_SeparateZFilterEnable
BANJO_RENUT_HOOK(sub_82636B80, GetSamplerStateHook<13>);  // D3DDevice_GetSamplerState_MinMipLevel
BANJO_RENUT_HOOK(sub_82636CC0, GetSamplerStateHook<14>);  // D3DDevice_GetSamplerState_TrilinearThreshold
BANJO_RENUT_HOOK(sub_82636A10, GetSamplerStateHook<15>);  // D3DDevice_GetSamplerState_AnisotropyBias
BANJO_RENUT_HOOK(sub_82636D18, GetSamplerStateHook<16>);  // D3DDevice_GetSamplerState_HGradientExpBias
BANJO_RENUT_HOOK(sub_82636D70, GetSamplerStateHook<17>);  // D3DDevice_GetSamplerState_VGradientExpBias
BANJO_RENUT_HOOK(sub_82636DC8, GetSamplerStateHook<18>);  // D3DDevice_GetSamplerState_WhiteBorderColorW
BANJO_RENUT_HOOK(sub_82636E28, GetSamplerStateHook<19>);  // D3DDevice_GetSamplerState_PointBorderEnable
