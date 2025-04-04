/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2025 Baldur Karlsson
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

#include "api/replay/rdcarray.h"
#include "api/replay/rdcflatmap.h"
#include "os/os_specific.h"

namespace rdcshaders
{
class Tangle;
class ControlFlow;

typedef uint32_t ExecutionPoint;
typedef uint32_t ThreadIndex;
typedef rdcarray<Tangle> TangleGroup;
typedef rdcarray<ExecutionPoint> EnteredExecutionPoints;
typedef rdcflatmap<ThreadIndex, EnteredExecutionPoints> ThreadExecutionStates;

const ExecutionPoint INVALID_EXECUTION_POINT = (uint32_t)-1;
const uint32_t INVALID_THREAD_INDEX = (uint32_t)-1;

struct ThreadReference
{
  ExecutionPoint execPoint = INVALID_EXECUTION_POINT;
  ThreadIndex id = INVALID_THREAD_INDEX;
  bool m_Alive = true;
};

class Tangle
{
public:
  Tangle() = default;

  bool IsAliveActive() const { return m_Alive && m_Active; }
  ExecutionPoint GetExecutionPoint() const { return m_ThreadRefs[0].execPoint; }
  uint32_t GetThreadCount() const { return (uint32_t)m_ThreadRefs.count(); }
  bool ContainsThread(ThreadIndex threadId) const;
  const rdcarray<ThreadReference> &GetThreadRefs() const { return m_ThreadRefs; }
  void SetDiverged(bool value)
  {
    m_Diverged = value;
    m_StateChanged = true;
  }
  void SetThreadDead(ThreadIndex threadId)
  {
    SetThreadAlive(threadId, false);
    m_StateChanged = true;
  }
  void AddMergePoint(ExecutionPoint execPoint)
  {
    // only add a new merge point
    if(execPoint != m_MergePoints.back())
      m_MergePoints.push_back(execPoint);
    m_StateChanged = true;
  }
  void AddFunctionReturnPoint(ExecutionPoint execPoint)
  {
    m_MergePoints.push_back(execPoint);
    m_FunctionReturnPoints.push_back(execPoint);
    m_StateChanged = true;
  }
  bool IsAlive() const { return m_Alive; }

private:
  int32_t GetId() const { return m_Id; }
  void SetThreadExecutionPoint(ThreadIndex threadId, ExecutionPoint execPoint);
  bool Entangled(const Tangle &other) const;
  void AddThreadReference(const ThreadReference &threadRef)
  {
    m_ThreadRefs.push_back(threadRef);
    m_StateChanged = true;
  }
  void RemoveThreadReference(const ThreadIndex &threadId)
  {
    m_ThreadRefs.removeIf(
        [threadId](const ThreadReference &threadRef) { return threadRef.id == threadId; });
    m_StateChanged = true;
  }
  void SetThreadAlive(ThreadIndex threadId, bool value);
  void SetId(int32_t id) { m_Id = id; }
  void SetAlive(bool value)
  {
    if(m_Alive != value)
    {
      m_Alive = value;
      m_StateChanged = true;
    }
  }
  void SetActive(bool value)
  {
    if(m_Active != value)
    {
      m_Active = value;
      m_StateChanged = true;
    }
  }
  bool IsConverged() const { return m_Converged; }
  bool IsDiverged() const { return m_Diverged; }
  void SetConverged(bool value)
  {
    if(m_Converged != value)
    {
      m_Converged = value;
      m_StateChanged = true;
    }
  }

  bool IsStateChanged() const { return m_StateChanged; }
  void SetStateChanged(bool value) { m_StateChanged = value; }
  void AppendThreadReferences(const rdcarray<ThreadReference> &threadRefs)
  {
    m_ThreadRefs.append(threadRefs);
    m_StateChanged = true;
  }
  void ClearThreadReferences()
  {
    m_ThreadRefs.clear();
    m_StateChanged = true;
  }
  ExecutionPoint GetMergePoint() const { return m_MergePoints.back(); }
  void PopMergePoint(void)
  {
    m_MergePoints.pop_back();
    m_StateChanged = true;
  }
  const rdcarray<ExecutionPoint> &GetMergePoints(void) const { return m_MergePoints; }
  void ClearMergePoints(void)
  {
    m_MergePoints.clear();
    m_StateChanged = true;
  }
  void SetMergePoints(const rdcarray<ExecutionPoint> &points)
  {
    m_MergePoints = points;
    m_StateChanged = true;
  }
  ExecutionPoint GetFunctionReturnPoint() const { return m_FunctionReturnPoints.back(); }
  void PopFunctionReturnPoint(void)
  {
    m_FunctionReturnPoints.pop_back();
    m_StateChanged = true;
  }
  const rdcarray<ExecutionPoint> &GetFunctionReturnPoints(void) const
  {
    return m_FunctionReturnPoints;
  }
  void ClearFunctionReturnPoints(void)
  {
    m_FunctionReturnPoints.clear();
    m_StateChanged = true;
  }
  void SetFunctionReturnPoints(const rdcarray<ExecutionPoint> &points)
  {
    m_FunctionReturnPoints = points;
    m_StateChanged = true;
  }
  void PruneMergePoints(ExecutionPoint execPoint);
  void CheckForDivergence();

  rdcarray<ThreadReference> m_ThreadRefs;
  rdcarray<ExecutionPoint> m_MergePoints;
  rdcarray<ExecutionPoint> m_FunctionReturnPoints;
  int32_t m_Id = -1;
  bool m_Active = false;
  bool m_Alive = false;
  bool m_Diverged = false;
  bool m_Converged = false;
  bool m_StateChanged = false;

  friend ControlFlow;
};

class ControlFlow
{
public:
  void Construct(const rdcarray<ThreadIndex> &threadIds);
  TangleGroup &GetTangles() { return m_Tangles; }
  void UpdateState(const ThreadExecutionStates &threadExecutionStates);

private:
  TangleGroup DivergeTangle(Tangle &tangle);
  void ProcessTangleDivergence();
  void ProcessTangleDeactivation();
  void ActivateIndependentTangles();
  void ProcessTangleConvergence();
  void MergeConvergedTangles();
  int32_t GetNewTangleId() { return Atomic::Inc32(&s_NextTangleId); }

  TangleGroup m_Tangles;
  static int32_t s_NextTangleId;
};

};    // namespace rdcshaders
