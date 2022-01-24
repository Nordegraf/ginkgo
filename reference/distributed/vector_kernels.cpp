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

#include "core/distributed/vector_kernels.hpp"


#include "core/base/allocator.hpp"
#include "core/components/prefix_sum_kernels.hpp"

namespace gko {
namespace kernels {
namespace reference {
namespace distributed_vector {


template <typename ValueType, typename LocalIndexType, typename GlobalIndexType>
void build_local(
    std::shared_ptr<const DefaultExecutor> exec,
    const Array<matrix_data_entry<ValueType, GlobalIndexType>>& input,
    const distributed::Partition<LocalIndexType, GlobalIndexType>* partition,
    comm_index_type local_part,
    Array<matrix_data_entry<ValueType, LocalIndexType>>& local_data,
    ValueType deduction_help)
{
    auto input_data = input.get_const_data();
    auto range_bounds = partition->get_range_bounds();
    auto range_parts = partition->get_part_ids();
    auto range_starting_indices = partition->get_range_starting_indices();
    auto num_ranges = partition->get_num_ranges();

    auto find_range = [&](GlobalIndexType idx, size_type hint) {
        if (range_bounds[hint] <= idx && idx < range_bounds[hint + 1]) {
            return hint;
        } else {
            auto it = std::upper_bound(range_bounds + 1,
                                       range_bounds + num_ranges + 1, idx);
            return static_cast<size_type>(std::distance(range_bounds + 1, it));
        }
    };
    auto map_to_local = [&](GlobalIndexType idx,
                            size_type range_id) -> LocalIndexType {
        return static_cast<LocalIndexType>(idx - range_bounds[range_id]) +
               range_starting_indices[range_id];
    };

    size_type count{};
    size_type range_id_hint = 0;
    for (size_type i = 0; i < input.get_num_elems(); ++i) {
        auto entry = input_data[i];
        auto range_id = find_range(entry.row, range_id_hint);
        range_id_hint = range_id;
        auto part_id = range_parts[range_id];
        // skip non-local rows
        if (part_id == local_part) {
            count++;
        }
    }

    local_data.resize_and_reset(count);
    size_type idx{};
    for (size_type i = 0; i < input.get_num_elems(); ++i) {
        auto entry = input_data[i];
        auto range_id = find_range(entry.row, range_id_hint);
        range_id_hint = range_id;
        auto part_id = range_parts[range_id];
        // skip non-local rows
        if (part_id == local_part) {
            local_data.get_data()[idx] = {
                // map global row idx to local row idx
                map_to_local(entry.row, range_id),
                static_cast<LocalIndexType>(entry.column), entry.value};
            idx++;
        }
    }
}

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_LOCAL_GLOBAL_INDEX_TYPE(
    GKO_DECLARE_DISTRIBUTED_VECTOR_BUILD_LOCAL);


}  // namespace distributed_vector
}  // namespace reference
}  // namespace kernels
}  // namespace gko
