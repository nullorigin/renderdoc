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

#pragma once

#include <set>
#include "common/common.h"
#include "driver/shaders/dxbc/dx_debug.h"
#include "driver/shaders/dxbc/dxbc_bytecode.h"
#include "driver/shaders/dxbc/dxbc_container.h"
#include "dxil_bytecode.h"
#include "dxil_controlflow.h"
#include "dxil_debuginfo.h"

namespace DXILDebug
{
using namespace DXDebug;

typedef DXDebug::SampleGatherResourceData SampleGatherResourceData;
typedef DXDebug::SampleGatherSamplerData SampleGatherSamplerData;
typedef DXDebug::BindingSlot BindingSlot;
typedef DXDebug::GatherChannel GatherChannel;
typedef DXBCBytecode::SamplerMode SamplerMode;
typedef DXBC::InterpolationMode InterpolationMode;

class Debugger;
struct GlobalState;

// D3D12 descriptors are equal sized and treated as effectively one byte in size
const uint32_t D3D12_DESCRIPTOR_BYTESIZE = 1;

struct ExecutionPoint
{
  ExecutionPoint() : block(~0U), instruction(~0U) {}
  ExecutionPoint(uint32_t block, uint32_t instruction) : block(block), instruction(instruction) {}
  bool IsAfter(const ExecutionPoint &from, const DXIL::ControlFlow &controlFlow) const;

  uint32_t block;
  uint32_t instruction;
};

void GetInterpolationModeForInputParams(const rdcarray<SigParameter> &stageInputSig,
                                        const DXIL::Program *program,
                                        rdcarray<DXBC::InterpolationMode> &interpModes);

struct InputData
{
  InputData(int inputIndex, int arrayIndex, int numWords, ShaderBuiltin sysAttribute, bool inc,
            void *pData)
  {
    input = inputIndex;
    array = arrayIndex;
    numwords = numWords;
    sysattribute = sysAttribute;
    included = inc;
    data = pData;
  }

  void *data;
  ShaderBuiltin sysattribute;
  int input;
  int array;
  int numwords;
  bool included;
};

struct FunctionInfo
{
  typedef std::set<Id> ReferencedIds;
  typedef std::map<Id, ExecutionPoint> ExecutionPointPerId;
  typedef std::map<uint32_t, ReferencedIds> PhiReferencedIdsPerBlock;
  typedef rdcarray<rdcstr> Callstack;

  const DXIL::Function *function = NULL;
  ReferencedIds referencedIds;
  ExecutionPointPerId maxExecPointPerId;
  PhiReferencedIdsPerBlock phiReferencedIdsPerBlock;
  uint32_t globalInstructionOffset = ~0U;
  rdcarray<uint32_t> uniformBlocks;
  DXIL::ControlFlow controlFlow;
  std::map<uint32_t, Callstack> callstacks;
  rdcarray<uint32_t> instructionToBlock;
};

struct StackFrame
{
  StackFrame(const DXIL::Function *func) : function(func) {}
  const DXIL::Function *function;

  // the thread's live list before the function was entered
  rdcarray<bool> live;
};

struct GlobalVariable
{
  Id id;
  ShaderVariable var;
};

struct GlobalConstant
{
  Id id;
  ShaderVariable var;
};

struct ResourceReferenceInfo
{
  ResourceReferenceInfo() : resClass(DXIL::ResourceClass::Invalid) {}
  void Create(const DXIL::ResourceReference *resRef, uint32_t arrayIndex);
  bool Valid() const { return resClass != DXIL::ResourceClass::Invalid; }

  DXIL::ResourceClass resClass;
  BindingSlot binding;
  DescriptorCategory category;
  VarType type;

  struct SRVData
  {
    DXDebug::ResourceDimension dim;
    uint32_t sampleCount;
    DXDebug::ResourceRetType compType;
  };
  struct SamplerData
  {
    SamplerMode samplerMode;
  };
  union
  {
    SRVData srvData;
    SamplerData samplerData;
  };
};

class DebugAPIWrapper
{
public:
  // During shader debugging, when a new resource is encountered
  // These will be called to fetch the data on demand.
  virtual void FetchSRV(const BindingSlot &slot) = 0;
  virtual void FetchUAV(const BindingSlot &slot) = 0;

  virtual bool CalculateMathIntrinsic(DXIL::DXOp dxOp, const ShaderVariable &input,
                                      ShaderVariable &output) = 0;
  virtual bool CalculateSampleGather(DXIL::DXOp dxOp, SampleGatherResourceData resourceData,
                                     SampleGatherSamplerData samplerData, const ShaderVariable &uv,
                                     const ShaderVariable &ddxCalc, const ShaderVariable &ddyCalc,
                                     const int8_t texelOffsets[3], int multisampleIndex,
                                     float lodValue, float compareValue, const uint8_t swizzle[4],
                                     GatherChannel gatherChannel, DXBC::ShaderType shaderType,
                                     uint32_t instructionIdx, const char *opString,
                                     ShaderVariable &output) = 0;
  virtual ShaderVariable GetResourceInfo(DXIL::ResourceClass resClass,
                                         const DXDebug::BindingSlot &slot, uint32_t mipLevel,
                                         const DXBC::ShaderType shaderType, int &dim) = 0;
  virtual ShaderVariable GetSampleInfo(DXIL::ResourceClass resClass, const DXDebug::BindingSlot &slot,
                                       const DXBC::ShaderType shaderType, const char *opString) = 0;
  virtual ShaderVariable GetRenderTargetSampleInfo(const DXBC::ShaderType shaderType,
                                                   const char *opString) = 0;
  virtual ResourceReferenceInfo GetResourceReferenceInfo(const DXDebug::BindingSlot &slot) = 0;
  virtual ShaderDirectAccess GetShaderDirectAccess(DescriptorCategory category,
                                                   const DXDebug::BindingSlot &slot) = 0;
};

struct MemoryTracking
{
  void AllocateMemoryForType(const DXIL::Type *type, Id allocId, bool global, ShaderVariable &var);

  // Represents actual memory allocations (think of it like a memory heap)
  struct Allocation
  {
    // the allocated memory
    void *backingMemory;
    uint64_t size;
    bool global;
  };

  // Represents pointers within a Allocation memory allocation (heap)
  struct Pointer
  {
    // the Allocation that owns the memory pointed to
    Id baseMemoryId;
    // the memory pointer which will be within the Allocation backing memory
    void *memory;
    // size of the data the pointer
    uint64_t size;
  };

  // Memory allocations with backing memory (heaps)
  std::map<Id, Allocation> m_Allocations;
  // Pointers within a Allocation memory allocation, the allocated memory will be in m_Allocations
  std::map<Id, Pointer> m_Pointers;
};

typedef rdcflatmap<ShaderBuiltin, ShaderVariable> BuiltinInputs;

struct ThreadState
{
  ThreadState(Debugger &debugger, const GlobalState &globalState, uint32_t maxSSAId);
  ~ThreadState();

  void EnterFunction(const DXIL::Function *function, const rdcarray<DXIL::Value *> &args);
  void EnterEntryPoint(const DXIL::Function *function, ShaderDebugState *state);
  void StepNext(ShaderDebugState *state, DebugAPIWrapper *apiWrapper,
                const rdcarray<ThreadState> &workgroup);
  void StepOverNopInstructions();

  bool Finished() const;
  bool InUniformBlock() const;

  bool ExecuteInstruction(DebugAPIWrapper *apiWrapper, const rdcarray<ThreadState> &workgroup);

  void MarkResourceAccess(const rdcstr &name, const ResourceReferenceInfo &resRefInfo,
                          bool directAccess, const ShaderDirectAccess &access,
                          const ShaderBindIndex &bindIndex);
  void SetResult(const Id &id, ShaderVariable &result, DXIL::Operation op, DXIL::DXOp dxOpCode,
                 ShaderEvents flags);
  rdcstr GetArgumentName(uint32_t i) const;
  Id GetArgumentId(uint32_t i) const;
  ResourceReferenceInfo GetResource(Id handleId, bool &annotatedHandle);
  void FillCallstack(ShaderDebugState &state);

  bool GetShaderVariable(const DXIL::Value *dxilValue, DXIL::Operation op, DXIL::DXOp dxOpCode,
                         ShaderVariable &var, bool flushDenormInput = true) const
  {
    return GetShaderVariableHelper(dxilValue, op, dxOpCode, var, flushDenormInput, true);
  }

  bool GetPhiShaderVariable(const DXIL::Value *dxilValue, DXIL::Operation op, DXIL::DXOp dxOpCode,
                            ShaderVariable &var, bool flushDenormInput = true) const
  {
    return GetShaderVariableHelper(dxilValue, op, dxOpCode, var, flushDenormInput, false);
  }

  bool GetLiveVariable(const Id &id, DXIL::Operation opCode, DXIL::DXOp dxOpCode,
                       ShaderVariable &var) const;
  bool GetPhiVariable(const Id &id, DXIL::Operation opCode, DXIL::DXOp dxOpCode,
                      ShaderVariable &var) const;
  bool GetVariableHelper(DXIL::Operation op, DXIL::DXOp dxOpCode, ShaderVariable &var) const;
  void UpdateBackingMemoryFromVariable(void *ptr, uint64_t &allocSize, const ShaderVariable &var);
  void UpdateMemoryVariableFromBackingMemory(Id memoryId, const void *ptr);

  void PerformGPUResourceOp(const rdcarray<ThreadState> &workgroup, DXIL::Operation opCode,
                            DXIL::DXOp dxOpCode, const ResourceReferenceInfo &resRef,
                            DebugAPIWrapper *apiWrapper, const DXIL::Instruction &inst,
                            ShaderVariable &result);
  void Sub(const ShaderVariable &a, const ShaderVariable &b, ShaderValue &ret) const;

  ShaderValue DDX(bool fine, DXIL::Operation opCode, DXIL::DXOp dxOpCode,
                  const rdcarray<ThreadState> &workgroup, const DXIL::Value *dxilValue) const;
  ShaderValue DDY(bool fine, DXIL::Operation opCode, DXIL::DXOp dxOpCode,
                  const rdcarray<ThreadState> &workgroup, const DXIL::Value *dxilValue) const;

  void ProcessScopeChange(const rdcarray<bool> &oldLive, const rdcarray<bool> &newLive);

  static bool WorkgroupIsDiverged(const rdcarray<ThreadState> &workgroup);
  static bool QuadIsDiverged(const rdcarray<ThreadState> &workgroup,
                             const rdcfixedarray<uint32_t, 4> &quadNeighbours);

  bool GetShaderVariableHelper(const DXIL::Value *dxilValue, DXIL::Operation op, DXIL::DXOp dxOpCode,
                               ShaderVariable &var, bool flushDenormInput, bool isLive) const;
  bool IsVariableAssigned(const Id id) const;

  ShaderVariable GetBuiltin(ShaderBuiltin builtin);

  struct AnnotationProperties
  {
    DXIL::ResourceKind resKind;
    DXIL::ResourceClass resClass;
    uint32_t structStride;
  };

  BuiltinInputs m_Builtins;

  Debugger &m_Debugger;
  const DXIL::Program &m_Program;
  const GlobalState &m_GlobalState;

  rdcarray<StackFrame *> m_Callstack;
  ShaderDebugState *m_State = NULL;

  ShaderVariable m_Input;
  GlobalVariable m_Output;

  // Known SSA ShaderVariables
  std::map<Id, ShaderVariable> m_Variables;
  // SSA Variables captured when a branch happens for use in phi nodes
  std::map<Id, ShaderVariable> m_PhiVariables;
  // Live variables at the current scope
  rdcarray<bool> m_Live;
  // Globals variables at the current scope
  rdcarray<bool> m_IsGlobal;
  // If the variable has been assigned a value
  rdcarray<bool> m_Assigned;
  // Annotated handle properties
  std::map<Id, AnnotationProperties> m_AnnotatedProperties;
  // ResourceReferenceInfo for any direct heap access bindings created using createHandleFromHeap
  std::map<Id, ResourceReferenceInfo> m_DirectHeapAccessBindings;

  const FunctionInfo *m_FunctionInfo = NULL;
  DXBC::ShaderType m_ShaderType;

  // Track memory allocations
  // For stack allocations do not bother freeing when leaving functions
  MemoryTracking m_Memory;

  // The instruction index within the current function
  uint32_t m_FunctionInstructionIdx = 0;
  const DXIL::Instruction *m_CurrentInstruction = NULL;
  // The current and previous function basic block index
  uint32_t m_Block = ~0U;
  uint32_t m_PreviousBlock = ~0U;
  // The global PC of the active instruction that was or will be executed on the current simulation step
  uint32_t m_ActiveGlobalInstructionIdx = 0;

  // SSA Ids guaranteed to be greater than 0 and less than this value
  uint32_t m_MaxSSAId;

  rdcarray<BindingSlot> m_accessedSRVs;
  rdcarray<BindingSlot> m_accessedUAVs;

  // quad ID (arbitrary, just used to find neighbours for derivatives)
  uint32_t m_QuadId = 0;
  // index in the pixel quad (relative to the active lane)
  uint32_t m_QuadLaneIndex = ~0U;
  // the lane indices of our quad neighbours
  rdcfixedarray<uint32_t, 4> m_QuadNeighbours = {~0U, ~0U, ~0U, ~0U};
  // index in the workgroup
  uint32_t m_WorkgroupIndex = ~0U;
  bool m_Dead = false;
  bool m_Ended = false;
  bool m_Helper = false;
};

struct GlobalState
{
  GlobalState() = default;
  ~GlobalState();
  BuiltinInputs builtins;

  struct ViewFmt
  {
    int byteWidth = 0;
    int numComps = 0;
    CompType compType = CompType::Typeless;
    int stride = 0;
  };

  struct ResourceInfo
  {
    ResourceInfo() : firstElement(0), numElements(0), isByteBuffer(false), isRootDescriptor(false)
    {
    }

    uint32_t firstElement;
    uint32_t numElements;

    bool isByteBuffer;
    bool isRootDescriptor;
    // Buffer stride is stored in format.stride
    ViewFmt format;
  };

  struct UAVData
  {
    UAVData() : tex(false), rowPitch(0), depthPitch(0), hiddenCounter(0) {}

    ResourceInfo resInfo;

    bytebuf data;
    bool tex;
    uint32_t rowPitch, depthPitch;

    uint32_t hiddenCounter;
  };

  std::map<BindingSlot, UAVData> uavs;
  typedef std::map<BindingSlot, UAVData>::const_iterator UAVIterator;

  struct SRVData
  {
    SRVData() {}

    ResourceInfo resInfo;
    bytebuf data;
  };

  std::map<BindingSlot, SRVData> srvs;
  typedef std::map<BindingSlot, SRVData>::const_iterator SRVIterator;

  // allocated storage for opaque uniform blocks, does not change over the course of debugging
  rdcarray<ShaderVariable> constantBlocks;
  rdcarray<bytebuf> constantBlocksData;

  // resources may be read-write but the variable itself doesn't change
  rdcarray<ShaderVariable> readOnlyResources;
  rdcarray<ShaderVariable> readWriteResources;
  rdcarray<ShaderVariable> samplers;
  // Globals across workgroup including inputs (immutable) and outputs (mutable)
  rdcarray<GlobalVariable> globals;
  // Constants across workgroup
  rdcarray<GlobalConstant> constants;
  // Memory created for global variables
  MemoryTracking memory;
};

struct LocalMapping
{
  bool operator<(const LocalMapping &o) const
  {
    if(sourceVarName != o.sourceVarName)
      return sourceVarName < o.sourceVarName;
    if(byteOffset != o.byteOffset)
      return byteOffset < o.byteOffset;
    if(countBytes != o.countBytes)
      return countBytes < o.countBytes;
    if(instIndex != o.instIndex)
      return instIndex < o.instIndex;
    if(isDeclare != o.isDeclare)
      return isDeclare;
    return debugVarSSAName < o.debugVarSSAName;
  }

  bool isSourceSupersetOf(const LocalMapping &o) const
  {
    // this mapping is a superset of the other if:

    // it's the same source var
    if(variable != o.variable)
      return false;

    if(sourceVarName != o.sourceVarName)
      return false;

    // it encompaases the other mapping
    if(byteOffset > o.byteOffset)
      return false;

    // countBytes = 0 means entire variable
    if(countBytes == 0)
      return true;

    if(o.countBytes == 0)
      return false;

    const int64_t thisEnd = byteOffset + countBytes;
    const int64_t otherEnd = o.byteOffset + o.countBytes;
    if(thisEnd < otherEnd)
      return false;

    return true;
  }

  const DXIL::DILocalVariable *variable;
  rdcstr sourceVarName;
  rdcstr debugVarSSAName;
  Id debugVarSSAId;
  int32_t byteOffset;
  uint32_t countBytes;
  uint32_t instIndex;
  bool isDeclare;
};

struct ScopedDebugData
{
  rdcarray<LocalMapping> localMappings;
  const DXIL::Metadata *md;
  ScopedDebugData *parent;
  rdcstr functionName;
  rdcstr fileName;
  uint32_t line;
  uint32_t maxInstruction;

  bool operator<(const ScopedDebugData &o) const { return line < o.line; }
};

struct TypeData
{
  const DXIL::Metadata *baseType = NULL;

  rdcarray<uint32_t> arrayDimensions;
  rdcarray<rdcpair<rdcstr, const DXIL::Metadata *>> structMembers;
  rdcarray<uint32_t> memberOffsets;

  rdcstr name;
  uint32_t sizeInBytes = 0;
  uint32_t alignInBytes = 0;

  VarType type = VarType::Unknown;
  uint32_t vecSize = 0;
  uint32_t matSize = 0;
  bool colMajorMat = false;
};

enum class ThreadProperty : uint32_t
{
  Helper,
  QuadId,
  QuadLane,
  Active,
  Count,
};

struct ThreadProperties
{
  rdcfixedarray<uint32_t, arraydim<ThreadProperty>()> props;

  uint32_t &operator[](ThreadProperty p)
  {
    if(p >= ThreadProperty::Count)
      return props[0];
    return props[(uint32_t)p];
  }

  uint32_t operator[](ThreadProperty p) const
  {
    if(p >= ThreadProperty::Count)
      return 0;
    return props[(uint32_t)p];
  }
};

class Debugger : public DXBCContainerDebugger
{
public:
  Debugger() : DXBCContainerDebugger(true){};
  ShaderDebugTrace *BeginDebug(uint32_t eventId, const DXBC::DXBCContainer *dxbcContainer,
                               const ShaderReflection &reflection, uint32_t activeLaneIndex,
                               uint32_t workgroupSize);
  void InitialiseWorkgroup(const rdcarray<ThreadProperties> &workgroupProperties);
  rdcarray<ShaderDebugState> ContinueDebug(DebugAPIWrapper *apiWrapper);
  GlobalState &GetGlobalState() { return m_GlobalState; }
  ThreadState &GetActiveLane() { return m_Workgroup[m_ActiveLaneIndex]; }
  ThreadState &GetLane(const uint32_t i) { return m_Workgroup[i]; }
  rdcarray<ThreadState> &GetWorkgroup() { return m_Workgroup; }
  const rdcarray<bool> &GetLiveGlobals() { return m_LiveGlobals; }
  static rdcstr GetResourceReferenceName(const DXIL::Program *program, DXIL::ResourceClass resClass,
                                         const BindingSlot &slot);
  const DXIL::Program &GetProgram() const { return *m_Program; }
  uint32_t GetEventId() { return m_EventId; }
  const FunctionInfo *GetFunctionInfo(const DXIL::Function *function) const;
  const rdcarray<DXIL::EntryPointInterface::Signature> &GetDXILEntryPointInputs(void) const
  {
    return m_EntryPointInterface->inputs;
  }

private:
  void CalcActiveMask(rdcarray<bool> &activeMask);
  void ParseDbgOpDeclare(const DXIL::Instruction &inst, uint32_t instructionIndex);
  void ParseDbgOpValue(const DXIL::Instruction &inst, uint32_t instructionIndex);
  const DXIL::Metadata *GetMDScope(const DXIL::Metadata *scopeMD) const;
  ScopedDebugData *AddScopedDebugData(const DXIL::Metadata *scopeMD);
  ScopedDebugData *FindScopedDebugData(const DXIL::Metadata *md) const;
  const TypeData &AddDebugType(const DXIL::Metadata *typeMD);
  void AddLocalVariable(const DXIL::SourceMappingInfo &srcMapping, uint32_t instructionIndex);
  void ParseDebugData();

  rdcarray<ThreadState> m_Workgroup;
  std::map<const DXIL::Function *, FunctionInfo> m_FunctionInfos;

  // the live mutable global variables, to initialise a stack frame's live list
  rdcarray<bool> m_LiveGlobals;

  GlobalState m_GlobalState;

  struct DebugInfo
  {
    ~DebugInfo();
    rdcarray<ScopedDebugData *> scopedDebugDatas;
    std::map<const DXIL::DILocalVariable *, LocalMapping> locals;
    std::map<const DXIL::Metadata *, TypeData> types;
  } m_DebugInfo;

  const DXIL::Program *m_Program = NULL;
  const DXIL::Function *m_EntryPointFunction = NULL;
  const DXIL::EntryPointInterface *m_EntryPointInterface = NULL;
  ShaderStage m_Stage;

  uint32_t m_EventId = 0;
  uint32_t m_ActiveLaneIndex = 0;
  int m_Steps = 0;
};

};    // namespace DXILDebug
