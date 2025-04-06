/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2024-2025 Baldur Karlsson
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

#include "dxil_controlflow.h"
#include "common/formatting.h"
#include "core/settings.h"

RDOC_EXTERN_CONFIG(bool, D3D12_DXILShaderDebugger_Logging);

/*

Inputs are links of blocks : from -> to (can be forwards or backwards links)
Output is a list of uniform control flow blocks which all possible flows go through (not diverged)
and are not in a loop

The algorithm is:

1. Setup
  * Compute all possible known blocks.
  * For each block generate a list of "to" blocks from the input links
  * Any block without links in the input are set to link to the end sentinel (PATH_END)

2. Generate all possible paths
  * Paths can terminate at the end block (PATH_END)
  * Paths can also terminate at a block before the end, if that block has had all its possible paths
already computed

3. Find DivergentBlocks
  * defined by blocks with more than one exit link

4. Find Uniform Blocks
  * Generate a list of path indexes for each block in the paths
  * Generate a list of loop blocks which are blocks which appear in any path starting from the block
  * Generate a list of all paths blocks which are blocks which appear in all possible paths
    * all paths includes walking any paths linked at the end node of the path being walked
  * uniform blocks are defined to be non-loop blocks which are in all paths

5. Find potential convergent blocks
  * defined to be blocks with more than one link into it and blocks which are directly connected
  to divergent blocks (to handle loop convergence)

6. Find ConvergentBlocks
  * For each divergent block find its convergent block
  * defined to be the first block which is in all possible paths that start from the divergent block
  * loops are not taken when walking the paths
  * this is similar to uniform blocks find convergent blocks starting from the block zero
  * the convergent blocks can be thought of as a local uniform block
  * where local is defined by the graph/paths which contain the divergent block
*/

namespace DXIL
{
void OutputGraph(const char *const name, const ControlFlow *graph)
{
  ControlFlow::BlockArray divergentBlocks = graph->GetDivergentBlocks();
  rdcarray<ConvergentBlockData> convergentBlocks = graph->GetConvergentBlocks();

  rdcstr fname = StringFormat::Fmt("%s.txt", name);
  FILE *f = FileIO::fopen(fname.c_str(), FileIO::WriteText);
  rdcstr line = StringFormat::Fmt("digraph %s {\n", name);
  fprintf(f, line.c_str());
  for(uint32_t from : graph->m_Blocks)
  {
    line = StringFormat::Fmt("%u", from);
    if(divergentBlocks.contains(from))
      line += StringFormat::Fmt(" [shape=diamond color=red]");
    line += ";\n";

    for(uint32_t to : graph->m_BlockOutLinks[from])
      line += StringFormat::Fmt("%u -> %u [weight=1];\n", from, to);

    fprintf(f, line.c_str());
  }
  for(ConvergentBlockData data : convergentBlocks)
  {
    line = StringFormat::Fmt("%u -> %u [weight=0 style=dashed color=blue constraint=false];\n",
                             data.first, data.second, data.first, data.second);

    fprintf(f, line.c_str());
  }
  fprintf(f, "}\n");
  FileIO::fclose(f);
}

bool ControlFlow::IsBlockConnected(const size_t pathsType, uint32_t from, uint32_t to) const
{
  const rdcarray<BlockPath> &paths = m_PathSets[pathsType];
  for(uint32_t pathIdx = 0; pathIdx < paths.size(); ++pathIdx)
  {
    m_CheckedPaths.clear();
    m_CheckedPaths.resize(paths.size());
    for(size_t i = 0; i < m_CheckedPaths.size(); ++i)
      m_CheckedPaths[i] = false;
    int32_t startIdx = -1;
    for(uint32_t i = 0; i < paths[pathIdx].size() - 1; ++i)
    {
      if(paths[pathIdx][i] == from)
      {
        startIdx = i;
        break;
      }
    }
    // BlockInAnyPath will also check all paths linked to from the end node of the path
    if(startIdx != -1 && (BlockInAnyPath(pathsType, to, pathIdx, startIdx + 1, 0) != -1))
    {
      return true;
    }
  }
  return false;
}

bool ControlFlow::TraceBlockFlow(const size_t pathsType, const uint32_t from, BlockPath &path)
{
  rdcarray<BlockPath> &paths = m_PathSets[pathsType];
  if(from == PATH_END)
  {
    paths.push_back(path);
    return true;
  }
  if(m_BlockOutLinks[from].empty())
  {
    paths.push_back(path);
    return true;
  }
  if(m_TracedBlocks[from])
  {
    paths.push_back(path);
    return true;
  }
  m_TracedBlocks[from] = true;
  BlockPath newPath = path;
  const rdcarray<uint32_t> &gotos = m_BlockOutLinks.at(from);
  for(uint32_t to : gotos)
  {
    // Ignore loops
    if((pathsType == PathType::NoLoops) && path.contains(to))
      continue;
    newPath.push_back(to);
    if(TraceBlockFlow(pathsType, to, newPath))
      newPath = path;
  }
  return true;
}

int32_t ControlFlow::BlockInAnyPath(const size_t pathsType, uint32_t block, uint32_t pathIdx,
                                    int32_t startIdx, int32_t steps) const
{
  const rdcarray<BlockPath> &paths = m_PathSets[pathsType];
  const BlockPath &path = paths[pathIdx];
  if(path.size() == 0)
    return -1;

  // Check the current path
  for(uint32_t i = startIdx; i < path.size(); ++i)
  {
    if(block == path[i])
      return steps;
    ++steps;
  }

  uint32_t endNode = path[path.size() - 1];
  if(endNode == PATH_END)
    return -1;

  // Check any paths linked to by the end node of the current path
  const rdcarray<rdcarray<uint32_t>> &blockPathLinks = m_BlockPathLinks[pathsType];
  const rdcarray<uint32_t> &childPathsToCheck = blockPathLinks.at(endNode);
  for(uint32_t childPathIdx : childPathsToCheck)
  {
    if(m_CheckedPaths[childPathIdx])
      continue;

    m_CheckedPaths[childPathIdx] = true;
    const BlockPath &childPath = paths[childPathIdx];
    int32_t childPartStartIdx = -1;
    int32_t newSteps = steps;
    for(uint32_t i = 0; i < childPath.size(); ++i)
    {
      if(childPath[i] == endNode)
      {
        childPartStartIdx = i;
        break;
      }
      ++newSteps;
    }
    if(childPartStartIdx != -1)
    {
      newSteps = BlockInAnyPath(pathsType, block, childPathIdx, childPartStartIdx, newSteps);
      if(newSteps != -1)
        return newSteps;
    }
  }
  return -1;
}

bool ControlFlow::BlockInAllPaths(const size_t pathsType, uint32_t block, uint32_t pathIdx,
                                  int32_t startIdx) const
{
  const rdcarray<BlockPath> &paths = m_PathSets[pathsType];
  const BlockPath &path = paths[pathIdx];
  if(path.size() == 0)
    return false;

  // Check the current path
  for(uint32_t i = startIdx; i < path.size(); ++i)
  {
    if(block == path[i])
      return true;
  }

  uint32_t endNode = path[path.size() - 1];
  if(endNode == block)
    return true;

  m_CheckedPaths[pathIdx] = true;
  if(endNode == PATH_END)
    return false;

  // Check any paths linked to by the end node of the current path
  const rdcarray<rdcarray<uint32_t>> &blockPathLinks = m_BlockPathLinks[pathsType];
  const rdcarray<uint32_t> &childPathsToCheck = blockPathLinks.at(endNode);
  for(uint32_t childPathIdx : childPathsToCheck)
  {
    if(m_CheckedPaths[childPathIdx])
      continue;

    m_CheckedPaths[childPathIdx] = true;
    int32_t childPartStartIdx = paths[childPathIdx].indexOf(endNode);
    RDCASSERTNOTEQUAL(childPartStartIdx, -1);
    if(!BlockInAllPaths(pathsType, block, childPathIdx, childPartStartIdx + 1))
      return false;
  }
  return true;
}

void ControlFlow::Construct(const rdcarray<rdcpair<uint32_t, uint32_t>> &links)
{
  m_Blocks.clear();
  m_BlockOutLinks.clear();
  m_BlockInLinks.clear();
  m_BlockPathLinks[PathType::IncLoops].clear();
  m_BlockPathLinks[PathType::NoLoops].clear();
  m_TracedBlocks.clear();
  m_CheckedPaths.clear();
  m_PathSets[PathType::IncLoops].clear();
  m_PathSets[PathType::NoLoops].clear();

  m_UniformBlocks.clear();
  m_LoopBlocks.clear();
  m_DivergentBlocks.clear();
  m_ConvergentBlocks.clear();

  // 1. Setup
  // Compute all possible known blocks
  uint32_t maxBlockIndex = 0;
  for(const auto &link : links)
  {
    uint32_t from = link.first;
    uint32_t to = link.second;
    m_Blocks.insert(from);
    m_Blocks.insert(to);
    maxBlockIndex = RDCMAX(maxBlockIndex, from);
    maxBlockIndex = RDCMAX(maxBlockIndex, to);
  }
  PATH_END = maxBlockIndex + 1;
  maxBlockIndex += 2;
  m_TracedBlocks.resize(maxBlockIndex);
  m_BlockOutLinks.resize(maxBlockIndex);
  m_BlockInLinks.resize(maxBlockIndex);
  m_BlockPathLinks[PathType::IncLoops].resize(maxBlockIndex);
  m_BlockPathLinks[PathType::NoLoops].resize(maxBlockIndex);

  // For each block a list of "to" and "from" blocks
  for(const auto &link : links)
  {
    uint32_t from = link.first;
    uint32_t to = link.second;
    m_BlockOutLinks[from].push_back(to);
    m_BlockInLinks[to].push_back(from);
  }

  // Any block without links in the input are set to link to the end sentinel (PATH_END)
  for(uint32_t b : m_Blocks)
  {
    if(m_BlockOutLinks[b].empty())
    {
      m_BlockOutLinks[b].push_back(PATH_END);
      m_BlockInLinks[PATH_END].push_back(b);
    }
  }

  // 2. Generate all possible paths

  // Paths can terminate at the end block (PATH_END)
  // Paths can also terminate at a block before the end,
  // if that block has had all its possible paths already computed

  // Compute separate sets of paths
  // * Paths which can include loops
  // * Paths which do not include loops
  for(size_t pathType = 0; pathType < PathType::Count; ++pathType)
  {
    for(size_t i = 0; i < maxBlockIndex; ++i)
      m_TracedBlocks[i] = false;
    for(size_t i = 0; i < m_BlockOutLinks.size(); ++i)
    {
      uint32_t from = (uint32_t)i;
      if(m_BlockOutLinks[i].empty())
        continue;
      if(m_TracedBlocks[from])
        continue;
      BlockPath path;
      path.push_back(from);
      TraceBlockFlow(pathType, from, path);
    }
  }

  // 3. Find DivergentBlocks
  //  * defined by blocks with more than one exit link
  for(uint32_t block : m_Blocks)
  {
    if(m_BlockOutLinks[block].size() > 1)
      m_DivergentBlocks.push_back(block);
  }

  // 4. Find Uniform Blocks
  for(size_t pathType = 0; pathType < PathType::Count; ++pathType)
  {
    for(uint32_t b : m_Blocks)
    {
      m_BlockPathLinks[pathType][b].clear();
    }

    // Generate a list of path indexes for each block in the paths
    rdcarray<BlockPath> &paths = m_PathSets[pathType];
    rdcarray<rdcarray<uint32_t>> &blockPathLinks = m_BlockPathLinks[pathType];
    for(uint32_t pathIdx = 0; pathIdx < paths.size(); ++pathIdx)
    {
      for(uint32_t block : paths[pathIdx])
      {
        if(block == PATH_END)
          break;
        blockPathLinks[block].push_back(pathIdx);
      }
    }

    // For each path that does not end with PATH_END
    // append the child path nodes which only have a single path and are not already in the path
    // This is an optimisation to reduce the amount of recursion required when tracing paths
    // In particular to help the speed of BlockInAnyPath()
    for(uint32_t p = 0; p < paths.size(); ++p)
    {
      BlockPath &path = paths[p];
      uint32_t endNode = path[path.size() - 1];
      while(endNode != PATH_END)
      {
        const rdcarray<uint32_t> &gotos = m_BlockOutLinks.at(endNode);
        if(gotos.size() > 1)
          break;
        endNode = gotos[0];
        if(path.contains(endNode))
          break;
        path.push_back(endNode);
      }
    }
  }

  // Generate the connections 2D map for quick lookup of forward connections
  // IsBlock B in any path ahead of Block A
  m_Connections.resize(maxBlockIndex);
  for(uint32_t from = 0; from < maxBlockIndex; ++from)
  {
    m_Connections[from].resize(maxBlockIndex);
    for(uint32_t to = 0; to < maxBlockIndex; ++to)
      m_Connections[from][to] = ConnectionState::Unknown;
  }

  const rdcarray<BlockPath> &pathsNoLoops = m_PathSets[PathType::NoLoops];

  for(uint32_t p = 0; p < pathsNoLoops.size(); ++p)
  {
    const BlockPath &path = pathsNoLoops[p];
    for(size_t i = 0; i < path.size() - 1; ++i)
    {
      uint32_t from = path[i];
      for(size_t j = i + 1; j < path.size(); ++j)
      {
        uint32_t to = path[j];
        if(to == PATH_END)
          break;
        m_Connections[from][to] = ConnectionState::Connected;
      }
    }
  }

  // A loop block is defined by any block which appears in any path (including loops) starting from the block
  for(uint32_t block : m_Blocks)
  {
    if(IsBlockConnected(PathType::IncLoops, block, block))
      m_LoopBlocks.push_back(block);
  }

  // An all paths block is defined by any block which appears in all paths
  // all paths includes walking any paths linked at the end node of the path being walked
  uint32_t globalDivergenceStart = 0;
  if(!m_Blocks.empty())
  {
    m_UniformBlocks.push_back(globalDivergenceStart);

    rdcarray<uint32_t> pathsToCheck;
    for(uint32_t pathIdx = 0; pathIdx < pathsNoLoops.size(); ++pathIdx)
    {
      if(pathsNoLoops[pathIdx].contains(globalDivergenceStart))
        pathsToCheck.push_back(pathIdx);
    }
    rdcarray<uint32_t> potentialUniformBlocks;
    // We want all uniform blocks connected to the global start block
    // Not just the first convergence points
    for(uint32_t block : m_Blocks)
    {
      if(block == globalDivergenceStart)
        continue;
      // Ignore blocks not connected to the divergent block
      if(!IsForwardConnection(globalDivergenceStart, block))
        continue;
      // Ignore loop blocks
      if(m_LoopBlocks.contains(block))
        continue;

      bool uniform = true;
      for(uint32_t pathIdx : pathsToCheck)
      {
        int32_t startIdx = pathsNoLoops[pathIdx].indexOf(globalDivergenceStart);
        RDCASSERTNOTEQUAL(startIdx, -1);

        m_CheckedPaths.clear();
        m_CheckedPaths.resize(pathsNoLoops.size());
        for(size_t i = 0; i < m_CheckedPaths.size(); ++i)
          m_CheckedPaths[i] = false;
        // BlockInAllPaths will also check all paths linked to from the end node of the path
        if(!BlockInAllPaths(PathType::NoLoops, block, pathIdx, startIdx + 1))
        {
          uniform = false;
          break;
        }
      }
      if(uniform)
        m_UniformBlocks.push_back(block);
    }
  }

  // 5. Find potential convergent blocks
  // * defined to be blocks with more than one link into it and blocks which are directly connected
  // to divergent blocks (to handle loop convergence)
  rdcarray<uint32_t> potentialConvergentBlocks;
  for(uint32_t start : m_DivergentBlocks)
  {
    for(uint32_t block : m_BlockOutLinks[start])
    {
      if(!potentialConvergentBlocks.contains(block))
        potentialConvergentBlocks.push_back(block);
    }
  }
  for(uint32_t block : m_Blocks)
  {
    if(m_BlockInLinks[block].size() > 1)
    {
      if(!potentialConvergentBlocks.contains(block))
        potentialConvergentBlocks.push_back(block);
    }
  }

  // 6. Find ConvergentBlocks
  //  * For each divergent block find its convergent block
  //  * defined to be the first block which is in all possible paths that start from the divergent block
  //  * loops are not taken when walking the paths
  //  * this is similar to uniform blocks find convergent blocks starting from the block zero
  //  * the convergent blocks can be thought of as a local uniform block
  //  * where local is defined by the graph/paths which contain the divergent block
  m_ConvergentBlocks.clear();

  for(uint32_t start : m_DivergentBlocks)
  {
    rdcarray<uint32_t> pathsToCheck;
    for(uint32_t pathIdx = 0; pathIdx < pathsNoLoops.size(); ++pathIdx)
    {
      if(pathsNoLoops[pathIdx].contains(start))
        pathsToCheck.push_back(pathIdx);
    }
    rdcarray<uint32_t> localUniformBlocks;
    for(uint32_t block : potentialConvergentBlocks)
    {
      if(block == start)
        continue;
      // Ignore blocks not connected to the divergent block
      if(!IsForwardConnection(start, block))
        continue;

      bool uniform = true;
      for(uint32_t pathIdx : pathsToCheck)
      {
        int32_t startIdx = pathsNoLoops[pathIdx].indexOf(start);
        RDCASSERTNOTEQUAL(startIdx, -1);

        m_CheckedPaths.clear();
        m_CheckedPaths.resize(pathsNoLoops.size());
        for(size_t i = 0; i < m_CheckedPaths.size(); ++i)
          m_CheckedPaths[i] = false;
        // BlockInAllPaths will also check all paths linked to from the end node of the path
        if(!BlockInAllPaths(PathType::NoLoops, block, pathIdx, startIdx + 1))
        {
          uniform = false;
          break;
        }
      }
      if(uniform)
        localUniformBlocks.push_back(block);
    }
    if(localUniformBlocks.empty())
      RDCERR("Failed to find any locally uniform blocks for divergent block %d", start);

    uint32_t convergentBlock = start;
    // Take the first block which is in all paths
    for(uint32_t blockA : localUniformBlocks)
    {
      uint32_t countConnected = 0;
      bool first = true;
      for(uint32_t blockB : localUniformBlocks)
      {
        if(blockA == blockB)
          continue;
        if(!IsForwardConnection(blockA, blockB))
        {
          first = false;
          break;
        }
        ++countConnected;
      }
      if(first)
      {
        RDCASSERTEQUAL(countConnected, localUniformBlocks.size() - 1);
        convergentBlock = blockA;
        break;
      }
    }
    if(start != convergentBlock)
      m_ConvergentBlocks.push_back({start, convergentBlock});
    else
      RDCERR("Failed to find convergent block for divergent block %d", start);
  }

  if(D3D12_DXILShaderDebugger_Logging())
  {
    RDCLOG("Block Links:");
    for(size_t i = 0; i < m_BlockOutLinks.size(); ++i)
    {
      uint32_t from = (uint32_t)i;
      if(m_BlockOutLinks[i].empty())
        continue;
      for(uint32_t to : m_BlockOutLinks[i])
        RDCLOG("Block:%d->Block:%d", from, to);
    }

    for(size_t i = 0; i < m_BlockInLinks.size(); ++i)
    {
      uint32_t to = (uint32_t)i;
      if(m_BlockInLinks[i].empty())
        continue;
      for(uint32_t from : m_BlockInLinks[i])
        RDCLOG("Block:%d->Block:%d", from, to);
    }

    RDCLOG("Thread Paths Including Loops:");
    rdcstr output = "";
    const rdcarray<BlockPath> &pathsIncLoops = m_PathSets[PathType::IncLoops];
    for(uint32_t pathIdx = 0; pathIdx < pathsIncLoops.size(); ++pathIdx)
    {
      bool start = true;
      for(uint32_t block : pathsIncLoops[pathIdx])
      {
        if(start)
        {
          output = StringFormat::Fmt("%d : %d", pathIdx, block);
          start = false;
        }
        else
        {
          if(block != PATH_END)
            output += " -> " + ToStr(block);
          else
            output += " : END";
        }
      }
      RDCLOG(output.c_str());
    }

    output = "";
    bool needComma = false;
    for(uint32_t block : m_LoopBlocks)
    {
      if(needComma)
        output += ", ";
      output += ToStr(block);
      needComma = true;
    }
    RDCLOG("Blocks in Loops: %s", output.c_str());

    output = "";
    needComma = false;
    for(uint32_t block : m_UniformBlocks)
    {
      if(needComma)
        output += ", ";
      output += ToStr(block);
      needComma = true;
    }
    RDCLOG("Uniform Blocks: %s", output.c_str());

    output = "";
    needComma = false;
    for(uint32_t block : m_DivergentBlocks)
    {
      if(needComma)
        output += ", ";
      output += ToStr(block);
      needComma = true;
    }
    RDCLOG("Divergent Blocks: %s", output.c_str());

    output = "";
    needComma = false;
    for(ConvergentBlockData data : m_ConvergentBlocks)
    {
      if(needComma)
        output += ", ";
      output += StringFormat::Fmt("{ %d -> %d }", data.first, data.second);
      needComma = true;
    }
    RDCLOG("Convergent Blocks: %s", output.c_str());
  }
  // OutputGraph("dxil_cfg", this);

  // Clear temporary data
  m_TracedBlocks.clear();
  m_CheckedPaths.clear();
}

uint32_t ControlFlow::GetNextUniformBlock(uint32_t from) const
{
  // find the closest uniform block when walking the path starting at the from block
  int32_t minSteps = INT_MAX;
  uint32_t bestBlock = from;
  for(uint32_t uniform : m_UniformBlocks)
  {
    const rdcarray<BlockPath> &paths = m_PathSets[PathType::IncLoops];
    for(uint32_t pathIdx = 0; pathIdx < paths.size(); ++pathIdx)
    {
      m_CheckedPaths.clear();
      m_CheckedPaths.resize(paths.size());
      for(size_t i = 0; i < m_CheckedPaths.size(); ++i)
        m_CheckedPaths[i] = false;
      int32_t startIdx = paths[pathIdx].indexOf(from);
      // BlockInAnyPath will also check all paths linked to from the end node of the path
      if(startIdx != -1)
      {
        int32_t steps = BlockInAnyPath(PathType::IncLoops, uniform, pathIdx, startIdx + 1, 0);
        if(steps != -1)
        {
          if(steps < minSteps)
          {
            minSteps = steps;
            bestBlock = uniform;
          }
        }
      }
    }
  }
  return bestBlock;
}

// Include loops
bool ControlFlow::IsForwardConnection(uint32_t from, uint32_t to) const
{
  if(m_Connections[from][to] == ConnectionState::Unknown)
  {
    if(IsBlockConnected(PathType::IncLoops, from, to))
      m_Connections[from][to] = ConnectionState::Connected;
    else
      m_Connections[from][to] = ConnectionState::NotConnected;
  }
  return m_Connections[from][to] == ConnectionState::Connected;
}
};    // namespace DXIL

#if ENABLED(ENABLE_UNIT_TESTS)

#include <limits>
#include "catch/catch.hpp"

using namespace DXIL;

void CheckDivergentBlocks(const rdcarray<ConvergentBlockData> &expectedDivergentBlocks,
                          const rdcarray<uint32_t> &divergentBlocks)
{
  REQUIRE(expectedDivergentBlocks.count() == divergentBlocks.count());
  for(ConvergentBlockData expected : expectedDivergentBlocks)
  {
    bool found = false;
    for(uint32_t actual : divergentBlocks)
    {
      if(expected.first == actual)
      {
        found = true;
      }
    }
    REQUIRE(found);
  }
}

void CheckConvergentBlocks(const rdcarray<ConvergentBlockData> &expectedConvergentBlocks,
                           const rdcarray<ConvergentBlockData> &convergentBlocks)
{
  REQUIRE(expectedConvergentBlocks.count() == convergentBlocks.count());
  for(ConvergentBlockData expected : expectedConvergentBlocks)
  {
    bool found = false;
    for(ConvergentBlockData actual : convergentBlocks)
    {
      if(expected.first == actual.first)
      {
        found = true;
        REQUIRE(expected.second == actual.second);
      }
    }
    REQUIRE(found);
  }
}

TEST_CASE("DXIL Control Flow", "[dxil][controlflow]")
{
  SECTION("FindUniformBlocks")
  {
    ControlFlow controlFlow;
    rdcarray<uint32_t> uniformBlocks;
    rdcarray<uint32_t> loopBlocks;
    SECTION("Degenerate Case")
    {
      // Degenerate case
      rdcarray<BlockLink> inputs;
      controlFlow.Construct(inputs);
      uniformBlocks = controlFlow.GetUniformBlocks();
      REQUIRE(0 == uniformBlocks.count());
      loopBlocks = controlFlow.GetLoopBlocks();
      REQUIRE(0 == loopBlocks.count());
    }
    SECTION("Just Start and End")
    {
      // Only uniform flow is the start and end
      // 0 -> 1
      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      controlFlow.Construct(inputs);
      uniformBlocks = controlFlow.GetUniformBlocks();
      REQUIRE(2 == uniformBlocks.count());
      REQUIRE(uniformBlocks.contains(0U));
      REQUIRE(uniformBlocks.contains(1U));
      loopBlocks = controlFlow.GetLoopBlocks();
      REQUIRE(0 == loopBlocks.count());
    }

    SECTION("Single Uniform Flow")
    {
      // Single uniform flow between start and end
      // 0 -> 1 -> 2 -> 3 -> 4
      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      inputs.push_back({1, 2});
      inputs.push_back({2, 3});
      inputs.push_back({3, 4});
      controlFlow.Construct(inputs);
      uniformBlocks = controlFlow.GetUniformBlocks();
      REQUIRE(5 == uniformBlocks.count());
      REQUIRE(uniformBlocks.contains(0U));
      REQUIRE(uniformBlocks.contains(1U));
      REQUIRE(uniformBlocks.contains(2U));
      REQUIRE(uniformBlocks.contains(3U));
      REQUIRE(uniformBlocks.contains(4U));
      loopBlocks = controlFlow.GetLoopBlocks();
      REQUIRE(0 == loopBlocks.count());
    }

    SECTION("Simple Branch")
    {
      // Simple branch
      // 0 -> 1
      // 0 -> 2
      // 1 -> 2
      // 2 -> 3
      // 2 -> 4
      // 3 -> 4
      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      inputs.push_back({0, 2});
      inputs.push_back({1, 2});
      inputs.push_back({2, 3});
      inputs.push_back({2, 4});
      inputs.push_back({3, 4});
      controlFlow.Construct(inputs);
      uniformBlocks = controlFlow.GetUniformBlocks();
      REQUIRE(3 == uniformBlocks.count());
      REQUIRE(uniformBlocks.contains(0U));
      REQUIRE(uniformBlocks.contains(2U));
      REQUIRE(uniformBlocks.contains(4U));
      loopBlocks = controlFlow.GetLoopBlocks();
      REQUIRE(0 == loopBlocks.count());
    }
    SECTION("Finite Loop1")
    {
      // Finite loop (3 -> 4 -> 5 -> 3)
      // 0 -> 1 -> 3
      // 0 -> 2 -> 3
      // 3 -> 4 -> 5
      // 4 -> 6
      // 5 -> 3
      // 5 -> 6
      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      inputs.push_back({1, 3});
      inputs.push_back({0, 2});
      inputs.push_back({2, 3});
      inputs.push_back({3, 4});
      inputs.push_back({4, 5});
      inputs.push_back({4, 6});
      inputs.push_back({5, 3});
      inputs.push_back({5, 6});
      controlFlow.Construct(inputs);
      uniformBlocks = controlFlow.GetUniformBlocks();
      REQUIRE(2 == uniformBlocks.count());
      REQUIRE(uniformBlocks.contains(0U));
      REQUIRE(uniformBlocks.contains(6U));
      loopBlocks = controlFlow.GetLoopBlocks();
      REQUIRE(3 == loopBlocks.count());
      REQUIRE(loopBlocks.contains(3U));
      REQUIRE(loopBlocks.contains(4U));
      REQUIRE(loopBlocks.contains(5U));
    }
    SECTION("Finite Loop2")
    {
      // Finite loop (3 -> 4 -> 5 -> 3)
      // 0 -> 1 -> 2
      // 0 -> 2
      // 2 -> 3
      // 3 -> 4 -> 5 -> 6
      // 3 -> 5 -> 3
      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      inputs.push_back({1, 2});
      inputs.push_back({0, 2});
      inputs.push_back({2, 3});
      inputs.push_back({3, 4});
      inputs.push_back({4, 5});
      inputs.push_back({5, 6});
      inputs.push_back({3, 5});
      inputs.push_back({5, 3});
      controlFlow.Construct(inputs);
      uniformBlocks = controlFlow.GetUniformBlocks();
      REQUIRE(3 == uniformBlocks.count());
      REQUIRE(uniformBlocks.contains(0U));
      REQUIRE(uniformBlocks.contains(2U));
      REQUIRE(uniformBlocks.contains(6U));
      loopBlocks = controlFlow.GetLoopBlocks();
      REQUIRE(3 == loopBlocks.count());
      REQUIRE(loopBlocks.contains(3U));
      REQUIRE(loopBlocks.contains(4U));
      REQUIRE(loopBlocks.contains(5U));
    }

    SECTION("Infinite Loop")
    {
      // Infinite loop which never converges (3 -> 4 -> 3)
      // 0 -> 1 -> 3
      // 0 -> 2 -> 3
      // 3 -> 4
      // 4 -> 3
      // 1 -> 6
      // 2 -> 6
      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      inputs.push_back({1, 3});
      inputs.push_back({0, 2});
      inputs.push_back({2, 3});
      inputs.push_back({3, 4});
      inputs.push_back({4, 3});
      inputs.push_back({1, 6});
      inputs.push_back({2, 6});
      controlFlow.Construct(inputs);
      uniformBlocks = controlFlow.GetUniformBlocks();
      REQUIRE(2 == uniformBlocks.count());
      REQUIRE(uniformBlocks.contains(0U));
      REQUIRE(uniformBlocks.contains(6U));
      loopBlocks = controlFlow.GetLoopBlocks();
      REQUIRE(2 == loopBlocks.count());
      REQUIRE(loopBlocks.contains(3U));
      REQUIRE(loopBlocks.contains(4U));
    }

    SECTION("Complex Case Two Loops")
    {
      // Complex case with two loops
      // Loop: 7 -> 9 -> 7, 13 -> 15 -> 13
      // 0 -> 1 -> 3
      // 0 -> 2 -> 3
      // 3 -> 4 -> 5
      // 3 -> 5
      // 5 -> 6 -> 7
      // 5 -> 11
      // 7 -> 8 -> 11
      // 7 -> 9 -> 7
      // 9 -> 10 -> 11 -> 12 -> 13 -> 14 -> 17 -> 18 -> 19 -> 20 -> 21 -> 22 -> 23 -> 26
      // 13 -> 15 -> 13
      // 15 -> 16 -> 17 -> 19 -> 21 -> 26
      // 11 -> 17
      // 22 -> 24 -> 25 -> 26
      // 24 -> 26
      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      inputs.push_back({0, 2});
      inputs.push_back({2, 3});
      inputs.push_back({1, 3});
      inputs.push_back({3, 4});
      inputs.push_back({4, 5});
      inputs.push_back({3, 5});
      inputs.push_back({5, 6});
      inputs.push_back({9, 7});
      inputs.push_back({6, 7});
      inputs.push_back({7, 8});
      inputs.push_back({7, 9});
      inputs.push_back({9, 10});
      inputs.push_back({10, 11});
      inputs.push_back({8, 11});
      inputs.push_back({5, 11});
      inputs.push_back({11, 12});
      inputs.push_back({15, 13});
      inputs.push_back({12, 13});
      inputs.push_back({13, 14});
      inputs.push_back({13, 15});
      inputs.push_back({15, 16});
      inputs.push_back({16, 17});
      inputs.push_back({14, 17});
      inputs.push_back({11, 17});
      inputs.push_back({17, 18});
      inputs.push_back({18, 19});
      inputs.push_back({17, 19});
      inputs.push_back({19, 20});
      inputs.push_back({20, 21});
      inputs.push_back({19, 21});
      inputs.push_back({21, 22});
      inputs.push_back({22, 23});
      inputs.push_back({22, 24});
      inputs.push_back({24, 25});
      inputs.push_back({25, 26});
      inputs.push_back({24, 26});
      inputs.push_back({23, 26});
      inputs.push_back({21, 26});
      controlFlow.Construct(inputs);
      uniformBlocks = controlFlow.GetUniformBlocks();
      REQUIRE(8 == uniformBlocks.count());
      REQUIRE(uniformBlocks.contains(0U));
      REQUIRE(uniformBlocks.contains(3U));
      REQUIRE(uniformBlocks.contains(5U));
      REQUIRE(uniformBlocks.contains(11U));
      REQUIRE(uniformBlocks.contains(17U));
      REQUIRE(uniformBlocks.contains(19U));
      REQUIRE(uniformBlocks.contains(21U));
      REQUIRE(uniformBlocks.contains(26U));
      loopBlocks = controlFlow.GetLoopBlocks();
      REQUIRE(4 == loopBlocks.count());
      REQUIRE(loopBlocks.contains(7U));
      REQUIRE(loopBlocks.contains(9U));
      REQUIRE(loopBlocks.contains(13U));
      REQUIRE(loopBlocks.contains(15U));
    }
    SECTION("Complex Case Multiple Loops")
    {
      // Complex case with multiple loops: 4 -> 5 -> 6 -> 4, 10 -> 11 -> 12 -> 10, 68
      // 0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 4
      // 0 -> 2 -> 8 -> 9 -> 10 -> 11 -> 12 -> 10
      // 4 -> 6 -> 7 -> 8 -> 14 -> 15 -> 19 -> 20 -> 24 -> 25 -> 29 -> 31 -> 32 -> 33
      // 10 -> 12 -> 13 -> 14 -> 16 -> 17 -> 19
      // 16 -> 18 -> 19 -> 21 -> 22 -> 23 -> 24 -> 26 -> 27 -> 29 -> 30 -> 33 -> 35 -> 37
      // 22 -> 24
      // 26 -> 28 -> 29

      // 31 -> 33 -> 34 -> 37 -> 39 -> 40 -> 41 -> 42 -> 43 -> 44 -> 45 -> 47 -> 49 -> 51 -> 52 ->
      //   53 -> 54 -> 55 -> 57 -> 58 -> 59 -> 60 -> 61 -> 62 -> 63 -> 64 -> 65 -> 66 -> 68 -> 67
      //   -> 69 -> 70 -> 71 -> 72 -> 73 -> 74 -> 75 -> 76 -> 77 -> 78 -> 79 -> END

      // 35 -> 36 -> 37 -> 38 -> 41 39 -> 41 -> 43 -> 45 -> 46 -> 47 -> 48 -> 49 -> 50 -> 51
      // 51 -> 53 -> 55 -> 56 -> 57 -> 59 -> 61 -> 63 -> 65 -> 69 -> 71 -> 73 -> 75 -> 77 -> 79
      // 68 -> 68
      rdcarray<BlockLink> inputs;
      inputs.push_back({8, 9});
      inputs.push_back({8, 14});
      inputs.push_back({64, 65});
      inputs.push_back({0, 1});
      inputs.push_back({0, 2});
      inputs.push_back({1, 2});
      inputs.push_back({2, 3});
      inputs.push_back({2, 8});
      inputs.push_back({6, 4});
      inputs.push_back({6, 7});
      inputs.push_back({3, 4});
      inputs.push_back({4, 5});
      inputs.push_back({4, 6});
      inputs.push_back({5, 6});
      inputs.push_back({7, 8});
      inputs.push_back({12, 10});
      inputs.push_back({12, 13});
      inputs.push_back({9, 10});
      inputs.push_back({10, 11});
      inputs.push_back({10, 12});
      inputs.push_back({11, 12});
      inputs.push_back({13, 14});
      inputs.push_back({14, 15});
      inputs.push_back({14, 16});
      inputs.push_back({16, 17});
      inputs.push_back({16, 18});
      inputs.push_back({18, 19});
      inputs.push_back({17, 19});
      inputs.push_back({15, 19});
      inputs.push_back({19, 20});
      inputs.push_back({19, 21});
      inputs.push_back({21, 22});
      inputs.push_back({21, 23});
      inputs.push_back({23, 24});
      inputs.push_back({22, 24});
      inputs.push_back({20, 24});
      inputs.push_back({24, 25});
      inputs.push_back({24, 26});
      inputs.push_back({26, 27});
      inputs.push_back({26, 28});
      inputs.push_back({28, 29});
      inputs.push_back({27, 29});
      inputs.push_back({25, 29});
      inputs.push_back({29, 30});
      inputs.push_back({29, 31});
      inputs.push_back({31, 32});
      inputs.push_back({31, 33});
      inputs.push_back({32, 33});
      inputs.push_back({30, 33});
      inputs.push_back({33, 34});
      inputs.push_back({33, 35});
      inputs.push_back({35, 37});
      inputs.push_back({35, 36});
      inputs.push_back({36, 37});
      inputs.push_back({34, 37});
      inputs.push_back({37, 38});
      inputs.push_back({37, 39});
      inputs.push_back({39, 40});
      inputs.push_back({39, 41});
      inputs.push_back({40, 41});
      inputs.push_back({38, 41});
      inputs.push_back({41, 42});
      inputs.push_back({41, 43});
      inputs.push_back({42, 43});
      inputs.push_back({43, 44});
      inputs.push_back({43, 45});
      inputs.push_back({44, 45});
      inputs.push_back({45, 46});
      inputs.push_back({45, 47});
      inputs.push_back({46, 47});
      inputs.push_back({47, 48});
      inputs.push_back({47, 49});
      inputs.push_back({48, 49});
      inputs.push_back({49, 50});
      inputs.push_back({49, 51});
      inputs.push_back({50, 51});
      inputs.push_back({51, 52});
      inputs.push_back({51, 53});
      inputs.push_back({52, 53});
      inputs.push_back({53, 54});
      inputs.push_back({53, 55});
      inputs.push_back({54, 55});
      inputs.push_back({55, 56});
      inputs.push_back({55, 57});
      inputs.push_back({56, 57});
      inputs.push_back({57, 58});
      inputs.push_back({57, 59});
      inputs.push_back({58, 59});
      inputs.push_back({59, 60});
      inputs.push_back({59, 61});
      inputs.push_back({60, 61});
      inputs.push_back({61, 62});
      inputs.push_back({61, 63});
      inputs.push_back({62, 63});
      inputs.push_back({63, 64});
      inputs.push_back({63, 65});
      inputs.push_back({65, 66});
      inputs.push_back({65, 69});
      inputs.push_back({68, 67});
      inputs.push_back({68, 68});
      inputs.push_back({66, 68});
      inputs.push_back({67, 69});
      inputs.push_back({69, 70});
      inputs.push_back({69, 71});
      inputs.push_back({70, 71});
      inputs.push_back({71, 72});
      inputs.push_back({71, 73});
      inputs.push_back({72, 73});
      inputs.push_back({73, 74});
      inputs.push_back({73, 75});
      inputs.push_back({74, 75});
      inputs.push_back({75, 76});
      inputs.push_back({75, 77});
      inputs.push_back({76, 77});
      inputs.push_back({77, 78});
      inputs.push_back({77, 79});
      inputs.push_back({78, 79});
      controlFlow.Construct(inputs);
      uniformBlocks = controlFlow.GetUniformBlocks();
      REQUIRE(28 == uniformBlocks.count());
      REQUIRE(uniformBlocks.contains(0U));
      REQUIRE(uniformBlocks.contains(2U));
      REQUIRE(uniformBlocks.contains(8U));
      REQUIRE(uniformBlocks.contains(14U));
      REQUIRE(uniformBlocks.contains(19U));
      REQUIRE(uniformBlocks.contains(24U));
      REQUIRE(uniformBlocks.contains(29U));
      REQUIRE(uniformBlocks.contains(33U));
      REQUIRE(uniformBlocks.contains(37U));
      REQUIRE(uniformBlocks.contains(41U));
      REQUIRE(uniformBlocks.contains(43U));
      REQUIRE(uniformBlocks.contains(45U));
      REQUIRE(uniformBlocks.contains(47U));
      REQUIRE(uniformBlocks.contains(49U));
      REQUIRE(uniformBlocks.contains(51U));
      REQUIRE(uniformBlocks.contains(53U));
      REQUIRE(uniformBlocks.contains(55U));
      REQUIRE(uniformBlocks.contains(57U));
      REQUIRE(uniformBlocks.contains(59U));
      REQUIRE(uniformBlocks.contains(61U));
      REQUIRE(uniformBlocks.contains(63U));
      REQUIRE(uniformBlocks.contains(65U));
      REQUIRE(uniformBlocks.contains(69U));
      REQUIRE(uniformBlocks.contains(71U));
      REQUIRE(uniformBlocks.contains(73U));
      REQUIRE(uniformBlocks.contains(75U));
      REQUIRE(uniformBlocks.contains(77U));
      REQUIRE(uniformBlocks.contains(79U));
      loopBlocks = controlFlow.GetLoopBlocks();
      REQUIRE(7 == loopBlocks.count());
      REQUIRE(loopBlocks.contains(4U));
      REQUIRE(loopBlocks.contains(5U));
      REQUIRE(loopBlocks.contains(6U));
      REQUIRE(loopBlocks.contains(10U));
      REQUIRE(loopBlocks.contains(11U));
      REQUIRE(loopBlocks.contains(12U));
      REQUIRE(loopBlocks.contains(68U));
    }
    SECTION("Single Loop Specific Setup")
    {
      // Specific loop case where a block (2) in a loop is only in a single path
      // 0 -> 1 -> 3 - 1
      // 0 -> 1 -> 2 -> 3
      // 3 -> 4 -> END
      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      inputs.push_back({1, 3});
      inputs.push_back({3, 1});
      inputs.push_back({1, 2});
      inputs.push_back({2, 3});
      inputs.push_back({3, 4});
      controlFlow.Construct(inputs);
      uniformBlocks = controlFlow.GetUniformBlocks();
      REQUIRE(2 == uniformBlocks.count());
      REQUIRE(uniformBlocks.contains(0U));
      REQUIRE(uniformBlocks.contains(4U));
      loopBlocks = controlFlow.GetLoopBlocks();
      REQUIRE(3 == loopBlocks.count());
      REQUIRE(loopBlocks.contains(1U));
      REQUIRE(loopBlocks.contains(2U));
      REQUIRE(loopBlocks.contains(3U));
    }
  };
  SECTION("FindConvergenceBlocks")
  {
    ControlFlow controlFlow;
    rdcarray<uint32_t> divergentBlocks;
    rdcarray<ConvergentBlockData> convergentBlocks;
    rdcarray<ConvergentBlockData> expectedConvergentBlocks;
    int32_t expectedCountDivergentBlocks = 0;
    SECTION("Degenerate Case")
    {
      // Degenerate case
      rdcarray<BlockLink> inputs;

      expectedCountDivergentBlocks = 0;
      expectedConvergentBlocks = {};
      REQUIRE(expectedConvergentBlocks.count() == expectedCountDivergentBlocks);

      controlFlow.Construct(inputs);
      divergentBlocks = controlFlow.GetDivergentBlocks();
      convergentBlocks = controlFlow.GetConvergentBlocks();
      REQUIRE(expectedCountDivergentBlocks == divergentBlocks.count());
      REQUIRE(expectedCountDivergentBlocks == convergentBlocks.count());
      CheckDivergentBlocks(expectedConvergentBlocks, divergentBlocks);
      CheckConvergentBlocks(expectedConvergentBlocks, convergentBlocks);
    }

    SECTION("Just Start and End")
    {
      // No divergent blocks
      // 0 -> 1
      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});

      expectedCountDivergentBlocks = 0;
      expectedConvergentBlocks = {};
      REQUIRE(expectedConvergentBlocks.count() == expectedCountDivergentBlocks);

      controlFlow.Construct(inputs);
      divergentBlocks = controlFlow.GetDivergentBlocks();
      convergentBlocks = controlFlow.GetConvergentBlocks();
      REQUIRE(expectedCountDivergentBlocks == divergentBlocks.count());
      REQUIRE(expectedCountDivergentBlocks == convergentBlocks.count());
      CheckDivergentBlocks(expectedConvergentBlocks, divergentBlocks);
      CheckConvergentBlocks(expectedConvergentBlocks, convergentBlocks);
    }
    SECTION("Single Branch")
    {
      // Single divergent block : 0
      // Single convergent block : 0->3
      // 0 -> 1 -> 3
      // 0 -> 2 -> 3
      // 3 -> 4
      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      inputs.push_back({1, 3});
      inputs.push_back({0, 2});
      inputs.push_back({2, 3});
      inputs.push_back({3, 4});

      expectedCountDivergentBlocks = 1;
      expectedConvergentBlocks = {
          {0, 3},
      };
      REQUIRE(expectedConvergentBlocks.count() == expectedCountDivergentBlocks);

      controlFlow.Construct(inputs);
      divergentBlocks = controlFlow.GetDivergentBlocks();
      convergentBlocks = controlFlow.GetConvergentBlocks();
      REQUIRE(expectedCountDivergentBlocks == divergentBlocks.count());
      REQUIRE(expectedCountDivergentBlocks == convergentBlocks.count());
      CheckDivergentBlocks(expectedConvergentBlocks, divergentBlocks);
      CheckConvergentBlocks(expectedConvergentBlocks, convergentBlocks);
    }
    SECTION("Simple Double Branch")
    {
      // Two divergent blocks : 0, 2
      // Two convergent blocks : 0->2, 2->4
      // 0 -> 1
      // 0 -> 2
      // 1 -> 2
      // 2 -> 3 -> 4
      // 2 -> 4
      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      inputs.push_back({0, 2});
      inputs.push_back({1, 2});
      inputs.push_back({2, 3});
      inputs.push_back({2, 4});
      inputs.push_back({3, 4});

      expectedCountDivergentBlocks = 2;
      expectedConvergentBlocks = {
          {0, 2},
          {2, 4},
      };
      REQUIRE(expectedConvergentBlocks.count() == expectedCountDivergentBlocks);

      controlFlow.Construct(inputs);
      divergentBlocks = controlFlow.GetDivergentBlocks();
      convergentBlocks = controlFlow.GetConvergentBlocks();
      REQUIRE(expectedCountDivergentBlocks == divergentBlocks.count());
      REQUIRE(expectedCountDivergentBlocks == convergentBlocks.count());
      CheckDivergentBlocks(expectedConvergentBlocks, divergentBlocks);
      CheckConvergentBlocks(expectedConvergentBlocks, convergentBlocks);
    }
    SECTION("Nested Branch")
    {
      // Two divergent blocks : 0, 3
      // Two convergent blocks : 0->9, 3->8
      // 0 -> 1
      // 0 -> 2
      // 1 -> 3
      // 3 -> 4
      // 3 -> 5
      // 4 -> 6
      // 5 -> 7
      // 6 -> 8
      // 7 -> 8
      // 8 -> 9
      // 2 -> 9
      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      inputs.push_back({0, 2});
      inputs.push_back({1, 3});
      inputs.push_back({3, 4});
      inputs.push_back({3, 5});
      inputs.push_back({4, 6});
      inputs.push_back({5, 7});
      inputs.push_back({6, 8});
      inputs.push_back({7, 8});
      inputs.push_back({8, 9});
      inputs.push_back({2, 9});

      expectedCountDivergentBlocks = 2;
      expectedConvergentBlocks = {
          {0, 9},
          {3, 8},
      };
      REQUIRE(expectedConvergentBlocks.count() == expectedCountDivergentBlocks);

      controlFlow.Construct(inputs);
      divergentBlocks = controlFlow.GetDivergentBlocks();
      convergentBlocks = controlFlow.GetConvergentBlocks();
      REQUIRE(expectedCountDivergentBlocks == divergentBlocks.count());
      REQUIRE(expectedCountDivergentBlocks == convergentBlocks.count());
      CheckDivergentBlocks(expectedConvergentBlocks, divergentBlocks);
      CheckConvergentBlocks(expectedConvergentBlocks, convergentBlocks);
    }
    SECTION("Nested Linked Branch")
    {
      // Two divergent blocks : 0, 3, 4
      // Two convergent blocks : 0->13, 3->11, 4->13
      // 0 -> 1
      // 0 -> 2
      // 1 -> 3
      // 2 -> 4
      // 3 -> 5
      // 3 -> 6
      // 4 -> 6
      // 4 -> 7
      // 5 -> 8
      // 6 -> 9
      // 7 -> 10
      // 8 -> 11
      // 9 -> 11
      // 11 -> 12
      // 12 -> 13
      // 10 -> 13
      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      inputs.push_back({0, 2});
      inputs.push_back({1, 3});
      inputs.push_back({2, 4});
      inputs.push_back({3, 5});
      inputs.push_back({3, 6});
      inputs.push_back({4, 6});
      inputs.push_back({4, 7});
      inputs.push_back({5, 8});
      inputs.push_back({6, 9});
      inputs.push_back({7, 10});
      inputs.push_back({8, 11});
      inputs.push_back({9, 11});
      inputs.push_back({11, 12});
      inputs.push_back({12, 13});
      inputs.push_back({10, 13});

      expectedCountDivergentBlocks = 3;
      expectedConvergentBlocks = {
          {0, 13},
          {3, 11},
          {4, 13},
      };
      REQUIRE(expectedConvergentBlocks.count() == expectedCountDivergentBlocks);

      controlFlow.Construct(inputs);
      divergentBlocks = controlFlow.GetDivergentBlocks();
      convergentBlocks = controlFlow.GetConvergentBlocks();
      REQUIRE(expectedCountDivergentBlocks == divergentBlocks.count());
      REQUIRE(expectedCountDivergentBlocks == convergentBlocks.count());
      CheckDivergentBlocks(expectedConvergentBlocks, divergentBlocks);
      CheckConvergentBlocks(expectedConvergentBlocks, convergentBlocks);
    }
    SECTION("Simple Loop")
    {
      // One divergent block : 2
      // One convergent block : 2->3
      // 0 -> 1
      // 1 -> 2
      // 2 -> 1
      // 2 -> 3
      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      inputs.push_back({1, 2});
      inputs.push_back({2, 1});
      inputs.push_back({2, 3});

      expectedCountDivergentBlocks = 1;
      expectedConvergentBlocks = {
          {2, 3},
      };
      REQUIRE(expectedConvergentBlocks.count() == expectedCountDivergentBlocks);

      controlFlow.Construct(inputs);
      divergentBlocks = controlFlow.GetDivergentBlocks();
      convergentBlocks = controlFlow.GetConvergentBlocks();
      REQUIRE(expectedCountDivergentBlocks == divergentBlocks.count());
      REQUIRE(expectedCountDivergentBlocks == convergentBlocks.count());
      CheckDivergentBlocks(expectedConvergentBlocks, divergentBlocks);
      CheckConvergentBlocks(expectedConvergentBlocks, convergentBlocks);
    }
    SECTION("Loop with multiple exits")
    {
      // Two divergent blocks : 2, 3
      // Two convergent blocks : 2->6, 3->6
      // 0 -> 1
      // 1 -> 2
      // 2 -> 3
      // 2 -> 4
      // 3 -> 1
      // 3 -> 6
      // 4 -> 5
      // 5 -> 6
      // 6 -> 7

      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      inputs.push_back({1, 2});
      inputs.push_back({2, 3});
      inputs.push_back({2, 4});
      inputs.push_back({3, 1});
      inputs.push_back({3, 6});
      inputs.push_back({4, 5});
      inputs.push_back({5, 6});
      inputs.push_back({6, 7});

      expectedCountDivergentBlocks = 2;
      expectedConvergentBlocks = {
          {2, 6},
          {3, 6},
      };
      REQUIRE(expectedConvergentBlocks.count() == expectedCountDivergentBlocks);

      controlFlow.Construct(inputs);
      divergentBlocks = controlFlow.GetDivergentBlocks();
      convergentBlocks = controlFlow.GetConvergentBlocks();
      REQUIRE(expectedCountDivergentBlocks == divergentBlocks.count());
      REQUIRE(expectedCountDivergentBlocks == convergentBlocks.count());
      CheckDivergentBlocks(expectedConvergentBlocks, divergentBlocks);
      CheckConvergentBlocks(expectedConvergentBlocks, convergentBlocks);
    }
    SECTION("Multiple Loops with multiple exits")
    {
      // Three divergent blocks : 2, 3, 5
      // Three convergent blocks : 2->6, 3->6, 5->6
      // 0 -> 1
      // 1 -> 2
      // 2 -> 3
      // 2 -> 4
      // 3 -> 1
      // 3 -> 6
      // 4 -> 5
      // 5 -> 6
      // 5 -> 7
      // 7 -> 2
      // 6 -> 8

      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      inputs.push_back({1, 2});
      inputs.push_back({2, 3});
      inputs.push_back({2, 4});
      inputs.push_back({3, 1});
      inputs.push_back({3, 6});
      inputs.push_back({4, 5});
      inputs.push_back({5, 6});
      inputs.push_back({5, 7});
      inputs.push_back({7, 2});
      inputs.push_back({6, 8});

      expectedCountDivergentBlocks = 3;
      expectedConvergentBlocks = {
          {2, 6},
          {3, 6},
          {5, 6},
      };
      REQUIRE(expectedConvergentBlocks.count() == expectedCountDivergentBlocks);

      controlFlow.Construct(inputs);
      divergentBlocks = controlFlow.GetDivergentBlocks();
      convergentBlocks = controlFlow.GetConvergentBlocks();
      REQUIRE(expectedCountDivergentBlocks == divergentBlocks.count());
      REQUIRE(expectedCountDivergentBlocks == convergentBlocks.count());
      CheckDivergentBlocks(expectedConvergentBlocks, divergentBlocks);
      CheckConvergentBlocks(expectedConvergentBlocks, convergentBlocks);
    }
    SECTION("If inside a Loop")
    {
      // Two divergent blocks : 2, 6
      // Two convergent blocks : 2->5, 6->7
      // 0 -> 1
      // 1 -> 2
      // 2 -> 3
      // 2 -> 4
      // 4 -> 5
      // 5 -> 6
      // 6 -> 1
      // 6 -> 7

      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      inputs.push_back({1, 2});
      inputs.push_back({2, 3});
      inputs.push_back({2, 4});
      inputs.push_back({3, 5});
      inputs.push_back({4, 5});
      inputs.push_back({5, 6});
      inputs.push_back({6, 1});
      inputs.push_back({6, 7});

      expectedCountDivergentBlocks = 2;
      expectedConvergentBlocks = {
          {2, 5},
          {6, 7},
      };
      REQUIRE(expectedConvergentBlocks.count() == expectedCountDivergentBlocks);

      controlFlow.Construct(inputs);
      divergentBlocks = controlFlow.GetDivergentBlocks();
      convergentBlocks = controlFlow.GetConvergentBlocks();
      REQUIRE(expectedCountDivergentBlocks == divergentBlocks.count());
      REQUIRE(expectedCountDivergentBlocks == convergentBlocks.count());
      CheckDivergentBlocks(expectedConvergentBlocks, divergentBlocks);
      CheckConvergentBlocks(expectedConvergentBlocks, convergentBlocks);
    }
    SECTION("Single Uniform Flow")
    {
      // Single uniform flow between start and end
      // 0 -> 1 -> 2 -> 3 -> 4
      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      inputs.push_back({1, 2});
      inputs.push_back({2, 3});
      inputs.push_back({3, 4});
      controlFlow.Construct(inputs);

      expectedCountDivergentBlocks = 0;
      expectedConvergentBlocks = {};
      REQUIRE(expectedConvergentBlocks.count() == expectedCountDivergentBlocks);

      controlFlow.Construct(inputs);
      divergentBlocks = controlFlow.GetDivergentBlocks();
      convergentBlocks = controlFlow.GetConvergentBlocks();
      REQUIRE(expectedCountDivergentBlocks == divergentBlocks.count());
      REQUIRE(expectedCountDivergentBlocks == convergentBlocks.count());
      CheckDivergentBlocks(expectedConvergentBlocks, divergentBlocks);
      CheckConvergentBlocks(expectedConvergentBlocks, convergentBlocks);
    }
    SECTION("Infinite Loop")
    {
      // Infinite loop which never converges (3 -> 4 -> 3)
      // 0 -> 1 -> 3
      // 0 -> 2 -> 3
      // 3 -> 4
      // 4 -> 3
      // 1 -> 6
      // 2 -> 6
      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      inputs.push_back({1, 3});
      inputs.push_back({0, 2});
      inputs.push_back({2, 3});
      inputs.push_back({3, 4});
      inputs.push_back({4, 3});
      inputs.push_back({1, 6});
      inputs.push_back({2, 6});
      controlFlow.Construct(inputs);

      expectedCountDivergentBlocks = 3;
      expectedConvergentBlocks = {
          {0, 6},
          {1, 6},
          {2, 6},
      };
      REQUIRE(expectedConvergentBlocks.count() == expectedCountDivergentBlocks);

      controlFlow.Construct(inputs);
      divergentBlocks = controlFlow.GetDivergentBlocks();
      convergentBlocks = controlFlow.GetConvergentBlocks();
      REQUIRE(expectedCountDivergentBlocks == divergentBlocks.count());
      REQUIRE(expectedCountDivergentBlocks == convergentBlocks.count());
      CheckDivergentBlocks(expectedConvergentBlocks, divergentBlocks);
      CheckConvergentBlocks(expectedConvergentBlocks, convergentBlocks);
    }

    SECTION("Complex Case Two Loops")
    {
      // Complex case with two loops
      // Loop: 7 -> 9 -> 7, 13 -> 15 -> 13
      // 0 -> 1 -> 3
      // 0 -> 2 -> 3
      // 3 -> 4 -> 5
      // 3 -> 5
      // 5 -> 6 -> 7
      // 5 -> 11
      // 7 -> 8 -> 11
      // 7 -> 9 -> 7
      // 9 -> 10 -> 11 -> 12 -> 13 -> 14 -> 17 -> 18 -> 19 -> 20 -> 21 -> 22 -> 23 -> 26
      // 13 -> 15 -> 13
      // 15 -> 16 -> 17 -> 19 -> 21 -> 26
      // 11 -> 17
      // 22 -> 24 -> 25 -> 26
      // 24 -> 26
      rdcarray<BlockLink> inputs;
      inputs.push_back({0, 1});
      inputs.push_back({0, 2});
      inputs.push_back({2, 3});
      inputs.push_back({1, 3});
      inputs.push_back({3, 4});
      inputs.push_back({4, 5});
      inputs.push_back({3, 5});
      inputs.push_back({5, 6});
      inputs.push_back({9, 7});
      inputs.push_back({6, 7});
      inputs.push_back({7, 8});
      inputs.push_back({7, 9});
      inputs.push_back({9, 10});
      inputs.push_back({10, 11});
      inputs.push_back({8, 11});
      inputs.push_back({5, 11});
      inputs.push_back({11, 12});
      inputs.push_back({15, 13});
      inputs.push_back({12, 13});
      inputs.push_back({13, 14});
      inputs.push_back({13, 15});
      inputs.push_back({15, 16});
      inputs.push_back({16, 17});
      inputs.push_back({14, 17});
      inputs.push_back({11, 17});
      inputs.push_back({17, 18});
      inputs.push_back({18, 19});
      inputs.push_back({17, 19});
      inputs.push_back({19, 20});
      inputs.push_back({20, 21});
      inputs.push_back({19, 21});
      inputs.push_back({21, 22});
      inputs.push_back({22, 23});
      inputs.push_back({22, 24});
      inputs.push_back({24, 25});
      inputs.push_back({25, 26});
      inputs.push_back({24, 26});
      inputs.push_back({23, 26});
      inputs.push_back({21, 26});
      controlFlow.Construct(inputs);

      expectedCountDivergentBlocks = 13;
      expectedConvergentBlocks = {
          {0, 3},   {3, 5},   {5, 11},  {7, 11},  {9, 10},  {11, 17}, {13, 17},
          {15, 16}, {17, 19}, {19, 21}, {21, 26}, {22, 26}, {24, 26},
      };
      REQUIRE(expectedConvergentBlocks.count() == expectedCountDivergentBlocks);

      controlFlow.Construct(inputs);
      divergentBlocks = controlFlow.GetDivergentBlocks();
      convergentBlocks = controlFlow.GetConvergentBlocks();
      REQUIRE(expectedCountDivergentBlocks == divergentBlocks.count());
      REQUIRE(expectedCountDivergentBlocks == convergentBlocks.count());
      CheckDivergentBlocks(expectedConvergentBlocks, divergentBlocks);
      CheckConvergentBlocks(expectedConvergentBlocks, convergentBlocks);
    }
    SECTION("Complex Case Multiple Loops")
    {
      // Complex case with multiple loops: 4 -> 5 -> 6 -> 4, 10 -> 11 -> 12 -> 10, 68
      // 0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 4
      // 0 -> 2 -> 8 -> 9 -> 10 -> 11 -> 12 -> 10
      // 4 -> 6 -> 7 -> 8 -> 14 -> 15 -> 19 -> 20 -> 24 -> 25 -> 29 -> 31 -> 32 -> 33
      // 10 -> 12 -> 13 -> 14 -> 16 -> 17 -> 19
      // 16 -> 18 -> 19 -> 21 -> 22 -> 23 -> 24 -> 26 -> 27 -> 29 -> 30 -> 33 -> 35 -> 37
      // 22 -> 24
      // 26 -> 28 -> 29

      // 31 -> 33 -> 34 -> 37 -> 39 -> 40 -> 41 -> 42 -> 43 -> 44 -> 45 -> 47 -> 49 -> 51 -> 52 ->
      //   53 -> 54 -> 55 -> 57 -> 58 -> 59 -> 60 -> 61 -> 62 -> 63 -> 64 -> 65 -> 66 -> 68 -> 67
      //   -> 69 -> 70 -> 71 -> 72 -> 73 -> 74 -> 75 -> 76 -> 77 -> 78 -> 79 -> END

      // 35 -> 36 -> 37 -> 38 -> 41 39 -> 41 -> 43 -> 45 -> 46 -> 47 -> 48 -> 49 -> 50 -> 51
      // 51 -> 53 -> 55 -> 56 -> 57 -> 59 -> 61 -> 63 -> 65 -> 69 -> 71 -> 73 -> 75 -> 77 -> 79
      // 68 -> 68
      rdcarray<BlockLink> inputs;
      inputs.push_back({8, 9});
      inputs.push_back({8, 14});
      inputs.push_back({64, 65});
      inputs.push_back({0, 1});
      inputs.push_back({0, 2});
      inputs.push_back({1, 2});
      inputs.push_back({2, 3});
      inputs.push_back({2, 8});
      inputs.push_back({6, 4});
      inputs.push_back({6, 7});
      inputs.push_back({3, 4});
      inputs.push_back({4, 5});
      inputs.push_back({4, 6});
      inputs.push_back({5, 6});
      inputs.push_back({7, 8});
      inputs.push_back({12, 10});
      inputs.push_back({12, 13});
      inputs.push_back({9, 10});
      inputs.push_back({10, 11});
      inputs.push_back({10, 12});
      inputs.push_back({11, 12});
      inputs.push_back({13, 14});
      inputs.push_back({14, 15});
      inputs.push_back({14, 16});
      inputs.push_back({16, 17});
      inputs.push_back({16, 18});
      inputs.push_back({18, 19});
      inputs.push_back({17, 19});
      inputs.push_back({15, 19});
      inputs.push_back({19, 20});
      inputs.push_back({19, 21});
      inputs.push_back({21, 22});
      inputs.push_back({21, 23});
      inputs.push_back({23, 24});
      inputs.push_back({22, 24});
      inputs.push_back({20, 24});
      inputs.push_back({24, 25});
      inputs.push_back({24, 26});
      inputs.push_back({26, 27});
      inputs.push_back({26, 28});
      inputs.push_back({28, 29});
      inputs.push_back({27, 29});
      inputs.push_back({25, 29});
      inputs.push_back({29, 30});
      inputs.push_back({29, 31});
      inputs.push_back({31, 32});
      inputs.push_back({31, 33});
      inputs.push_back({32, 33});
      inputs.push_back({30, 33});
      inputs.push_back({33, 34});
      inputs.push_back({33, 35});
      inputs.push_back({35, 37});
      inputs.push_back({35, 36});
      inputs.push_back({36, 37});
      inputs.push_back({34, 37});
      inputs.push_back({37, 38});
      inputs.push_back({37, 39});
      inputs.push_back({39, 40});
      inputs.push_back({39, 41});
      inputs.push_back({40, 41});
      inputs.push_back({38, 41});
      inputs.push_back({41, 42});
      inputs.push_back({41, 43});
      inputs.push_back({42, 43});
      inputs.push_back({43, 44});
      inputs.push_back({43, 45});
      inputs.push_back({44, 45});
      inputs.push_back({45, 46});
      inputs.push_back({45, 47});
      inputs.push_back({46, 47});
      inputs.push_back({47, 48});
      inputs.push_back({47, 49});
      inputs.push_back({48, 49});
      inputs.push_back({49, 50});
      inputs.push_back({49, 51});
      inputs.push_back({50, 51});
      inputs.push_back({51, 52});
      inputs.push_back({51, 53});
      inputs.push_back({52, 53});
      inputs.push_back({53, 54});
      inputs.push_back({53, 55});
      inputs.push_back({54, 55});
      inputs.push_back({55, 56});
      inputs.push_back({55, 57});
      inputs.push_back({56, 57});
      inputs.push_back({57, 58});
      inputs.push_back({57, 59});
      inputs.push_back({58, 59});
      inputs.push_back({59, 60});
      inputs.push_back({59, 61});
      inputs.push_back({60, 61});
      inputs.push_back({61, 62});
      inputs.push_back({61, 63});
      inputs.push_back({62, 63});
      inputs.push_back({63, 64});
      inputs.push_back({63, 65});
      inputs.push_back({65, 66});
      inputs.push_back({65, 69});
      inputs.push_back({68, 67});
      inputs.push_back({68, 68});
      inputs.push_back({66, 68});
      inputs.push_back({67, 69});
      inputs.push_back({69, 70});
      inputs.push_back({69, 71});
      inputs.push_back({70, 71});
      inputs.push_back({71, 72});
      inputs.push_back({71, 73});
      inputs.push_back({72, 73});
      inputs.push_back({73, 74});
      inputs.push_back({73, 75});
      inputs.push_back({74, 75});
      inputs.push_back({75, 76});
      inputs.push_back({75, 77});
      inputs.push_back({76, 77});
      inputs.push_back({77, 78});
      inputs.push_back({77, 79});
      inputs.push_back({78, 79});
      controlFlow.Construct(inputs);

      expectedCountDivergentBlocks = 38;
      expectedConvergentBlocks = {
          {0, 2},   {2, 8},   {4, 6},   {6, 7},   {8, 14},  {10, 12}, {12, 13}, {14, 19},
          {16, 19}, {19, 24}, {21, 24}, {24, 29}, {26, 29}, {29, 33}, {31, 33}, {33, 37},
          {35, 37}, {37, 41}, {39, 41}, {41, 43}, {43, 45}, {45, 47}, {47, 49}, {49, 51},
          {51, 53}, {53, 55}, {55, 57}, {57, 59}, {59, 61}, {61, 63}, {63, 65}, {65, 69},
          {68, 67}, {69, 71}, {71, 73}, {73, 75}, {75, 77}, {77, 79},
      };
      REQUIRE(expectedConvergentBlocks.count() == expectedCountDivergentBlocks);

      controlFlow.Construct(inputs);
      divergentBlocks = controlFlow.GetDivergentBlocks();
      convergentBlocks = controlFlow.GetConvergentBlocks();
      REQUIRE(expectedCountDivergentBlocks == divergentBlocks.count());
      REQUIRE(expectedCountDivergentBlocks == convergentBlocks.count());
      CheckDivergentBlocks(expectedConvergentBlocks, divergentBlocks);
      CheckConvergentBlocks(expectedConvergentBlocks, convergentBlocks);

      // OutputGraph("complex_case", &controlFlow);
    }
  };
};
#endif    // ENABLED(ENABLE_UNIT_TESTS)
