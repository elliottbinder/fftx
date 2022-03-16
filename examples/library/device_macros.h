#ifndef DEVICE_MACROS_HEADER
#define DEVICE_MACROS_HEADER
#if defined(FFTX_HIP)
#include <hip/hip_runtime.h>
#include <hipfft.h>
#include "rocfft.h"
#define DEVICE_SUCCESS hipSuccess
#define DEVICE_EVENT_T hipEvent_t
#define DEVICE_EVENT_CREATE hipEventCreate
#define DEVICE_SET hipSetDevice
#define DEVICE_MALLOC hipMalloc
#define DEVICE_EVENT_RECORD hipEventRecord
#define DEVICE_EVENT_ELAPSED_TIME hipEventElapsedTime
#define DEVICE_SYNCHRONIZE hipDeviceSynchronize
#define DEVICE_EVENT_SYNCHRONIZE hipEventSynchronize
#define DEVICE_FREE hipFree
#define DEVICE_MEM_COPY hipMemcpy
#define MEM_COPY_DEVICE_TO_HOST hipMemcpyDeviceToHost
#define MEM_COPY_HOST_TO_DEVICE hipMemcpyHostToDevice
#define DEVICE_ERROR_T hipError_t
#define DEVICE_GET_LAST_ERROR hipGetLastError
#define DEVICE_GET_ERROR_STRING hipGetErrorString
#define DEVICE_FFT_TYPE hipfftType
#define DEVICE_FFT_RESULT hipfftResult
#define DEVICE_FFT_HANDLE hipfftHandle
#define DEVICE_FFT_PLAN3D hipfftPlan3d
#define DEVICE_FFT_EXECZ2Z hipfftExecZ2Z
#define DEVICE_FFT_EXECD2Z hipfftExecD2Z
#define DEVICE_FFT_EXECZ2D hipfftExecZ2D
#define DEVICE_FFT_DESTROY hipfftDestroy
#define DEVICE_FFT_DOUBLEREAL hipfftDoubleReal
#define DEVICE_FFT_DOUBLECOMPLEX hipfftDoubleComplex
#define DEVICE_FFT_Z2Z HIPFFT_Z2Z
#define DEVICE_FFT_D2Z HIPFFT_D2Z
#define DEVICE_FFT_Z2D HIPFFT_Z2D
#define DEVICE_FFT_SUCCESS HIPFFT_SUCCESS
#define DEVICE_FFT_FORWARD HIPFFT_FORWARD
#define DEVICE_FFT_INVERSE HIPFFT_BACKWARD
#elif defined(__CUDACC__)
#include <cufft.h>
#include <helper_cuda.h>
#define DEVICE_SUCCESS cudaSuccess
#define DEVICE_EVENT_T cudaEvent_t
#define DEVICE_EVENT_CREATE cudaEventCreate
#define DEVICE_SET cudaSetDevice
#define DEVICE_MALLOC cudaMalloc
#define DEVICE_EVENT_RECORD cudaEventRecord
#define DEVICE_EVENT_ELAPSED_TIME cudaEventElapsedTime
#define DEVICE_SYNCHRONIZE cudaDeviceSynchronize
#define DEVICE_EVENT_SYNCHRONIZE cudaEventSynchronize
#define DEVICE_FREE cudaFree
#define DEVICE_MEM_COPY cudaMemcpy
#define MEM_COPY_DEVICE_TO_HOST cudaMemcpyDeviceToHost
#define MEM_COPY_HOST_TO_DEVICE cudaMemcpyHostToDevice
#define DEVICE_ERROR_T cudaError_t
#define DEVICE_GET_LAST_ERROR cudaGetLastError
#define DEVICE_GET_ERROR_STRING cudaGetErrorString
#define DEVICE_FFT_TYPE cufftType
#define DEVICE_FFT_RESULT cufftResult
#define DEVICE_FFT_HANDLE cufftHandle
#define DEVICE_FFT_PLAN3D cufftPlan3d
#define DEVICE_FFT_EXECZ2Z cufftExecZ2Z
#define DEVICE_FFT_EXECD2Z cufftExecD2Z
#define DEVICE_FFT_EXECZ2D cufftExecZ2D
#define DEVICE_FFT_DESTROY cufftDestroy
#define DEVICE_FFT_DOUBLEREAL cufftDoubleReal
#define DEVICE_FFT_DOUBLECOMPLEX cufftDoubleComplex
#define DEVICE_FFT_Z2Z CUFFT_Z2Z
#define DEVICE_FFT_D2Z CUFFT_D2Z
#define DEVICE_FFT_Z2D CUFFT_Z2D
#define DEVICE_FFT_SUCCESS CUFFT_SUCCESS
#define DEVICE_FFT_FORWARD CUFFT_FORWARD
#define DEVICE_FFT_INVERSE CUFFT_INVERSE
#else
// neither CUDA nor HIP
#define DEVICE_SUCCESS 0
#endif
// Functions that are defined if and only if either CUDA or HIP.
#if defined(__CUDACC__) || defined(FFTX_HIP)
#include <iostream>
inline void DEVICE_CHECK_ERROR(DEVICE_ERROR_T a_rc)
{
  // There does not appear to be a HIP analogue.
#if defined(__CUDACC__)
  checkCudaErrors(a_rc);
#endif
  if (a_rc != DEVICE_SUCCESS)
    {
      std::cerr << "Failure with code " << a_rc
                << " meaning " << DEVICE_GET_ERROR_STRING(a_rc)
                << std::endl;
      exit(-1);
    }
}
// Example of use: DEVICE_CHECK(DEVICE_MEM_COPY(...), "memcpy at step 2");
inline void DEVICE_CHECK(DEVICE_ERROR_T a_rc, const std::string& a_name)
{
   if (a_rc != DEVICE_SUCCESS)
     {
        std::cerr << a_name << " failed with code " << a_rc
                  << " meaning " << DEVICE_GET_ERROR_STRING(a_rc)
                  << std::endl;
        exit(-1);
     }
}
// Example of use: DEVICE_FFT_CHECK(DEVICE_FFT_PLAN3D(...), "fftplan at step 3");
inline void DEVICE_FFT_CHECK(DEVICE_FFT_RESULT a_rc, const std::string& a_name)
{
   if (a_rc != DEVICE_FFT_SUCCESS)
     {
        // There does not appear to be a HIP analogue.
        std::cerr << a_name << " failed with code " << a_rc
#if defined(__CUDACC__)
                  << " meaning " << _cudaGetErrorEnum(a_rc)
#endif
                  << std::endl;
        exit(-1);
     }
}
#endif
#endif
