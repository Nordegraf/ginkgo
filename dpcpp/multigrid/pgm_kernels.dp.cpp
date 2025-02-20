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

// force-top: on
// oneDPL needs to be first to avoid issues with libstdc++ TBB impl
#include <oneapi/dpl/algorithm>
#include <oneapi/dpl/execution>
// force-top: off


#include "core/multigrid/pgm_kernels.hpp"


#include <memory>


#include <ginkgo/core/base/exception_helpers.hpp>
#include <ginkgo/core/base/math.hpp>
#include <ginkgo/core/multigrid/pgm.hpp>


namespace gko {
namespace kernels {
namespace dpcpp {
/**
 * @brief The PGM solver namespace.
 *
 * @ingroup pgm
 */
namespace pgm {


template <typename IndexType>
void sort_agg(std::shared_ptr<const DefaultExecutor> exec, IndexType num,
              IndexType* row_idxs, IndexType* col_idxs)
{
    auto policy =
        oneapi::dpl::execution::make_device_policy(*exec->get_queue());
    auto it = oneapi::dpl::make_zip_iterator(row_idxs, col_idxs);
    std::sort(policy, it, it + num, [](auto a, auto b) {
        return std::tie(std::get<0>(a), std::get<1>(a)) <
               std::tie(std::get<0>(b), std::get<1>(b));
    });
}

GKO_INSTANTIATE_FOR_EACH_INDEX_TYPE(GKO_DECLARE_PGM_SORT_AGG_KERNEL);


template <typename ValueType, typename IndexType>
void sort_row_major(std::shared_ptr<const DefaultExecutor> exec, size_type nnz,
                    IndexType* row_idxs, IndexType* col_idxs, ValueType* vals)
{
    auto policy =
        oneapi::dpl::execution::make_device_policy(*exec->get_queue());
    auto it = oneapi::dpl::make_zip_iterator(row_idxs, col_idxs, vals);
    // Because reduce_by_segment is not determinstic, so we do not need
    // stable_sort
    // TODO: If we have determinstic reduce_by_segment, it should be stable_sort
    std::sort(policy, it, it + nnz, [](auto a, auto b) {
        return std::tie(std::get<0>(a), std::get<1>(a)) <
               std::tie(std::get<0>(b), std::get<1>(b));
    });
}

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(GKO_DECLARE_PGM_SORT_ROW_MAJOR);


template <typename ValueType, typename IndexType>
class coarse_coo_policy {};


template <typename ValueType, typename IndexType>
void compute_coarse_coo(std::shared_ptr<const DefaultExecutor> exec,
                        size_type fine_nnz, const IndexType* row_idxs,
                        const IndexType* col_idxs, const ValueType* vals,
                        matrix::Coo<ValueType, IndexType>* coarse_coo)
{
    // WORKAROUND: reduce_by_segment needs unique policy. Otherwise, dpcpp
    // throws same mangled name error. Related:
    // https://github.com/oneapi-src/oneDPL/issues/507
    auto policy = oneapi::dpl::execution::make_device_policy<
        coarse_coo_policy<ValueType, IndexType>>(*exec->get_queue());
    auto key_it = oneapi::dpl::make_zip_iterator(row_idxs, col_idxs);

    auto coarse_key_it = oneapi::dpl::make_zip_iterator(
        coarse_coo->get_row_idxs(), coarse_coo->get_col_idxs());

    oneapi::dpl::reduce_by_segment(
        policy, key_it, key_it + fine_nnz, vals, coarse_key_it,
        coarse_coo->get_values(),
        [](auto a, auto b) {
            return std::tie(std::get<0>(a), std::get<1>(a)) ==
                   std::tie(std::get<0>(b), std::get<1>(b));
        },
        [](auto a, auto b) { return a + b; });
}

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_PGM_COMPUTE_COARSE_COO);


}  // namespace pgm
}  // namespace dpcpp
}  // namespace kernels
}  // namespace gko
