// 2018, Patrick Wieschollek <mail@patwie.com>

#if GOOGLE_CUDA

#define EIGEN_USE_GPU

#include "matrix_add_op.h"

#if (TF_MAJOR_VERSION >= 1) && (TF_MINOR_VERSION >= 14)
#include "tensorflow/core/util/gpu_kernel_helper.h"
#include "tensorflow/core/util/gpu_launch_config.h"
#else  // TF >= 1.14.0
#include "tensorflow/core/util/cuda_kernel_helper.h"
#endif  // TF >= 1.14.0

namespace tensorflow {
namespace {

#if (TF_MAJOR_VERSION >= 1) && (TF_MINOR_VERSION >= 14)
using LaunchConfig = ::tensorflow::GpuLaunchConfig;
constexpr auto GetLaunchConfig = ::tensorflow::GetGpuLaunchConfig;
#else  // TF >= 1.14.0
using LaunchConfig = ::tensorflow::CudaLaunchConfig;
constexpr auto GetLaunchConfig = ::tensorflow::GetCudaLaunchConfig;
#define GpuGridRangeX CudaGridRangeX
#endif  // TF >= 1.14.0

template <typename T>
__global__ void forward(LaunchConfig cfg, T* __restrict__ Z, const int N,
                        const T* __restrict__ X, const T* __restrict__ Y,
                        const T bias) {
  // for (int i = blockIdx.x * blockDim.x + threadIdx.x; i < N; i += blockDim.x
  // * gridDim.x) {
  for (int i : GpuGridRangeX(cfg.virtual_thread_count)) {
    Z[i] = X[i] + Y[i] + (T)bias;
  }
}

template <typename T>
__global__ void backward(LaunchConfig cfg, const T* __restrict__ top_diff,
                         const int N, T* __restrict__ grad_matrixA,
                         T* __restrict__ grad_matrixB) {
  // for (int i = blockIdx.x * blockDim.x + threadIdx.x; i < N; i += blockDim.x
  // * gridDim.x) {
  for (int i : GpuGridRangeX(cfg.virtual_thread_count)) {
    grad_matrixA[i] = top_diff[i];
    grad_matrixB[i] = top_diff[i];
  }
}

}  // anonymous namespace

namespace functor {

template <typename Dtype>
struct MatrixAddFunctor<GPUDevice, Dtype> {
  static void launch(::tensorflow::OpKernelContext* context, const Tensor& mA_,
                     const Tensor& mB_, Tensor* mC_, Dtype bias) {
    const int N = mA_.NumElements();
    const GPUDevice& d = context->eigen_gpu_device();

    LaunchConfig cfg = GetLaunchConfig(N, d);

    forward<Dtype><<<cfg.block_count, cfg.thread_per_block, 0, d.stream()>>>(
        cfg, mC_->flat<Dtype>().data(), mA_.NumElements(),
        mA_.flat<Dtype>().data(), mB_.flat<Dtype>().data(), bias);

    if (!d.ok()) {
      context->SetStatus(
          tensorflow::errors::Internal("Failed launching MatrixAdd on GPU"));
    }
  }
};

template struct MatrixAddFunctor<GPUDevice, int>;
template struct MatrixAddFunctor<GPUDevice, float>;
template struct MatrixAddFunctor<GPUDevice, double>;

template <typename Dtype>
struct MatrixAddGrad<GPUDevice, Dtype> {
  static void launch(::tensorflow::OpKernelContext* context,
                     const Tensor& topdiff_, Tensor* grad_mA_,
                     Tensor* grad_mB_) {
    const int N = topdiff_.NumElements();
    const GPUDevice& d = context->eigen_gpu_device();

    LaunchConfig cfg = GetLaunchConfig(N, d);

    // // optional reset gradients before running a kernel
    // cudaMemset(grad_mA_->flat<Dtype>().data(), 0, N * sizeof(Dtype));
    // cudaMemset(grad_mB_->flat<Dtype>().data(), 0, N * sizeof(Dtype));

    // backward<Dtype>
    // <<< cfg.block_count, cfg.thread_per_block, 0,
    // context->eigen_gpu_device().stream() >>> (
    //   cfg,
    //   topdiff_.flat<Dtype>().data(),
    //   topdiff_.NumElements(),
    //   grad_mA_->flat<Dtype>().data(),
    //   grad_mB_->flat<Dtype>().data());

    // faster alternative to custom kernel (above)
    cudaMemcpy(grad_mA_->flat<Dtype>().data(), topdiff_.flat<Dtype>().data(),
               N * sizeof(Dtype), cudaMemcpyDeviceToDevice);
    cudaMemcpy(grad_mB_->flat<Dtype>().data(), topdiff_.flat<Dtype>().data(),
               N * sizeof(Dtype), cudaMemcpyDeviceToDevice);

    if (!d.ok()) {
      context->SetStatus(tensorflow::errors::Internal(
          "Failed launching MatrixAddGrad on GPU"));
    }
  }
};

template struct MatrixAddGrad<GPUDevice, float>;
template struct MatrixAddGrad<GPUDevice, double>;

}  // namespace functor
}  // namespace tensorflow

#endif  // GOOGLE_CUDA
