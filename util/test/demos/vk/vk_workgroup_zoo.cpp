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
#include "vk_test.h"

RD_TEST(VK_Workgroup_Zoo, VulkanGraphicsTest)
{
  static constexpr const char *Description =
      "Test of behaviour around workgroup operations in shaders.";

  const std::string common = R"EOSHADER(

#version 460 core
#extension GL_KHR_shader_subgroup_basic : enable
#extension GL_KHR_shader_subgroup_ballot : enable
#extension GL_KHR_shader_subgroup_vote : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable

#if FEAT_SHUFFLE
#extension GL_KHR_shader_subgroup_shuffle : enable
#endif

#if FEAT_SHUFFLE_RELATIVE
#extension GL_KHR_shader_subgroup_shuffle_relative : enable
#endif

#if FEAT_CLUSTERED
#extension GL_KHR_shader_subgroup_clustered : enable
#endif

#if FEAT_QUAD
#extension GL_KHR_shader_subgroup_quad : enable
#endif

#if FEAT_ROTATE || FEAT_ROTATE_CLUSTERED
#extension GL_KHR_shader_subgroup_rotate : enable
#endif

layout(push_constant) uniform PushData
{
  uint test;
} push;

#define IsTest(x) (push.test == x)

)EOSHADER";

  const std::string comp = common + R"EOSHADER(

shared uvec4 gsmUint4[1024];

struct Output
{
  vec4 vals[1024];
};

layout(binding = 0, std430) buffer outbuftype {
  Output data[COMP_TESTS];
} outbuf;

layout(local_size_x = GROUP_SIZE_X, local_size_y = GROUP_SIZE_Y, local_size_z = 1) in;

vec4 funcD(uint id)
{
  return vec4(subgroupAdd(id/2));
}

vec4 nestedFunc(uint id)
{
  vec4 ret = funcD(id/3);
  ret.w = subgroupAdd(id);
  return ret;
}

vec4 funcA(uint id)
{
   return nestedFunc(id*2);
}

vec4 funcB(uint id)
{
   return nestedFunc(id*4);
}

vec4 funcTest(uint id)
{
  if ((id % 2) == 0)
  {
    return vec4(0);
  }
  else
  {
    float value = subgroupAdd(id);
    if (id < 10)
    {
      return vec4(value);
    }
    value += subgroupAdd(id/2);
    return vec4(value);
  }
}

void SetOutput(vec4 data)
{
  outbuf.data[push.test].vals[gl_LocalInvocationID.y * GROUP_SIZE_X + gl_LocalInvocationID.x] = data;
}

void main()
{
  vec4 data = vec4(0);
  uint id = gl_SubgroupInvocationID;
  gsmUint4[id] = id.xxxx;
  SetOutput(data);

  if(IsTest(0))
  {
    data.x = id;
    barrier();
  }
  else if(IsTest(1))
  {
    data.x = subgroupAdd(id);
    barrier();
  }
  else if(IsTest(2))
  {
    // Diverged threads which reconverge 
    if (id < 10)
    {
        // active threads 0-9
        data.x = subgroupAdd(id);

        if ((id % 2) == 0)
          data.y = subgroupAdd(id);
        else
          data.y = subgroupAdd(id);

        data.x += subgroupAdd(id);
    }
    else
    {
        // active threads 10...
        data.x = subgroupAdd(id);
    }
    data.y = subgroupAdd(id);
    barrier();
  }
  else if(IsTest(3))
  {
    // Converged threads calling a function 
    data = funcTest(id);
    data.y = subgroupAdd(id);
    barrier();
  }
  else if(IsTest(4))
  {
    // Converged threads calling a function which has a nested function call in it
    data = nestedFunc(id);
    data.y = subgroupAdd(id);
    barrier();
  }
  else if(IsTest(5))
  {
    // Diverged threads calling the same function
    if (id < 10)
    {
      data = funcD(id);
    }
    else
    {
      data = funcD(id);
    }
    data.y = subgroupAdd(id);
    barrier();
  }
  else if(IsTest(6))
  {
    // Diverged threads calling the same function which has a nested function call in it
    if (id < 10)
    {
      data = funcA(id);
    }
    else
    {
      data = funcB(id);
    }
    data.y = subgroupAdd(id);
    barrier();
  }
  else if(IsTest(7))
  {
    // Diverged threads which early exit
    if (id < 10)
    {
      data.x = subgroupAdd(id+10);
      SetOutput(data);
      return;
    }
    data.x = subgroupAdd(id);
  }
  else if(IsTest(8))
  {
     // Loops with different number of iterations per thread
    for (uint i = 0; i < id; i++)
    {
      data.x += subgroupAdd(id);
    }
    barrier();
  }
  else if(IsTest(9))
  {
    // Query functions : unit tests
    data.x = float(gl_SubgroupSize);
    data.y = float(gl_SubgroupInvocationID);
    data.z = float(subgroupElect());

    barrier();
  }

  SetOutput(data);
}

)EOSHADER";

  VkSubgroupFeatureFlags ops = 0;

  void Prepare(int argc, char **argv)
  {
    VulkanGraphicsTest::Prepare(argc, argv);

    if(!Avail.empty())
      return;

    if(devVersion < VK_API_VERSION_1_1)
      Avail = "Vulkan device version isn't 1.1";

    static VkPhysicalDeviceSubgroupProperties subProps = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES,
    };

    getPhysProperties2(&subProps);

    if(subProps.subgroupSize < 16)
      Avail = "Subgroup size is less than 16";

    // require at least a few ops so we only have a few conditional compilations
    const VkSubgroupFeatureFlags requiredOps =
        VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT |
        VK_SUBGROUP_FEATURE_ARITHMETIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT;

    ops = subProps.supportedOperations;

    if((subProps.supportedOperations & requiredOps) != requiredOps)
      Avail = "Missing ops support";

    if((subProps.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) == 0)
      Avail = "Missing compute subgroup support";
  }

  int main()
  {
    // initialise, create window, create context, etc
    if(!Init())
      return 3;

    VkDescriptorSetLayout setlayout = createDescriptorSetLayout(vkh::DescriptorSetLayoutCreateInfo({
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
    }));

    VkPipelineLayout layout = createPipelineLayout(vkh::PipelineLayoutCreateInfo(
        {setlayout}, {vkh::PushConstantRange(VK_SHADER_STAGE_ALL, 0, 4)}));

    std::map<std::string, std::string> macros;

    int numCompTests = 0;

    size_t pos = 0;
    while(pos != std::string::npos)
    {
      pos = comp.find("IsTest(", pos);
      if(pos == std::string::npos)
        break;
      pos += sizeof("IsTest(") - 1;
      numCompTests = std::max(numCompTests, atoi(comp.c_str() + pos) + 1);
    }

    if(ops & VK_SUBGROUP_FEATURE_SHUFFLE_BIT)
      macros["FEAT_SHUFFLE"] = "1";
    else
      macros["FEAT_SHUFFLE"] = "0";
    if(ops & VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT)
      macros["FEAT_SHUFFLE_RELATIVE"] = "1";
    else
      macros["FEAT_SHUFFLE_RELATIVE"] = "0";
    if(ops & VK_SUBGROUP_FEATURE_CLUSTERED_BIT)
      macros["FEAT_CLUSTERED"] = "1";
    else
      macros["FEAT_CLUSTERED"] = "0";
    if(ops & VK_SUBGROUP_FEATURE_QUAD_BIT)
      macros["FEAT_QUAD"] = "1";
    else
      macros["FEAT_QUAD"] = "0";
    if(ops & VK_SUBGROUP_FEATURE_ROTATE_BIT_KHR)
      macros["FEAT_ROTATE"] = "1";
    else
      macros["FEAT_ROTATE"] = "0";
    if(ops & VK_SUBGROUP_FEATURE_ROTATE_CLUSTERED_BIT_KHR)
      macros["FEAT_ROTATE_CLUSTERED"] = "1";
    else
      macros["FEAT_ROTATE_CLUSTERED"] = "0";

    std::string comppipe_name[1];
    VkPipeline comppipe[1];
    uint32_t countPipes = 0;

    macros["COMP_TESTS"] = fmt::format("{}", numCompTests);

    macros["GROUP_SIZE_X"] = "70";
    macros["GROUP_SIZE_Y"] = "1";
    comppipe_name[countPipes] = "70x1";
    comppipe[countPipes] = createComputePipeline(vkh::ComputePipelineCreateInfo(
        layout, CompileShaderModule(comp, ShaderLang::glsl, ShaderStage::comp, "main", macros,
                                    SPIRVTarget::vulkan11)));
    ++countPipes;

    AllocatedBuffer bufout(
        this,
        vkh::BufferCreateInfo(sizeof(Vec4f) * 1024 * numCompTests,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT),
        VmaAllocationCreateInfo({0, VMA_MEMORY_USAGE_CPU_TO_GPU}));

    setName(bufout.buffer, "bufout");

    VkDescriptorSet set = allocateDescriptorSet(setlayout);

    vkh::updateDescriptorSets(
        device, {vkh::WriteDescriptorSet(set, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                         {vkh::DescriptorBufferInfo(bufout.buffer)})});

    while(Running())
    {
      VkCommandBuffer cmd = GetCommandBuffer();

      vkBeginCommandBuffer(cmd, vkh::CommandBufferBeginInfo());

      VkImage swapimg =
          StartUsingBackbuffer(cmd, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);

      vkh::cmdClearImage(cmd, swapimg, vkh::ClearColorValue(0.2f, 0.2f, 0.2f, 1.0f));

      pushMarker(cmd, "Compute Tests");

      for(size_t p = 0; p < countPipes; p++)
      {
        vkh::cmdPipelineBarrier(
            cmd, {},
            {vkh::BufferMemoryBarrier(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                                      bufout.buffer, 0, sizeof(Vec4f) * 1024 * numCompTests)});

        vkCmdFillBuffer(cmd, bufout.buffer, 0, sizeof(Vec4f) * 1024 * numCompTests, 0);

        vkh::cmdPipelineBarrier(
            cmd, {},
            {vkh::BufferMemoryBarrier(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                                      bufout.buffer, 0, sizeof(Vec4f) * 1024 * numCompTests)});

        pushMarker(cmd, comppipe_name[p]);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, comppipe[p]);
        vkh::cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, {set}, {});

        for(int i = 0; i < numCompTests; i++)
        {
          vkh::cmdPushConstants(cmd, layout, i);
          vkCmdDispatch(cmd, 2, 1, 1);
        }

        popMarker(cmd);
      }

      popMarker(cmd);

      FinishUsingBackbuffer(cmd, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);

      vkEndCommandBuffer(cmd);

      SubmitAndPresent({cmd});
    }

    return 0;
  }
};

REGISTER_TEST();
