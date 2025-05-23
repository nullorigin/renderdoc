/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2019-2025 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "3rdparty/fmt/core.h"
#include "d3d12_test.h"

RD_TEST(D3D12_Subgroup_Zoo, D3D12GraphicsTest)
{
  static constexpr const char *Description =
      "Test of behaviour around subgroup operations in shaders.";

  const std::string common = R"EOSHADER(

cbuffer rootconsts : register(b0)
{
  uint root_test;
}

#define IsTest(x) (root_test == x)

)EOSHADER";

  const std::string pixelCommon = common + R"EOSHADER(

struct IN
{
  float4 pos : SV_Position;
  float4 data : DATA;
};

)EOSHADER";

  const std::string compCommon = common + R"EOSHADER(

RWStructuredBuffer<float4> outbuf : register(u0);

static uint3 tid;

void SetOutput(float4 data)
{
  outbuf[root_test * 1024 + tid.y * GROUP_SIZE_X + tid.x] = data;
}

)EOSHADER";

  const std::string vertex = common + R"EOSHADER(

struct OUT
{
  float4 pos : SV_Position;
  float4 data : DATA;
};

OUT main(uint vert : SV_VertexID)
{
  OUT ret = (OUT)0;

  float2 positions[] = {
    float2(-1.0f,  1.0f),
    float2( 1.0f,  1.0f),
    float2(-1.0f, -1.0f),
    float2( 1.0f, -1.0f),
  };

  float scale = 1.0f;
  if(IsTest(2))
    scale = 0.2f;

  ret.pos = float4(positions[vert]*float2(scale,scale), 0, 1);

  ret.data = 0.0f.xxxx;

  uint wave = WaveGetLaneIndex();

  if(IsTest(0))
    ret.data = float4(wave, 0, 0, 1);
  else if(IsTest(3))
    ret.data = float4(WaveActiveSum(wave), 0, 0, 0);

  return ret;
}

)EOSHADER";

  const std::string pixel = pixelCommon + R"EOSHADER(

float4 main(IN input) : SV_Target0
{
  uint subgroupId = WaveGetLaneIndex();

  float4 pixdata = 0.0f.xxxx;

  if(IsTest(1) || IsTest(2))
  {
    pixdata = float4(subgroupId, 0, 0, 1);
  }
  else if(IsTest(4))
  {
    pixdata = float4(WaveActiveSum(subgroupId), 0, 0, 0);
  }
  else if(IsTest(5))
  {
    // QuadReadLaneAt : unit tests
    pixdata.x = float(QuadReadLaneAt(subgroupId, 0));
    pixdata.y = float(QuadReadLaneAt(subgroupId, 1));
    pixdata.z = float(QuadReadLaneAt(subgroupId, 2));
    pixdata.w = float(QuadReadLaneAt(subgroupId, 3));
  }
  else if(IsTest(6))
  {
    // QuadReadAcrossDiagonal, QuadReadAcrossX, QuadReadAcrossY: unit tests
    pixdata.x = float(QuadReadAcrossDiagonal(subgroupId));
    pixdata.y = float(QuadReadAcrossX(subgroupId));
    pixdata.z = float(QuadReadAcrossY(subgroupId));
    pixdata.w = QuadReadLaneAt(pixdata.x, 2);
  }
  else if(IsTest(7))
  {
    // QuadAny, QuadAll: unit tests
    pixdata.x = float(QuadAny(subgroupId > 2));
    pixdata.y = float(QuadAll(subgroupId < 10));
    pixdata.z = float(QuadAny(pixdata.x == 0.0f));
    pixdata.w = float(QuadAll(pixdata.x == 0.0f));
  }

  return input.data + pixdata;
}

)EOSHADER";

  const std::string pixel67 = pixelCommon + R"EOSHADER(

float4 main(IN input) : SV_Target0
{
  uint subgroupId = WaveGetLaneIndex();

  float4 pixdata = 0.0f.xxxx;

  if(IsTest(0))
  {
    // QuadAny, QuadAll: unit tests
    pixdata.x = float(QuadAny(subgroupId > 2));
    pixdata.y = float(QuadAll(subgroupId < 10));
    pixdata.z = float(QuadAny(pixdata.x == 0.0f));
    pixdata.w = float(QuadAll(pixdata.x == 0.0f));
  }

  return pixdata;
}

)EOSHADER";

  const std::string comp = compCommon + R"EOSHADER(

[numthreads(GROUP_SIZE_X, GROUP_SIZE_Y, 1)]
void main(uint3 inTid : SV_DispatchThreadID)
{
  float4 data = 0.0f.xxxx;
  tid = inTid;

  uint id = WaveGetLaneIndex();

  SetOutput(id);

  if(IsTest(0))
  {
    // Query functions : unit tests
    data.x = float(WaveGetLaneCount());
    data.y = float(WaveGetLaneIndex());
    data.z = float(WaveIsFirstLane());
  }
  else if(IsTest(1))
  {
    // Vote functions : unit tests
    data.x = float(WaveActiveAnyTrue(id*2 > id+10));
    data.y = float(WaveActiveAllTrue(id < WaveGetLaneCount()));
    if (id > 10)
    {
      data.z = float(WaveActiveAllTrue(id > 10));
      uint4 ballot = WaveActiveBallot(id > 20);
      data.w = countbits(ballot.x) + countbits(ballot.y) + countbits(ballot.z) + countbits(ballot.w);
    }
    else
    {
      data.z = float(WaveActiveAllTrue(id > 3));
      uint4 ballot = WaveActiveBallot(id > 4);
      data.w = countbits(ballot.x) + countbits(ballot.y) + countbits(ballot.z) + countbits(ballot.w);
    }
  }
  else if(IsTest(2))
  {
    // Broadcast functions : unit tests
    if (id >= 2 && id <= 20)
    {
      data.x = WaveReadLaneFirst(id);
      data.y = WaveReadLaneAt(id, 5);
      data.z = WaveReadLaneAt(id, id);
      data.w = WaveReadLaneAt(data.x, 2+id%3);
    }
  }
  else if(IsTest(3))
  {
    // Scan and Prefix functions : unit tests
    if (id >= 2 && id <= 20)
    {
      data.x = WavePrefixCountBits(id > 4);
      data.y = WavePrefixCountBits(id > 10);
      data.z = WavePrefixSum(data.x);
      data.w = WavePrefixProduct(1 + data.y);
    }
    else
    {
      data.x = WavePrefixCountBits(id > 23);
      data.y = WavePrefixCountBits(id < 1);
      data.z = WavePrefixSum(data.x);
      data.w = WavePrefixSum(data.y);
    }
  }
  else if(IsTest(4))
  {
    // Reduction functions : unit tests
    if (id >= 2 && id <= 20)
    {
      data.x = float(WaveActiveMax(id));
      data.y = float(WaveActiveMin(id));
      data.z = float(WaveActiveProduct(id));
      data.w = float(WaveActiveSum(id));
    }
  }
  else if(IsTest(5))
  {
    // Reduction functions : unit tests
    if (id >= 2 && id <= 20)
    {
      data.x = float(WaveActiveCountBits(id > 23));
      data.y = float(WaveActiveBitAnd(id));
      data.z = float(WaveActiveBitOr(id));
      data.w = float(WaveActiveBitXor(id));
    }
  }
  else if(IsTest(6))
  {
    // Reduction functions : unit tests
    if (id > 13)
    {
      bool test1 = (id > 15).x;
      bool2 test2 = bool2(test1, (id < 23));
      bool3 test3 = bool3(test1, (id < 23), (id >= 25));
      bool4 test4 = bool4(test1, (id < 23), (id >= 25), (id >= 28));

      data.x = float(WaveActiveAllEqual(test1).x);
      data.y = float(WaveActiveAllEqual(test2).y);
      data.z = float(WaveActiveAllEqual(test3).z);
      data.w = float(WaveActiveAllEqual(test4).w);
    }
  }
  SetOutput(data);
}

)EOSHADER";

  const std::string comp65 = compCommon + R"EOSHADER(

[numthreads(GROUP_SIZE_X, GROUP_SIZE_Y, 1)]
void main(uint3 inTid : SV_DispatchThreadID)
{
  float4 data = 0.0f.xxxx;
  tid = inTid;

  uint id = WaveGetLaneIndex();

  SetOutput(id);

  if(IsTest(0))
  {
    // SM6.5 functions : unit tests
    uint4 mask = WaveMatch(id);
    data.x = countbits(mask.x) + countbits(mask.y) + countbits(mask.z) + countbits(mask.w);
    mask = WaveMatch(id%3 == 1);
    data.y = countbits(mask.x) + countbits(mask.y) + countbits(mask.z) + countbits(mask.w);
    mask = WaveMatch(id%5 == 1);
		data.z = WaveMultiPrefixSum(id, mask);
		data.w = WaveMultiPrefixProduct(id, mask);
  }
  if(IsTest(1))
  {
    // SM6.5 functions : unit tests
    uint4 mask = WaveMatch(id%7 == 1);
		data.x = WaveMultiPrefixCountBits(id, mask);
		data.y = WaveMultiPrefixBitAnd((id+7)*3, mask);
		data.z = WaveMultiPrefixBitOr(id, mask);
		data.w = WaveMultiPrefixBitXor(id, mask);
  }
  SetOutput(data);
}

)EOSHADER";

  void Prepare(int argc, char **argv)
  {
    D3D12GraphicsTest::Prepare(argc, argv);

    if(opts1.WaveLaneCountMax < 16)
      Avail = "Subgroup size is less than 16";

    bool supportSM60 = (m_HighestShaderModel >= D3D_SHADER_MODEL_6_0) && m_DXILSupport;
    if(!supportSM60)
      Avail = "SM 6.0 not supported";
  }

  int main()
  {
    // initialise, create window, create device, etc
    if(!Init())
      return 3;

    ID3D12RootSignaturePtr sig = MakeSig({constParam(D3D12_SHADER_VISIBILITY_ALL, 0, 0, 1),
                                          uavParam(D3D12_SHADER_VISIBILITY_ALL, 0, 0)});

    const uint32_t imgDim = 128;

    ID3D12ResourcePtr fltTex = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, imgDim, imgDim)
                                   .RTV()
                                   .InitialState(D3D12_RESOURCE_STATE_RENDER_TARGET);
    fltTex->SetName(L"fltTex");
    D3D12_CPU_DESCRIPTOR_HANDLE fltRTV = MakeRTV(fltTex).CreateCPU(0);
    D3D12_GPU_DESCRIPTOR_HANDLE fltSRV = MakeSRV(fltTex).CreateGPU(8);

    int vertTests = 0;
    int numPixelTests60 = 0;
    int numPixelTests67 = 0;
    int numCompTests60 = 0;
    int numCompTests65 = 0;

    {
      size_t pos = 0;
      while(pos != std::string::npos)
      {
        pos = pixel.find("IsTest(", pos);
        if(pos == std::string::npos)
          break;
        pos += sizeof("IsTest(") - 1;
        numPixelTests60 = std::max(numPixelTests60, atoi(pixel.c_str() + pos) + 1);
      }
      pos = 0;
      while(pos != std::string::npos)
      {
        pos = pixel67.find("IsTest(", pos);
        if(pos == std::string::npos)
          break;
        pos += sizeof("IsTest(") - 1;
        numPixelTests67 = std::max(numCompTests65, atoi(pixel67.c_str() + pos) + 1);
      }

      pos = 0;
      while(pos != std::string::npos)
      {
        pos = vertex.find("IsTest(", pos);
        if(pos == std::string::npos)
          break;
        pos += sizeof("IsTest(") - 1;
        vertTests = std::max(vertTests, atoi(vertex.c_str() + pos) + 1);
      }

      pos = 0;
      while(pos != std::string::npos)
      {
        pos = comp.find("IsTest(", pos);
        if(pos == std::string::npos)
          break;
        pos += sizeof("IsTest(") - 1;
        numCompTests60 = std::max(numCompTests60, atoi(comp.c_str() + pos) + 1);
      }
      pos = 0;
      while(pos != std::string::npos)
      {
        pos = comp65.find("IsTest(", pos);
        if(pos == std::string::npos)
          break;
        pos += sizeof("IsTest(") - 1;
        numCompTests65 = std::max(numCompTests65, atoi(comp65.c_str() + pos) + 1);
      }
    }

    const uint32_t numGraphicsTests60 = std::max(vertTests, numPixelTests60);
    const uint32_t numGraphicsTests67 = numPixelTests67;
    const uint32_t numCompTests = std::max(numCompTests60, numCompTests65);

    struct
    {
      int x, y;
    } compsize[] = {
        {256, 1},
        {128, 2},
        {8, 128},
        {150, 1},
    };
    std::string comppipe_name[ARRAY_COUNT(compsize)];
    ID3D12PipelineStatePtr comppipe[ARRAY_COUNT(compsize)];
    ID3D12PipelineStatePtr comppipe65[ARRAY_COUNT(compsize)];

    std::string defines60;
    std::string defines65;

    bool supportSM65 = (m_HighestShaderModel >= D3D_SHADER_MODEL_6_5) && m_DXILSupport;
    bool supportSM67 = (m_HighestShaderModel >= D3D_SHADER_MODEL_6_7) && m_DXILSupport;

    ID3D12PipelineStatePtr graphics60 = MakePSO()
                                            .RootSig(sig)
                                            .VS(Compile(defines60 + vertex, "main", "vs_6_0"))
                                            .PS(Compile(defines60 + pixel, "main", "ps_6_0"))
                                            .RTVs({DXGI_FORMAT_R32G32B32A32_FLOAT});
    ID3D12PipelineStatePtr graphics67;
    if(supportSM67)
      graphics67 = MakePSO()
                       .RootSig(sig)
                       .VS(Compile(defines60 + vertex, "main", "vs_6_0"))
                       .PS(Compile(defines60 + pixel67, "main", "ps_6_7"))
                       .RTVs({DXGI_FORMAT_R32G32B32A32_FLOAT});

    for(int i = 0; i < ARRAY_COUNT(comppipe); i++)
    {
      std::string sizedefine;
      sizedefine = fmt::format("#define GROUP_SIZE_X {}\n#define GROUP_SIZE_Y {}\n", compsize[i].x,
                               compsize[i].y);
      comppipe_name[i] = fmt::format("{}x{}", compsize[i].x, compsize[i].y);

      comppipe[i] =
          MakePSO().RootSig(sig).CS(Compile(defines60 + sizedefine + comp, "main", "cs_6_0"));
      comppipe[i]->SetName(UTF82Wide(comppipe_name[i]).c_str());

      if(supportSM65)
      {
        comppipe65[i] =
            MakePSO().RootSig(sig).CS(Compile(defines65 + sizedefine + comp65, "main", "cs_6_5"));
        comppipe65[i]->SetName(UTF82Wide(comppipe_name[i]).c_str());
      }
    }

    ID3D12ResourcePtr bufOut = MakeBuffer().Size(sizeof(Vec4f) * 1024 * numCompTests).UAV();
    D3D12ViewCreator uavView =
        MakeUAV(bufOut).Format(DXGI_FORMAT_R32_UINT).NumElements(4 * 1024 * numCompTests);
    D3D12_CPU_DESCRIPTOR_HANDLE uavcpu = uavView.CreateClearCPU(10);
    D3D12_GPU_DESCRIPTOR_HANDLE uavgpu = uavView.CreateGPU(10);

    bufOut->SetName(L"bufOut");

    while(Running())
    {
      ID3D12GraphicsCommandListPtr cmd = GetCommandBuffer();

      Reset(cmd);

      cmd->SetDescriptorHeaps(1, &m_CBVUAVSRV.GetInterfacePtr());

      ID3D12ResourcePtr bb = StartUsingBackbuffer(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);

      ClearRenderTargetView(cmd, BBRTV, {0.2f, 0.2f, 0.2f, 1.0f});

      cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

      RSSetViewport(cmd, {0.0f, 0.0f, (float)imgDim, (float)imgDim, 0.0f, 1.0f});
      RSSetScissorRect(cmd, {0, 0, imgDim, imgDim});

      pushMarker(cmd, "Graphics Tests");

      cmd->SetPipelineState(graphics60);
      cmd->SetGraphicsRootSignature(sig);

      for(uint32_t i = 0; i < numGraphicsTests60; i++)
      {
        ResourceBarrier(cmd);

        OMSetRenderTargets(cmd, {fltRTV}, {});
        ClearRenderTargetView(cmd, fltRTV, {123456.0f, 789.0f, 101112.0f, 0.0f});

        cmd->SetGraphicsRoot32BitConstant(0, i, 0);
        cmd->DrawInstanced(4, 1, 0, 0);
      }

      if(supportSM67)
      {
        cmd->SetPipelineState(graphics67);
        cmd->SetGraphicsRootSignature(sig);

        for(uint32_t i = 0; i < numGraphicsTests67; i++)
        {
          ResourceBarrier(cmd);

          OMSetRenderTargets(cmd, {fltRTV}, {});
          ClearRenderTargetView(cmd, fltRTV, {123456.0f, 789.0f, 101112.0f, 0.0f});

          cmd->SetGraphicsRoot32BitConstant(0, i, 0);
          cmd->DrawInstanced(4, 1, 0, 0);
        }
      }

      popMarker(cmd);

      pushMarker(cmd, "Compute Tests");

      for(size_t p = 0; p < ARRAY_COUNT(comppipe); p++)
      {
        ResourceBarrier(cmd);

        UINT zero[4] = {};
        cmd->ClearUnorderedAccessViewUint(uavgpu, uavcpu, bufOut, zero, 0, NULL);

        ResourceBarrier(cmd);
        pushMarker(cmd, comppipe_name[p]);

        cmd->SetPipelineState(comppipe[p]);
        cmd->SetComputeRootSignature(sig);
        cmd->SetComputeRootUnorderedAccessView(1, bufOut->GetGPUVirtualAddress());

        for(int i = 0; i < numCompTests60; i++)
        {
          cmd->SetComputeRoot32BitConstant(0, i, 0);
          cmd->Dispatch(1, 1, 1);
        }

        popMarker(cmd);
      }

      for(size_t p = 0; p < ARRAY_COUNT(comppipe65); p++)
      {
        ResourceBarrier(cmd);

        UINT zero[4] = {};
        cmd->ClearUnorderedAccessViewUint(uavgpu, uavcpu, bufOut, zero, 0, NULL);

        ResourceBarrier(cmd);
        pushMarker(cmd, comppipe_name[p]);

        cmd->SetPipelineState(comppipe65[p]);
        cmd->SetComputeRootSignature(sig);
        cmd->SetComputeRootUnorderedAccessView(1, bufOut->GetGPUVirtualAddress());

        for(int i = 0; i < numCompTests65; i++)
        {
          cmd->SetComputeRoot32BitConstant(0, i, 0);
          cmd->Dispatch(1, 1, 1);
        }

        popMarker(cmd);
      }

      popMarker(cmd);

      FinishUsingBackbuffer(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);

      cmd->Close();

      SubmitAndPresent({cmd});
    }

    return 0;
  }
};

REGISTER_TEST();
