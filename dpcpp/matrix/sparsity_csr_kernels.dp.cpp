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

#include "core/matrix/sparsity_csr_kernels.hpp"


#include <CL/sycl.hpp>


#include <ginkgo/core/base/exception_helpers.hpp>


#include "accessor/reduced_row_major.hpp"
#include "core/base/mixed_precision_types.hpp"
#include "core/synthesizer/implementation_selection.hpp"
#include "dpcpp/base/config.hpp"
#include "dpcpp/base/dim3.dp.hpp"
#include "dpcpp/components/cooperative_groups.dp.hpp"
#include "dpcpp/components/reduction.dp.hpp"
#include "dpcpp/components/thread_ids.dp.hpp"
#include "dpcpp/components/uninitialized_array.hpp"


namespace gko {
namespace kernels {
namespace dpcpp {
/**
 * @brief The Compressed sparse row matrix format namespace.
 *
 * @ingroup sparsity
 */
namespace sparsity_csr {


constexpr int classical_overweight = 32;
constexpr int spmv_block_size = 128;


using classical_kernels = syn::value_list<int, 1>;


namespace kernel {


template <size_type subgroup_size, typename MatrixValueType,
          typename input_accessor, typename output_accessor, typename IndexType,
          typename Closure>
void device_classical_spmv(const size_type num_rows,
                           const MatrixValueType* __restrict__ val,
                           const IndexType* __restrict__ col_idxs,
                           const IndexType* __restrict__ row_ptrs,
                           acc::range<input_accessor> b,
                           acc::range<output_accessor> c, Closure scale,
                           sycl::nd_item<3> item_ct1)
{
    using arithmetic_type = typename output_accessor::arithmetic_type;
    auto subgroup_tile = group::tiled_partition<subgroup_size>(
        group::this_thread_block(item_ct1));
    const auto subrow = thread::get_subwarp_num_flat<subgroup_size>(item_ct1);
    const auto subid = subgroup_tile.thread_rank();
    const IndexType column_id = item_ct1.get_group(1);
    const arithmetic_type value = static_cast<arithmetic_type>(val[0]);
    auto row = thread::get_subwarp_id_flat<subgroup_size>(item_ct1);
    for (; row < num_rows; row += subrow) {
        const auto ind_end = row_ptrs[row + 1];
        arithmetic_type temp_val = zero<arithmetic_type>();
        for (auto ind = row_ptrs[row] + subid; ind < ind_end;
             ind += subgroup_size) {
            temp_val += value * b(col_idxs[ind], column_id);
        }
        auto subgroup_result = ::gko::kernels::dpcpp::reduce(
            subgroup_tile, temp_val,
            [](const arithmetic_type& a, const arithmetic_type& b) {
                return a + b;
            });
        // TODO: check the barrier
        subgroup_tile.sync();
        if (subid == 0) {
            c(row, column_id) = scale(subgroup_result, c(row, column_id));
        }
    }
}


template <size_type subgroup_size, typename MatrixValueType,
          typename input_accessor, typename output_accessor, typename IndexType>
void abstract_classical_spmv(const size_type num_rows,
                             const MatrixValueType* __restrict__ val,
                             const IndexType* __restrict__ col_idxs,
                             const IndexType* __restrict__ row_ptrs,
                             acc::range<input_accessor> b,
                             acc::range<output_accessor> c,
                             sycl::nd_item<3> item_ct1)
{
    using type = typename output_accessor::arithmetic_type;
    device_classical_spmv<subgroup_size>(
        num_rows, val, col_idxs, row_ptrs, b, c,
        [](const type& x, const type& y) { return x; }, item_ct1);
}

template <size_type subgroup_size, typename MatrixValueType,
          typename input_accessor, typename output_accessor, typename IndexType>
void abstract_classical_spmv(
    dim3 grid, dim3 block, size_type dynamic_shared_memory, sycl::queue* queue,
    const size_type num_rows, const MatrixValueType* val,
    const IndexType* col_idxs, const IndexType* row_ptrs,
    acc::range<input_accessor> b, acc::range<output_accessor> c)
{
    // only subgroup = 1, so does not need sycl::reqd_sub_group_size
    queue->parallel_for(
        sycl_nd_range(grid, block), [=](sycl::nd_item<3> item_ct1) {
            abstract_classical_spmv<subgroup_size>(num_rows, val, col_idxs,
                                                   row_ptrs, b, c, item_ct1);
        });
}


template <size_type subgroup_size, typename MatrixValueType,
          typename input_accessor, typename output_accessor, typename IndexType>
void abstract_classical_spmv(
    const size_type num_rows, const MatrixValueType* __restrict__ alpha,
    const MatrixValueType* __restrict__ val,
    const IndexType* __restrict__ col_idxs,
    const IndexType* __restrict__ row_ptrs, acc::range<input_accessor> b,
    const typename output_accessor::storage_type* __restrict__ beta,
    acc::range<output_accessor> c, sycl::nd_item<3> item_ct1)
{
    using type = typename output_accessor::arithmetic_type;
    const type alpha_val = static_cast<type>(alpha[0]);
    const type beta_val = static_cast<type>(beta[0]);
    device_classical_spmv<subgroup_size>(
        num_rows, val, col_idxs, row_ptrs, b, c,
        [&alpha_val, &beta_val](const type& x, const type& y) {
            return alpha_val * x + beta_val * y;
        },
        item_ct1);
}

template <size_type subgroup_size, typename MatrixValueType,
          typename input_accessor, typename output_accessor, typename IndexType>
void abstract_classical_spmv(
    dim3 grid, dim3 block, size_type dynamic_shared_memory, sycl::queue* queue,
    const size_type num_rows, const MatrixValueType* alpha,
    const MatrixValueType* val, const IndexType* col_idxs,
    const IndexType* row_ptrs, acc::range<input_accessor> b,
    const typename output_accessor::storage_type* beta,
    acc::range<output_accessor> c)
{
    // only subgroup = 1, so does not need sycl::reqd_sub_group_size
    queue->parallel_for(
        sycl_nd_range(grid, block), [=](sycl::nd_item<3> item_ct1) {
            abstract_classical_spmv<subgroup_size>(
                num_rows, alpha, val, col_idxs, row_ptrs, b, beta, c, item_ct1);
        });
}


}  // namespace kernel


namespace host_kernel {


template <int subgroup_size, typename MatrixValueType, typename InputValueType,
          typename OutputValueType, typename IndexType>
void classical_spmv(syn::value_list<int, subgroup_size>,
                    std::shared_ptr<const DpcppExecutor> exec,
                    const matrix::SparsityCsr<MatrixValueType, IndexType>* a,
                    const matrix::Dense<InputValueType>* b,
                    matrix::Dense<OutputValueType>* c,
                    const matrix::Dense<MatrixValueType>* alpha = nullptr,
                    const matrix::Dense<OutputValueType>* beta = nullptr)
{
    using arithmetic_type =
        highest_precision<InputValueType, OutputValueType, MatrixValueType>;
    using input_accessor =
        gko::acc::reduced_row_major<2, arithmetic_type, const InputValueType>;
    using output_accessor =
        gko::acc::reduced_row_major<2, arithmetic_type, OutputValueType>;
    constexpr int threads_per_cu = 7;
    const auto num_subgroup =
        exec->get_num_computing_units() * threads_per_cu * classical_overweight;
    const auto nsg_in_group = spmv_block_size / subgroup_size;
    const auto gridx =
        std::min(ceildiv(a->get_size()[0], spmv_block_size / subgroup_size),
                 int64(num_subgroup / nsg_in_group));
    const dim3 grid(gridx, b->get_size()[1]);
    const auto block = spmv_block_size;

    const auto b_vals = gko::acc::range<input_accessor>(
        std::array<acc::size_type, 2>{
            {static_cast<acc::size_type>(b->get_size()[0]),
             static_cast<acc::size_type>(b->get_size()[1])}},
        b->get_const_values(),
        std::array<acc::size_type, 1>{
            {static_cast<acc::size_type>(b->get_stride())}});
    auto c_vals = gko::acc::range<output_accessor>(
        std::array<acc::size_type, 2>{
            {static_cast<acc::size_type>(c->get_size()[0]),
             static_cast<acc::size_type>(c->get_size()[1])}},
        c->get_values(),
        std::array<acc::size_type, 1>{
            {static_cast<acc::size_type>(c->get_stride())}});
    if (c->get_size()[0] == 0 || c->get_size()[1] == 0) {
        // empty output: nothing to do
        return;
    }
    if (alpha == nullptr && beta == nullptr) {
        kernel::abstract_classical_spmv<subgroup_size>(
            grid, block, 0, exec->get_queue(), a->get_size()[0],
            a->get_const_value(), a->get_const_col_idxs(),
            a->get_const_row_ptrs(), b_vals, c_vals);
    } else if (alpha != nullptr && beta != nullptr) {
        kernel::abstract_classical_spmv<subgroup_size>(
            grid, block, 0, exec->get_queue(), a->get_size()[0],
            alpha->get_const_values(), a->get_const_value(),
            a->get_const_col_idxs(), a->get_const_row_ptrs(), b_vals,
            beta->get_const_values(), c_vals);
    } else {
        GKO_KERNEL_NOT_FOUND;
    }
}

GKO_ENABLE_IMPLEMENTATION_SELECTION(select_classical_spmv, classical_spmv);


}  // namespace host_kernel


template <typename MatrixValueType, typename InputValueType,
          typename OutputValueType, typename IndexType>
void spmv(std::shared_ptr<const DpcppExecutor> exec,
          const matrix::SparsityCsr<MatrixValueType, IndexType>* a,
          const matrix::Dense<InputValueType>* b,
          matrix::Dense<OutputValueType>* c)
{
    host_kernel::select_classical_spmv(
        classical_kernels(), [](int compiled_info) { return true; },
        syn::value_list<int>(), syn::type_list<>(), exec, a, b, c);
}

GKO_INSTANTIATE_FOR_EACH_MIXED_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_SPARSITY_CSR_SPMV_KERNEL);


template <typename MatrixValueType, typename InputValueType,
          typename OutputValueType, typename IndexType>
void advanced_spmv(std::shared_ptr<const DpcppExecutor> exec,
                   const matrix::Dense<MatrixValueType>* alpha,
                   const matrix::SparsityCsr<MatrixValueType, IndexType>* a,
                   const matrix::Dense<InputValueType>* b,
                   const matrix::Dense<OutputValueType>* beta,
                   matrix::Dense<OutputValueType>* c)
{
    host_kernel::select_classical_spmv(
        classical_kernels(), [](int compiled_info) { return true; },
        syn::value_list<int>(), syn::type_list<>(), exec, a, b, c, alpha, beta);
}

GKO_INSTANTIATE_FOR_EACH_MIXED_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_SPARSITY_CSR_ADVANCED_SPMV_KERNEL);


template <typename ValueType, typename IndexType>
void fill_in_dense(std::shared_ptr<const DefaultExecutor> exec,
                   const matrix::SparsityCsr<ValueType, IndexType>* input,
                   matrix::Dense<ValueType>* output) GKO_NOT_IMPLEMENTED;

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_SPARSITY_CSR_FILL_IN_DENSE_KERNEL);


template <typename ValueType, typename IndexType>
void count_num_diagonal_elements(
    std::shared_ptr<const DpcppExecutor> exec,
    const matrix::SparsityCsr<ValueType, IndexType>* matrix,
    size_type* num_diagonal_elements) GKO_NOT_IMPLEMENTED;

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_SPARSITY_CSR_COUNT_NUM_DIAGONAL_ELEMENTS_KERNEL);


template <typename ValueType, typename IndexType>
void remove_diagonal_elements(
    std::shared_ptr<const DpcppExecutor> exec, const IndexType* row_ptrs,
    const IndexType* col_idxs,
    matrix::SparsityCsr<ValueType, IndexType>* matrix) GKO_NOT_IMPLEMENTED;

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_SPARSITY_CSR_REMOVE_DIAGONAL_ELEMENTS_KERNEL);


template <typename ValueType, typename IndexType>
void transpose(std::shared_ptr<const DpcppExecutor> exec,
               const matrix::SparsityCsr<ValueType, IndexType>* orig,
               matrix::SparsityCsr<ValueType, IndexType>* trans)
    GKO_NOT_IMPLEMENTED;

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_SPARSITY_CSR_TRANSPOSE_KERNEL);


template <typename ValueType, typename IndexType>
void sort_by_column_index(std::shared_ptr<const DpcppExecutor> exec,
                          matrix::SparsityCsr<ValueType, IndexType>* to_sort)
    GKO_NOT_IMPLEMENTED;

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_SPARSITY_CSR_SORT_BY_COLUMN_INDEX);


template <typename ValueType, typename IndexType>
void is_sorted_by_column_index(
    std::shared_ptr<const DpcppExecutor> exec,
    const matrix::SparsityCsr<ValueType, IndexType>* to_check,
    bool* is_sorted) GKO_NOT_IMPLEMENTED;

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_SPARSITY_CSR_IS_SORTED_BY_COLUMN_INDEX);


}  // namespace sparsity_csr
}  // namespace dpcpp
}  // namespace kernels
}  // namespace gko
