// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <executorch/backends/cadence/reference/kernels/kernels.h>
#include <executorch/backends/cadence/reference/operators/operators.h>

using executorch::runtime::getLeadingDims;

// Generate kernels that perform elementwise arithmetic on two quantized
// tensors. The tensors are either the same size, or the second tensor is a
// scalar.
#define DECLARE_POINTWISE_TENSOR_QUANTIZED_BINARY_OP(BINARY_FUNC_NAME, OP)    \
  template <typename T>                                                       \
  void BINARY_FUNC_NAME(                                                      \
      const ::executorch::aten::Tensor& X,                                    \
      float X_scale,                                                          \
      int32_t X_zero_point,                                                   \
      const ::executorch::aten::Tensor& Y,                                    \
      float Y_scale,                                                          \
      int32_t Y_zero_point,                                                   \
      float out_scale,                                                        \
      int32_t out_zero_point,                                                 \
      ::executorch::aten::Tensor& out) {                                      \
    const T* __restrict__ X_data = X.const_data_ptr<T>();                     \
    const T* __restrict__ Y_data = Y.const_data_ptr<T>();                     \
    T* __restrict__ out_data = out.mutable_data_ptr<T>();                     \
    size_t Y_numel = Y.numel();                                               \
    size_t X_numel = X.numel();                                               \
    float inv_out_scale = 1.0f / out_scale;                                   \
    /* Tensor that has the same element of X */                               \
    if (Y_numel == X_numel) {                                                 \
      for (size_t i = 0; i < X_numel; ++i) {                                  \
        float x = kernels::dequantize<T>(X_data[i], X_scale, X_zero_point);   \
        float y = kernels::dequantize<T>(Y_data[i], Y_scale, Y_zero_point);   \
        float z = x OP y;                                                     \
        out_data[i] = kernels::quantize<T>(z, inv_out_scale, out_zero_point); \
      }                                                                       \
    } /* if Y is a scalar Tensor */                                           \
    else if (Y_numel == 1) {                                                  \
      float y = kernels::dequantize<T>(Y_data[0], Y_scale, Y_zero_point);     \
      for (size_t i = 0; i < X_numel; ++i) {                                  \
        float x = kernels::dequantize<T>(X_data[i], X_scale, X_zero_point);   \
        float z = x OP y;                                                     \
        out_data[i] = kernels::quantize<T>(z, inv_out_scale, out_zero_point); \
      }                                                                       \
    } /* other broadcasting cases */                                          \
    else {                                                                    \
      ET_DCHECK_MSG(false, "Unsupported broadcasting");                       \
    }                                                                         \
  }

template <typename T>
inline __attribute__((always_inline)) void quantized_linear_per_tensor_(
    const ::executorch::aten::Tensor& src,
    const ::executorch::aten::Tensor& weight,
    const ::executorch::aten::Tensor& bias,
    const int64_t src_zero_point,
    const int64_t weight_zero_point,
    const int64_t out_multiplier,
    const int64_t out_shift,
    const int64_t out_zero_point,
    ::executorch::aten::Tensor& out) {
  // input comes in shape [leading_dims, in_dim]
  // weight comes in shape [out_dim, in_dim]
  // output comes in empty with shape [leading_dims, out_dim]
  // Perform matrix multiply (M x N) x (N x P)' => M x P
  const int64_t leading_dims = getLeadingDims(src, src.dim() - 1);
  const int64_t out_dim = weight.size(0); // = out_dim
  const int64_t in_dim = weight.size(1); // = in_dim

  const T* __restrict__ in_data = src.const_data_ptr<T>();
  const T* __restrict__ weight_data = weight.const_data_ptr<T>();
  const int32_t* __restrict__ bias_data = bias.const_data_ptr<int32_t>();
  T* __restrict__ out_data = out.mutable_data_ptr<T>();

  // Compute the requant_scale from out_multiplier and out_shift
  const float requant_scale =
      -out_multiplier * 1.0 / (1 << 31) * pow(2, out_shift);

  for (size_t i = 0; i < leading_dims; ++i) {
    for (size_t j = 0; j < out_dim; ++j) {
      int32_t sum = bias_data[j];
      for (size_t k = 0; k < in_dim; ++k) {
        int32_t x = (int32_t)in_data[i * in_dim + k] - src_zero_point;
        int32_t w =
            (int32_t)weight_data[j * in_dim + k] - (int32_t)weight_zero_point;
        sum += x * w;
      }
      out_data[i * out_dim + j] = ::impl::reference::kernels::quantize<T>(
          sum, requant_scale, out_zero_point);
    }
  }
}

template <typename T>
inline __attribute__((always_inline)) void quantized_linear_per_tensor_(
    const ::executorch::aten::Tensor& src,
    const ::executorch::aten::Tensor& weight,
    const ::executorch::aten::Tensor& bias,
    int64_t src_zero_point,
    const ::executorch::aten::Tensor& weight_zero_point_t,
    int64_t out_multiplier,
    int64_t out_shift,
    int64_t out_zero_point,
    ::executorch::aten::Tensor& out) {
  // Get the zero_point of weight.
  int32_t weight_zero_point = weight_zero_point_t.const_data_ptr<int32_t>()[0];
  quantized_linear_per_tensor_<T>(
      src,
      weight,
      bias,
      src_zero_point,
      weight_zero_point,
      out_multiplier,
      out_shift,
      out_zero_point,
      out);
}

template <typename T>
inline __attribute__((always_inline)) void quantized_linear_per_channel_(
    const ::executorch::aten::Tensor& src,
    const ::executorch::aten::Tensor& weight,
    const ::executorch::aten::Tensor& bias,
    int64_t src_zero_point,
    int64_t weight_zero_point,
    const ::executorch::aten::Tensor& out_multiplier,
    const ::executorch::aten::Tensor& out_shift,
    int64_t out_zero_point,
    ::executorch::aten::Tensor& out) {
  // input comes in shape [leading_dims, in_dim]
  // weight comes in shape [out_dim, in_dim]
  // output comes in empty with shape [leading_dims, out_dim]
  // Perform matrix multiply (M x N) x (N x P)' => M x P
  int64_t leading_dims = getLeadingDims(src, src.dim() - 1);
  const int64_t out_dim = weight.size(0); // = out_dim
  const int64_t in_dim = weight.size(1); // = in_dim

  const T* __restrict__ in_data = src.const_data_ptr<T>();
  const T* __restrict__ weight_data = weight.const_data_ptr<T>();
  const int32_t* __restrict__ bias_data = bias.const_data_ptr<int32_t>();
  T* __restrict__ out_data = out.mutable_data_ptr<T>();
  const int32_t* __restrict__ out_multiplier_data =
      out_multiplier.const_data_ptr<int32_t>();
  const int32_t* __restrict__ out_shift_data =
      out_shift.const_data_ptr<int32_t>();

  for (size_t i = 0; i < leading_dims; ++i) {
    for (size_t j = 0; j < out_dim; ++j) {
      int32_t sum = bias_data[j];
      for (size_t k = 0; k < in_dim; ++k) {
        int32_t x = (int32_t)in_data[i * in_dim + k] - src_zero_point;
        int32_t w =
            (int32_t)weight_data[j * in_dim + k] - (int32_t)weight_zero_point;
        sum += x * w;
      }
      // Compute the out_scale from out_multiplier and out_shift
      const float out_scale =
          -out_multiplier_data[j] * 1.0 / (1 << 31) * pow(2, out_shift_data[j]);
      out_data[i * out_dim + j] = ::impl::reference::kernels::quantize<T>(
          sum, out_scale, out_zero_point);
    }
  }
}

template <typename T>
inline __attribute__((always_inline)) void quantized_linear_(
    const ::executorch::aten::Tensor& src,
    const ::executorch::aten::Tensor& weight,
    const ::executorch::aten::Tensor& bias,
    int64_t src_zero_point,
    int64_t weight_zero_point,
    const ::executorch::aten::Tensor& out_multiplier,
    const ::executorch::aten::Tensor& out_shift,
    int64_t out_zero_point,
    ::executorch::aten::Tensor& out) {
  if (out_multiplier.numel() == 1) {
    // Use per-tensor quantization kernel.
    const int32_t* __restrict__ out_multiplier_data =
        out_multiplier.const_data_ptr<int32_t>();
    const int32_t* __restrict__ out_shift_data =
        out_shift.const_data_ptr<int32_t>();
    quantized_linear_per_tensor_<T>(
        src,
        weight,
        bias,
        src_zero_point,
        weight_zero_point,
        out_multiplier_data[0],
        out_shift_data[0],
        out_zero_point,
        out);
    return;
  }

  // Use per-channel quantization kernel.
  quantized_linear_per_channel_<T>(
      src,
      weight,
      bias,
      src_zero_point,
      weight_zero_point,
      out_multiplier,
      out_shift,
      out_zero_point,
      out);
}

template <typename T>
inline __attribute__((always_inline)) void quantized_linear_(
    const ::executorch::aten::Tensor& src,
    const ::executorch::aten::Tensor& weight,
    const ::executorch::aten::Tensor& bias,
    int64_t src_zero_point,
    const ::executorch::aten::Tensor& weight_zero_point_t,
    const ::executorch::aten::Tensor& out_multiplier,
    const ::executorch::aten::Tensor& out_shift,
    int64_t out_zero_point,
    ::executorch::aten::Tensor& out) {
  // Get the zero_point of weight.
  int32_t weight_zero_point = weight_zero_point_t.const_data_ptr<int32_t>()[0];
  quantized_linear_<T>(
      src,
      weight,
      bias,
      src_zero_point,
      weight_zero_point,
      out_multiplier,
      out_shift,
      out_zero_point,
      out);
}