//===--------- platform.cpp - OpenCL Adapter ---------------------------===//
//
// Copyright (C) 2023 Intel Corporation
//
// Part of the Unified-Runtime Project, under the Apache License v2.0 with LLVM
// Exceptions. See LICENSE.TXT
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "platform.hpp"
#include "adapter.hpp"

static cl_int mapURPlatformInfoToCL(ur_platform_info_t URPropName) {

  switch (URPropName) {
  case UR_PLATFORM_INFO_NAME:
    return CL_PLATFORM_NAME;
  case UR_PLATFORM_INFO_VENDOR_NAME:
    return CL_PLATFORM_VENDOR;
  case UR_PLATFORM_INFO_VERSION:
    return CL_PLATFORM_VERSION;
  case UR_PLATFORM_INFO_EXTENSIONS:
    return CL_PLATFORM_EXTENSIONS;
  case UR_PLATFORM_INFO_PROFILE:
    return CL_PLATFORM_PROFILE;
  default:
    return -1;
  }
}

UR_DLLEXPORT ur_result_t UR_APICALL
urPlatformGetInfo(ur_platform_handle_t hPlatform, ur_platform_info_t propName,
                  size_t propSize, void *pPropValue, size_t *pSizeRet) {

  UrReturnHelper ReturnValue(propSize, pPropValue, pSizeRet);
  const cl_int CLPropName = mapURPlatformInfoToCL(propName);

  switch (static_cast<uint32_t>(propName)) {
  case UR_PLATFORM_INFO_BACKEND:
    return ReturnValue(UR_PLATFORM_BACKEND_OPENCL);
  case UR_PLATFORM_INFO_NAME:
  case UR_PLATFORM_INFO_VENDOR_NAME:
  case UR_PLATFORM_INFO_VERSION:
  case UR_PLATFORM_INFO_EXTENSIONS:
  case UR_PLATFORM_INFO_PROFILE: {
    cl_platform_id Plat = hPlatform->CLPlatform;

    CL_RETURN_ON_FAILURE(
        clGetPlatformInfo(Plat, CLPropName, propSize, pPropValue, pSizeRet));

    return UR_RESULT_SUCCESS;
  }
  default:
    return UR_RESULT_ERROR_INVALID_ENUMERATION;
  }
}

UR_DLLEXPORT ur_result_t UR_APICALL
urPlatformGetApiVersion([[maybe_unused]] ur_platform_handle_t hPlatform,
                        ur_api_version_t *pVersion) {
  *pVersion = UR_API_VERSION_CURRENT;
  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT ur_result_t UR_APICALL urPlatformGet(
    ur_adapter_handle_t *phAdapters, uint32_t NumAdapters, uint32_t NumEntries,
    ur_platform_handle_t *phPlatforms, uint32_t *pNumPlatforms) {
  for (uint32_t idx = 0; idx < NumAdapters; idx++) {
    if (!(phAdapters[idx]->NumPlatforms)) {
      uint32_t NumPlatforms = 0;
      cl_int Res = clGetPlatformIDs(0, nullptr, &NumPlatforms);

      std::vector<cl_platform_id> CLPlatforms(NumPlatforms);
      Res = clGetPlatformIDs(static_cast<cl_uint>(NumPlatforms),
                             CLPlatforms.data(), nullptr);

      /* Absorb the CL_PLATFORM_NOT_FOUND_KHR and just return 0 in num_platforms
       */
      if (Res == CL_PLATFORM_NOT_FOUND_KHR) {
        if (pNumPlatforms) {
          *pNumPlatforms = 0;
          return UR_RESULT_SUCCESS;
        }
      }
      CL_RETURN_ON_FAILURE(Res);
      try {
        for (uint32_t i = 0; i < NumPlatforms; i++) {
          auto URPlatform =
              std::make_unique<ur_platform_handle_t_>(CLPlatforms[i]);
          phAdapters[idx]->URPlatforms.emplace_back(URPlatform.release());
        }
        phAdapters[idx]->NumPlatforms = NumPlatforms;
      } catch (std::bad_alloc &) {
        return UR_RESULT_ERROR_OUT_OF_RESOURCES;
      } catch (...) {
        return UR_RESULT_ERROR_INVALID_PLATFORM;
      }
    }
  }
  if (pNumPlatforms) {
    *pNumPlatforms = 0;
  }
  for (uint32_t idx = 0; idx < NumAdapters; idx++) {
    if (pNumPlatforms != nullptr) {
      *pNumPlatforms += phAdapters[idx]->NumPlatforms;
    }
    if (NumEntries && phPlatforms) {
      for (uint32_t i = 0; i < NumEntries; i++) {
        phPlatforms[i] = phAdapters[idx]->URPlatforms[i].get();
      }
    }
  }
  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT ur_result_t UR_APICALL urPlatformGetNativeHandle(
    ur_platform_handle_t hPlatform, ur_native_handle_t *phNativePlatform) {
  *phNativePlatform =
      reinterpret_cast<ur_native_handle_t>(hPlatform->CLPlatform);
  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT ur_result_t UR_APICALL urPlatformCreateWithNativeHandle(
    ur_native_handle_t hNativePlatform, ur_adapter_handle_t hAdapter,
    const ur_platform_native_properties_t *, ur_platform_handle_t *phPlatform) {
  cl_platform_id NativeHandle =
      reinterpret_cast<cl_platform_id>(hNativePlatform);

  uint32_t NumPlatforms = 0;
  UR_RETURN_ON_FAILURE(urPlatformGet(&hAdapter, 1, 0, nullptr, &NumPlatforms));
  std::vector<ur_platform_handle_t> Platforms(NumPlatforms);
  UR_RETURN_ON_FAILURE(
      urPlatformGet(&hAdapter, 1, NumPlatforms, Platforms.data(), nullptr));

  for (uint32_t i = 0; i < NumPlatforms; i++) {
    if (Platforms[i]->CLPlatform == NativeHandle) {
      *phPlatform = Platforms[i];
      return UR_RESULT_SUCCESS;
    }
  }
  return UR_RESULT_ERROR_INVALID_PLATFORM;
}

// Returns plugin specific backend option.
// Current support is only for optimization options.
// Return '-cl-opt-disable' for pFrontendOption = -O0 and '' for others.
UR_APIEXPORT ur_result_t UR_APICALL
urPlatformGetBackendOption(ur_platform_handle_t, const char *pFrontendOption,
                           const char **ppPlatformOption) {
  using namespace std::literals;
  if (pFrontendOption == nullptr)
    return UR_RESULT_SUCCESS;
  if (pFrontendOption == ""sv) {
    *ppPlatformOption = "";
    return UR_RESULT_SUCCESS;
  }
  // Return '-cl-opt-disable' for frontend_option = -O0 and '' for others.
  if (!strcmp(pFrontendOption, "-O0")) {
    *ppPlatformOption = "-cl-opt-disable";
    return UR_RESULT_SUCCESS;
  }
  if (pFrontendOption == "-O1"sv || pFrontendOption == "-O2"sv ||
      pFrontendOption == "-O3"sv) {
    *ppPlatformOption = "";
    return UR_RESULT_SUCCESS;
  }
  if (pFrontendOption == "-ftarget-compile-fast"sv) {
    *ppPlatformOption = "-igc_opts 'PartitionUnit=1,SubroutineThreshold=50000'";
    return UR_RESULT_SUCCESS;
  }
  return UR_RESULT_ERROR_INVALID_VALUE;
}
