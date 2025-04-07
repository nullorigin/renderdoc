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

RD_TEST(VK_Subgroup_Zoo, VulkanGraphicsTest)
{
  static constexpr const char *Description =
      "Test of behaviour around subgroup operations in shaders.";

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

  const std::string vertex = common + R"EOSHADER(

layout(location = 0) out vec4 vertdata;

void main()
{
  vec2 positions[] = {
    vec2(-1.0f,  1.0f),
    vec2( 1.0f,  1.0f),
    vec2(-1.0f, -1.0f),
    vec2( 1.0f, -1.0f),
  };

  float scale = 1.0f;
  if(IsTest(2))
    scale = 0.2f;

  gl_Position = vec4(positions[gl_VertexIndex]*vec2(scale,scale), 0, 1);

  vertdata = vec4(0);

  if(IsTest(0))
    vertdata = vec4(gl_SubgroupInvocationID, 0, 0, 1);
  else if(IsTest(3))
    vertdata = vec4(subgroupAdd(gl_SubgroupInvocationID), 0, 0, 0);
}

)EOSHADER";

  const std::string pixel = common + R"EOSHADER(

layout(location = 0) in vec4 vertdata;

layout(location = 0, index = 0) out vec4 Color;

void main()
{
  uint subgroupId = gl_SubgroupInvocationID;

  vec4 fragdata = vec4(0);

  if(IsTest(1) || IsTest(2))
  {
    fragdata = vec4(subgroupId, 0, 0, 1);
  }
  else if(IsTest(4))
  {
    fragdata = vec4(subgroupAdd(subgroupId), 0, 0, 0);
  }
  else if(IsTest(5))
  {
    // subgroupQuadBroadcast : unit tests
    fragdata.x = float(subgroupQuadBroadcast(subgroupId, 0));
    fragdata.y = float(subgroupQuadBroadcast(subgroupId, 1));
    fragdata.z = float(subgroupQuadBroadcast(subgroupId, 2));
    fragdata.w = float(subgroupQuadBroadcast(subgroupId, 3));
  }
  else if(IsTest(6))
  {
    // subgroupQuadSwapDiagonal, subgroupQuadSwapHorizontal, subgroupQuadSwapVertical : unit tests
    fragdata.x = float(subgroupQuadSwapDiagonal(subgroupId));
    fragdata.y = float(subgroupQuadSwapHorizontal(subgroupId));
    fragdata.z = float(subgroupQuadSwapVertical(subgroupId));
    fragdata.w = subgroupQuadBroadcast(fragdata.x, 2);
  }

  Color = vertdata + fragdata;
}

)EOSHADER";

  const std::string comp = common + R"EOSHADER(

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

void SetOuput(vec4 data)
{
  outbuf.data[push.test].vals[gl_LocalInvocationID.y * GROUP_SIZE_X + gl_LocalInvocationID.x] = data;
}
void main()
{
  vec4 data = vec4(0);
  uint id = gl_SubgroupInvocationID;
  SetOuput(data);

  if(IsTest(0))
  {
    data.x = id;
  }
  else if(IsTest(1))
  {
    data.x = subgroupAdd(id);
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
  }
  else if(IsTest(3))
  {
    // Converged threads calling a function 
    data = funcTest(id);
    data.y = subgroupAdd(id);
  }
  else if(IsTest(4))
  {
    // Converged threads calling a function which has a nested function call in it
    data = nestedFunc(id);
    data.y = subgroupAdd(id);
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
  }
  else if(IsTest(7))
  {
    // Diverged threads which early exit
    if (id < 10)
    {
      data.x = subgroupAdd(id+10);
      SetOuput(data);
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
  }
  else if(IsTest(9))
  {
    // Query functions : unit tests
    data.x = float(gl_SubgroupSize);
    data.y = float(gl_SubgroupInvocationID);
    data.z = float(subgroupElect());
  }
  else if(IsTest(10))
  {
    // Vote functions : unit tests
    data.x = float(subgroupAny(id*2 > id+10));
    data.y = float(subgroupAll(id < gl_SubgroupSize));
    if (id > 10)
    {
      data.z = float(subgroupAll(id > 10));
      uvec4 ballot = subgroupBallot(id > 20);
      data.w = bitCount(ballot.x) + bitCount(ballot.y) + bitCount(ballot.z) + bitCount(ballot.w);
    }
    else
    {
      data.z = float(subgroupAll(id > 3));
      uvec4 ballot = subgroupBallot(id > 4);
      data.w = bitCount(ballot.x) + bitCount(ballot.y) + bitCount(ballot.z) + bitCount(ballot.w);
    }
  }
  else if(IsTest(11))
  {
    // Broadcast functions : unit tests
    if (id >= 2 && id <= 20)
    {
      data.x = subgroupBroadcastFirst(id);
      data.y = subgroupBroadcast(id, 5);
      data.z = subgroupShuffle(id, id);
      data.w = subgroupShuffle(data.x, 2+id%3);
    }
  }
  else if(IsTest(12))
  {
    // Scan and Prefix functions : unit tests
    if (id >= 2 && id <= 20)
    {
      uvec4 bits = subgroupBallot(id > 4);
      data.x = subgroupBallotExclusiveBitCount(bits);
      bits = subgroupBallot(id > 10);
      data.y = subgroupBallotExclusiveBitCount(bits);
      data.z = subgroupExclusiveAdd(data.x);
      data.w = subgroupExclusiveMul(1 + data.y);
    }
  }
  SetOuput(data);
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

    // require all stages for simplicity
    if((subProps.supportedStages & VK_SHADER_STAGE_VERTEX_BIT) == 0)
      Avail = "Missing vertex subgroup support";

    if((subProps.supportedStages & VK_SHADER_STAGE_FRAGMENT_BIT) == 0)
      Avail = "Missing pixel subgroup support";

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

    const uint32_t imgDim = 128;

    AllocatedImage img(
        this,
        vkh::ImageCreateInfo(imgDim, imgDim, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT),
        VmaAllocationCreateInfo({0, VMA_MEMORY_USAGE_GPU_ONLY}));

    VkImageView imgview = createImageView(
        vkh::ImageViewCreateInfo(img.image, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT));

    vkh::RenderPassCreator renderPassCreateInfo;

    renderPassCreateInfo.attachments.push_back(
        vkh::AttachmentDescription(VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_GENERAL, VK_ATTACHMENT_LOAD_OP_CLEAR));

    renderPassCreateInfo.addSubpass({VkAttachmentReference({0, VK_IMAGE_LAYOUT_GENERAL})});

    VkRenderPass renderPass = createRenderPass(renderPassCreateInfo);

    VkFramebuffer framebuffer =
        createFramebuffer(vkh::FramebufferCreateInfo(renderPass, {imgview}, {imgDim, imgDim}));

    vkh::GraphicsPipelineCreateInfo pipeCreateInfo;

    pipeCreateInfo.renderPass = renderPass;
    pipeCreateInfo.layout = layout;
    pipeCreateInfo.inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    std::map<std::string, std::string> macros;

    int vertTests = 0, pixTests = 0;
    int numCompTests = 0;

    {
      size_t pos = 0;
      while(pos != std::string::npos)
      {
        pos = pixel.find("IsTest(", pos);
        if(pos == std::string::npos)
          break;
        pos += sizeof("IsTest(") - 1;
        pixTests = std::max(pixTests, atoi(pixel.c_str() + pos) + 1);
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
        numCompTests = std::max(numCompTests, atoi(comp.c_str() + pos) + 1);
      }
    }

    const uint32_t numGraphicsTests = std::max(vertTests, pixTests);

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

    pipeCreateInfo.stages = {
        CompileShaderModule(vertex, ShaderLang::glsl, ShaderStage::vert, "main", macros,
                            SPIRVTarget::vulkan11),
        CompileShaderModule(pixel, ShaderLang::glsl, ShaderStage::frag, "main", macros,
                            SPIRVTarget::vulkan11),
    };

    VkPipeline pipe = createGraphicsPipeline(pipeCreateInfo);

    std::string comppipe_name[4];
    VkPipeline comppipe[4];

    macros["COMP_TESTS"] = fmt::format("{}", numCompTests);

    macros["GROUP_SIZE_X"] = "256";
    macros["GROUP_SIZE_Y"] = "1";
    comppipe_name[0] = "256x1";
    comppipe[0] = createComputePipeline(vkh::ComputePipelineCreateInfo(
        layout, CompileShaderModule(comp, ShaderLang::glsl, ShaderStage::comp, "main", macros,
                                    SPIRVTarget::vulkan11)));

    macros["GROUP_SIZE_X"] = "128";
    macros["GROUP_SIZE_Y"] = "2";
    comppipe_name[1] = "128x2";
    comppipe[1] = createComputePipeline(vkh::ComputePipelineCreateInfo(
        layout, CompileShaderModule(comp, ShaderLang::glsl, ShaderStage::comp, "main", macros,
                                    SPIRVTarget::vulkan11)));

    macros["GROUP_SIZE_X"] = "8";
    macros["GROUP_SIZE_Y"] = "128";
    comppipe_name[2] = "8x128";
    comppipe[2] = createComputePipeline(vkh::ComputePipelineCreateInfo(
        layout, CompileShaderModule(comp, ShaderLang::glsl, ShaderStage::comp, "main", macros,
                                    SPIRVTarget::vulkan11)));

    macros["GROUP_SIZE_X"] = "150";
    macros["GROUP_SIZE_Y"] = "1";
    comppipe_name[3] = "150x1";
    comppipe[3] = createComputePipeline(vkh::ComputePipelineCreateInfo(
        layout, CompileShaderModule(comp, ShaderLang::glsl, ShaderStage::comp, "main", macros,
                                    SPIRVTarget::vulkan11)));

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

      vkh::cmdPipelineBarrier(
          cmd, {vkh::ImageMemoryBarrier(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                        VK_IMAGE_LAYOUT_GENERAL, img.image)});

      vkh::cmdClearImage(cmd, swapimg, vkh::ClearColorValue(0.2f, 0.2f, 0.2f, 1.0f));

      vkh::cmdPipelineBarrier(
          cmd,
          {vkh::ImageMemoryBarrier(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, img.image)});

      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);

      VkViewport v = {};
      v.maxDepth = 1.0f;
      v.width = v.height = (float)imgDim;

      VkRect2D s = {};
      s.extent.width = s.extent.height = imgDim;

      vkCmdSetViewport(cmd, 0, 1, &v);
      vkCmdSetScissor(cmd, 0, 1, &s);

      // separate render passes with a fat barrier before each to avoid subgroups crossing draws

      pushMarker(cmd, "Graphics Tests");

      for(uint32_t i = 0; i < numGraphicsTests; i++)
      {
        vkh::cmdPipelineBarrier(
            cmd, {}, {},
            {vkh::MemoryBarrier(VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT)});

        vkCmdBeginRenderPass(
            cmd,
            vkh::RenderPassBeginInfo(renderPass, framebuffer, s,
                                     {vkh::ClearValue(123456.0f, 789.0f, 101112.0f, 0.0f)}),
            VK_SUBPASS_CONTENTS_INLINE);

        vkh::cmdPushConstants(cmd, layout, i);
        vkCmdDraw(cmd, 4, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
      }

      popMarker(cmd);

      pushMarker(cmd, "Compute Tests");

      for(size_t p = 0; p < ARRAY_COUNT(comppipe); p++)
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
          vkCmdDispatch(cmd, 1, 1, 1);
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
