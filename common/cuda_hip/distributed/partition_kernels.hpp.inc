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


namespace kernel {


template <typename LocalIndexType, typename GlobalIndexType>
void setup_sizes_ids_permutation(
    std::shared_ptr<const DefaultExecutor> exec, size_type num_ranges,
    comm_index_type num_parts, const GlobalIndexType* range_offsets,
    const comm_index_type* range_parts, array<LocalIndexType>& range_sizes,
    array<comm_index_type>& part_ids, array<GlobalIndexType>& permutation)
{
    run_kernel(
        exec,
        [] GKO_KERNEL(auto i, auto num_ranges, auto num_parts,
                      auto range_offsets, auto range_parts, auto range_sizes,
                      auto part_ids, auto permutation) {
            if (i == 0) {
                // set sentinel value at the end
                part_ids[num_ranges] = num_parts;
            }
            range_sizes[i] = range_offsets[i + 1] - range_offsets[i];
            part_ids[i] = range_parts[i];
            permutation[i] = static_cast<GlobalIndexType>(i);
        },
        num_ranges, num_ranges, num_parts, range_offsets, range_parts,
        range_sizes.get_data(), part_ids.get_data(), permutation.get_data());
}


template <typename LocalIndexType, typename GlobalIndexType>
void compute_part_sizes_and_starting_indices(
    std::shared_ptr<const DefaultExecutor> exec, size_type num_ranges,
    const array<LocalIndexType>& range_sizes,
    const array<comm_index_type>& part_ids,
    const array<GlobalIndexType>& permutation, LocalIndexType* starting_indices,
    LocalIndexType* part_sizes)
{
    run_kernel(
        exec,
        [] GKO_KERNEL(auto i, auto grouped_starting_indices,
                      auto grouped_part_ids, auto orig_idxs,
                      auto starting_indices, auto part_sizes) {
            auto prev_part = i > 0 ? grouped_part_ids[i - 1]
                                   : invalid_index<comm_index_type>();
            auto cur_part = grouped_part_ids[i];
            auto next_part =
                grouped_part_ids[i + 1];  // last element has to be num_parts
            if (cur_part != next_part) {
                part_sizes[cur_part] = grouped_starting_indices[i];
            }
            // write result shifted by one entry to get exclusive prefix sum
            starting_indices[orig_idxs[i]] =
                prev_part == cur_part ? grouped_starting_indices[i - 1]
                                      : LocalIndexType{};
        },
        num_ranges, range_sizes.get_const_data(), part_ids.get_const_data(),
        permutation.get_const_data(), starting_indices, part_sizes);
}


}  // namespace kernel


template <typename LocalIndexType, typename GlobalIndexType>
void build_starting_indices(std::shared_ptr<const DefaultExecutor> exec,
                            const GlobalIndexType* range_offsets,
                            const comm_index_type* range_parts,
                            size_type num_ranges, comm_index_type num_parts,
                            comm_index_type& num_empty_parts,
                            LocalIndexType* starting_indices,
                            LocalIndexType* part_sizes)
{
    if (num_ranges > 0) {
        array<LocalIndexType> range_sizes{exec, num_ranges};
        // num_parts sentinel at the end
        array<comm_index_type> tmp_part_ids{exec, num_ranges + 1};
        array<GlobalIndexType> permutation{exec, num_ranges};
        // set part_sizes to 0 in case of empty parts
        components::fill_array(exec, part_sizes, num_parts, LocalIndexType{});

        kernel::setup_sizes_ids_permutation(
            exec, num_ranges, num_parts, range_offsets, range_parts,
            range_sizes, tmp_part_ids, permutation);

        auto tmp_part_id_ptr =
            thrust::device_pointer_cast(tmp_part_ids.get_data());
        auto range_sizes_ptr =
            thrust::device_pointer_cast(range_sizes.get_data());
        auto permutation_ptr =
            thrust::device_pointer_cast(permutation.get_data());
        auto value_it = thrust::make_zip_iterator(
            thrust::make_tuple(range_sizes_ptr, permutation_ptr));
        // group range_sizes by part ID
        thrust::stable_sort_by_key(thrust::device, tmp_part_id_ptr,
                                   tmp_part_id_ptr + num_ranges, value_it);
        // compute inclusive prefix sum for each part
        thrust::inclusive_scan_by_key(thrust::device, tmp_part_id_ptr,
                                      tmp_part_id_ptr + num_ranges,
                                      range_sizes_ptr, range_sizes_ptr);
        // write back the results
        kernel::compute_part_sizes_and_starting_indices(
            exec, num_ranges, range_sizes, tmp_part_ids, permutation,
            starting_indices, part_sizes);
        num_empty_parts = thrust::count(thrust::device, part_sizes,
                                        part_sizes + num_parts, 0);
    } else {
        num_empty_parts = num_parts;
    }
}

GKO_INSTANTIATE_FOR_EACH_LOCAL_GLOBAL_INDEX_TYPE(
    GKO_DECLARE_PARTITION_BUILD_STARTING_INDICES);
