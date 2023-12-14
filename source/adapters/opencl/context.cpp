//===--------- context.cpp - OpenCL Adapter ---------------------------===//
//
// Copyright (C) 2023 Intel Corporation
//
// Part of the Unified-Runtime Project, under the Apache License v2.0 with LLVM
// Exceptions. See LICENSE.TXT
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "context.hpp"

#include <mutex>
#include <set>
#include <unordered_map>

ur_result_t cl_adapter::getDevicesFromContext(
    ur_context_handle_t hContext,
    std::unique_ptr<std::vector<cl_device_id>> &DevicesInCtx) {

  cl_uint DeviceCount = hContext->DeviceCount;

  if (DeviceCount < 1) {
    return UR_RESULT_ERROR_INVALID_CONTEXT;
  }

  DevicesInCtx = std::make_unique<std::vector<cl_device_id>>(DeviceCount);
  for (size_t i = 0; i < DeviceCount; i++) {
    (*DevicesInCtx)[i] = hContext->Devices[i]->get();
  }

  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT ur_result_t UR_APICALL urContextCreate(
    uint32_t DeviceCount, const ur_device_handle_t *phDevices,
    const ur_context_properties_t *, ur_context_handle_t *phContext) {

  cl_int Ret;
  std::vector<cl_device_id> CLDevices(DeviceCount);
  for (size_t i = 0; i < DeviceCount; i++) {
    CLDevices[i] = phDevices[i]->get();
  }

  cl_context Ctx = clCreateContext(
      nullptr, cl_adapter::cast<cl_uint>(DeviceCount), CLDevices.data(),
      nullptr, nullptr, cl_adapter::cast<cl_int *>(&Ret));
  auto URContext =
      std::make_unique<ur_context_handle_t_>(Ctx, DeviceCount, phDevices);
  *phContext = URContext.release();
  return mapCLErrorToUR(Ret);
}

static cl_int mapURContextInfoToCL(ur_context_info_t URPropName) {

  cl_int CLPropName;
  switch (URPropName) {
  case UR_CONTEXT_INFO_NUM_DEVICES:
    CLPropName = CL_CONTEXT_NUM_DEVICES;
    break;
  case UR_CONTEXT_INFO_DEVICES:
    CLPropName = CL_CONTEXT_DEVICES;
    break;
  case UR_CONTEXT_INFO_REFERENCE_COUNT:
    CLPropName = CL_CONTEXT_REFERENCE_COUNT;
    break;
  default:
    CLPropName = -1;
  }

  return CLPropName;
}

UR_APIEXPORT ur_result_t UR_APICALL
urContextGetInfo(ur_context_handle_t hContext, ur_context_info_t propName,
                 size_t propSize, void *pPropValue, size_t *pPropSizeRet) {

  UrReturnHelper ReturnValue(propSize, pPropValue, pPropSizeRet);
  const cl_int CLPropName = mapURContextInfoToCL(propName);

  switch (static_cast<uint32_t>(propName)) {
  /* 2D USM memops are not supported. */
  case UR_CONTEXT_INFO_USM_MEMCPY2D_SUPPORT:
  case UR_CONTEXT_INFO_USM_FILL2D_SUPPORT: {
    return ReturnValue(false);
  }
  case UR_CONTEXT_INFO_ATOMIC_MEMORY_ORDER_CAPABILITIES:
  case UR_CONTEXT_INFO_ATOMIC_MEMORY_SCOPE_CAPABILITIES:
  case UR_CONTEXT_INFO_ATOMIC_FENCE_ORDER_CAPABILITIES:
  case UR_CONTEXT_INFO_ATOMIC_FENCE_SCOPE_CAPABILITIES: {
    /* These queries should be dealt with in context_impl.cpp by calling the
     * queries of each device separately and building the intersection set. */
    return UR_RESULT_ERROR_INVALID_ARGUMENT;
  }
  case UR_CONTEXT_INFO_NUM_DEVICES: {
    return ReturnValue(hContext->DeviceCount);
  }
  case UR_CONTEXT_INFO_DEVICES: {
    return ReturnValue(&hContext->Devices[0], hContext->DeviceCount);
  }
  case UR_CONTEXT_INFO_REFERENCE_COUNT: {
    size_t CheckPropSize = 0;
    auto ClResult = clGetContextInfo(hContext->get(), CLPropName, propSize,
                                     pPropValue, &CheckPropSize);
    if (pPropValue && CheckPropSize != propSize) {
      return UR_RESULT_ERROR_INVALID_SIZE;
    }
    CL_RETURN_ON_FAILURE(ClResult);
    if (pPropSizeRet) {
      *pPropSizeRet = CheckPropSize;
    }
    return UR_RESULT_SUCCESS;
  }
  default:
    return UR_RESULT_ERROR_INVALID_ENUMERATION;
  }
}

UR_APIEXPORT ur_result_t UR_APICALL
urContextRelease(ur_context_handle_t hContext) {

  cl_int Ret = clReleaseContext(hContext->get());
  return mapCLErrorToUR(Ret);
}

UR_APIEXPORT ur_result_t UR_APICALL
urContextRetain(ur_context_handle_t hContext) {

  cl_int Ret = clRetainContext(hContext->get());
  return mapCLErrorToUR(Ret);
}

UR_APIEXPORT ur_result_t UR_APICALL urContextGetNativeHandle(
    ur_context_handle_t hContext, ur_native_handle_t *phNativeContext) {

  *phNativeContext = reinterpret_cast<ur_native_handle_t>(hContext->get());
  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT ur_result_t UR_APICALL urContextCreateWithNativeHandle(
    ur_native_handle_t hNativeContext, uint32_t numDevices, const ur_device_handle_t *phDevices,
    const ur_context_native_properties_t *pProperties, ur_context_handle_t *phContext) {

  cl_context NativeHandle = reinterpret_cast<cl_context>(hNativeContext);
  auto URContext = std::make_unique<ur_context_handle_t_>(
      NativeHandle, numDevices, phDevices);
  *phContext = URContext.release();
  if (!pProperties || !pProperties->isNativeHandleOwned) {
    return clRetainContext(NativeHandle);
  }
  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT ur_result_t UR_APICALL urContextSetExtendedDeleter(
    ur_context_handle_t hContext, ur_context_extended_deleter_t pfnDeleter,
    void *pUserData) {
  static std::unordered_map<ur_context_handle_t,
                            std::set<ur_context_extended_deleter_t>>
      ContextCallbackMap;
  static std::mutex ContextCallbackMutex;

  {
    std::lock_guard<std::mutex> Lock(ContextCallbackMutex);
    // Callbacks can only be registered once and we need to avoid double
    // allocating.
    if (ContextCallbackMap.count(hContext) &&
        ContextCallbackMap[hContext].count(pfnDeleter)) {
      return UR_RESULT_SUCCESS;
    }

    ContextCallbackMap[hContext].insert(pfnDeleter);
  }

  struct ContextCallback {
    void execute() {
      pfnDeleter(pUserData);
      {
        std::lock_guard<std::mutex> Lock(*CallbackMutex);
        (*CallbackMap)[hContext].erase(pfnDeleter);
        if ((*CallbackMap)[hContext].empty()) {
          CallbackMap->erase(hContext);
        }
      }
      delete this;
    }
    ur_context_handle_t hContext;
    ur_context_extended_deleter_t pfnDeleter;
    void *pUserData;
    std::unordered_map<ur_context_handle_t,
                       std::set<ur_context_extended_deleter_t>> *CallbackMap;
    std::mutex *CallbackMutex;
  };
  auto Callback =
      new ContextCallback({hContext, pfnDeleter, pUserData, &ContextCallbackMap,
                           &ContextCallbackMutex});
  auto ClCallback = [](cl_context, void *pUserData) {
    auto *C = static_cast<ContextCallback *>(pUserData);
    C->execute();
  };
  CL_RETURN_ON_FAILURE(
      clSetContextDestructorCallback(hContext->get(), ClCallback, Callback));

  return UR_RESULT_SUCCESS;
}
