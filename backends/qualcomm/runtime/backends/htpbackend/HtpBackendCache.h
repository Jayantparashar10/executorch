/*
 * Copyright (c) Qualcomm Innovation Center, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */
#pragma once
#include <executorch/backends/qualcomm/runtime/backends/QnnBackendCache.h>

namespace executorch {
namespace backends {
namespace qnn {
class HtpBackendCache : public QnnBackendCache {
 public:
  explicit HtpBackendCache(
      const QnnExecuTorchContextBinary& qnn_context_blob,
      const std::string& aot_graph_name)
      : QnnBackendCache(qnn_context_blob, aot_graph_name), spill_fill_buf_(0) {}
  ~HtpBackendCache() override = default;

  uint64_t GetSpillFillBufferSize() {
    return spill_fill_buf_;
  }

 protected:
  executorch::runtime::Error RetrieveBackendBinaryInfo(
      const QnnSystemContext_BinaryInfo_t* binaryinfo) override;

 private:
  uint64_t spill_fill_buf_;
};
} // namespace qnn
} // namespace backends
} // namespace executorch
