/*******************************<GINKGO LICENSE>******************************
Copyright (c) 2017-2022, the Ginkgo authors
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************<GINKGO LICENSE>*******************************/

#include "core/factorization/ilu_kernels.hpp"


#include <hip/hip_runtime.h>


#include <ginkgo/core/base/array.hpp>


#include "hip/base/hipsparse_bindings.hip.hpp"


namespace gko {
namespace kernels {
namespace hip {
/**
 * @brief The ilu factorization namespace.
 *
 * @ingroup factor
 */
namespace ilu_factorization {


template <typename ValueType, typename IndexType>
void compute_lu(std::shared_ptr<const DefaultExecutor> exec,
                matrix::Csr<ValueType, IndexType>* m)
{
    const auto id = exec->get_device_id();
    auto handle = exec->get_hipsparse_handle();
    auto desc = hipsparse::create_mat_descr();
    auto info = hipsparse::create_ilu0_info();

    // get buffer size for ILU
    IndexType num_rows = m->get_size()[0];
    IndexType nnz = m->get_num_stored_elements();
    size_type buffer_size{};
    hipsparse::ilu0_buffer_size(handle, num_rows, nnz, desc,
                                m->get_const_values(), m->get_const_row_ptrs(),
                                m->get_const_col_idxs(), info, buffer_size);

    array<char> buffer{exec, buffer_size};

    // set up ILU(0)
    hipsparse::ilu0_analysis(handle, num_rows, nnz, desc, m->get_const_values(),
                             m->get_const_row_ptrs(), m->get_const_col_idxs(),
                             info, HIPSPARSE_SOLVE_POLICY_USE_LEVEL,
                             buffer.get_data());

    hipsparse::ilu0(handle, num_rows, nnz, desc, m->get_values(),
                    m->get_const_row_ptrs(), m->get_const_col_idxs(), info,
                    HIPSPARSE_SOLVE_POLICY_USE_LEVEL, buffer.get_data());

    hipsparse::destroy_ilu0_info(info);
    hipsparse::destroy(desc);
}

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_ILU_COMPUTE_LU_KERNEL);


}  // namespace ilu_factorization
}  // namespace hip
}  // namespace kernels
}  // namespace gko
