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

#ifndef GKO_CORE_TEST_UTILS_MATRIX_GENERATOR_HPP_
#define GKO_CORE_TEST_UTILS_MATRIX_GENERATOR_HPP_


#include <algorithm>
#include <iterator>
#include <numeric>
#include <random>
#include <type_traits>
#include <vector>


#include <ginkgo/core/base/math.hpp>
#include <ginkgo/core/base/utils.hpp>
#include <ginkgo/core/matrix/dense.hpp>


#include "core/test/utils/value_generator.hpp"


namespace gko {
namespace test {


/**
 * Generates matrix data for a random matrix.
 *
 * @tparam ValueType  the type for matrix values
 * @tparam IndexType  the type for row and column indices
 * @tparam NonzeroDistribution  type of nonzero distribution
 * @tparam ValueDistribution  type of value distribution
 * @tparam Engine  type of random engine
 *
 * @param num_rows  number of rows
 * @param num_cols  number of columns
 * @param nonzero_dist  distribution of nonzeros per row
 * @param value_dist  distribution of matrix values
 * @param engine  a random engine
 *
 * @return the generated matrix_data with entries according to the given
 *         dimensions and nonzero count and value distributions.
 */
template <typename ValueType, typename IndexType, typename NonzeroDistribution,
          typename ValueDistribution, typename Engine>
matrix_data<ValueType, IndexType> generate_random_matrix_data(
    size_type num_rows, size_type num_cols, NonzeroDistribution&& nonzero_dist,
    ValueDistribution&& value_dist, Engine&& engine)
{
    using std::begin;
    using std::end;

    matrix_data<ValueType, IndexType> data{gko::dim<2>{num_rows, num_cols}, {}};

    std::vector<bool> present_cols(num_cols);

    for (IndexType row = 0; row < num_rows; ++row) {
        // randomly generate number of nonzeros in this row
        const auto nnz_in_row = std::max(
            size_type(0),
            std::min(static_cast<size_type>(nonzero_dist(engine)), num_cols));
        std::uniform_int_distribution<IndexType> col_dist{
            0, static_cast<IndexType>(num_cols) - 1};
        if (nnz_in_row > num_cols / 2) {
            present_cols.assign(num_cols, true);
            // remove num_cols - nnz_in_row entries from present_cols
            size_type count = num_cols;
            while (count > nnz_in_row) {
                const auto new_col = col_dist(engine);
                if (present_cols[new_col]) {
                    present_cols[new_col] = false;
                    count--;
                }
            }
            for (IndexType col = 0; col < num_cols; col++) {
                if (present_cols[col]) {
                    data.nonzeros.emplace_back(
                        row, col,
                        detail::get_rand_value<ValueType>(value_dist, engine));
                }
            }
        } else {
            // add nnz_in_row entries to present_cols
            present_cols.assign(num_cols, false);
            size_type count = 0;
            while (count < nnz_in_row) {
                const auto col = col_dist(engine);
                if (!present_cols[col]) {
                    present_cols[col] = true;
                    count++;
                    data.nonzeros.emplace_back(
                        row, col,
                        detail::get_rand_value<ValueType>(value_dist, engine));
                }
            }
        }
    }

    data.ensure_row_major_order();
    return data;
}


/**
 * Generates device matrix data for a random matrix.
 *
 * @see generate_random_matrix_data
 */
template <typename ValueType, typename IndexType, typename NonzeroDistribution,
          typename ValueDistribution, typename Engine>
gko::device_matrix_data<ValueType, IndexType>
generate_random_device_matrix_data(gko::size_type num_rows,
                                   gko::size_type num_cols,
                                   NonzeroDistribution&& nonzero_dist,
                                   ValueDistribution&& value_dist,
                                   Engine&& engine,
                                   std::shared_ptr<const gko::Executor> exec)
{
    auto md = gko::test::generate_random_matrix_data<ValueType, IndexType>(
        num_rows, num_cols, std::forward<NonzeroDistribution>(nonzero_dist),
        std::forward<ValueDistribution>(value_dist),
        std::forward<Engine>(engine));
    return gko::device_matrix_data<ValueType, IndexType>::create_from_host(exec,
                                                                           md);
}


/**
 * Generates a random matrix.
 *
 * @tparam MatrixType  type of matrix to generate (must implement
 *                     the interface `ReadableFromMatrixData<>` and provide
 *                     matching `value_type` and `index_type` type aliases)
 *
 * @param exec  executor where the matrix should be allocated
 * @param args  additional arguments for the matrix constructor
 *
 * The other (template) parameters match generate_random_matrix_data.
 *
 * @return the unique pointer of MatrixType
 */
template <typename MatrixType = matrix::Dense<>, typename NonzeroDistribution,
          typename ValueDistribution, typename Engine, typename... MatrixArgs>
std::unique_ptr<MatrixType> generate_random_matrix(
    size_type num_rows, size_type num_cols, NonzeroDistribution&& nonzero_dist,
    ValueDistribution&& value_dist, Engine&& engine,
    std::shared_ptr<const Executor> exec, MatrixArgs&&... args)
{
    using value_type = typename MatrixType::value_type;
    using index_type = typename MatrixType::index_type;

    auto result = MatrixType::create(exec, std::forward<MatrixArgs>(args)...);
    result->read(generate_random_matrix_data<value_type, index_type>(
        num_rows, num_cols, std::forward<NonzeroDistribution>(nonzero_dist),
        std::forward<ValueDistribution>(value_dist),
        std::forward<Engine>(engine)));
    return result;
}


/**
 * Generates a random dense matrix.
 *
 * @tparam ValueType  value type of the generated matrix
 * @tparam ValueDistribution  type of value distribution
 * @tparam Engine  type of random engine
 *
 * @param num_rows  number of rows
 * @param num_cols  number of columns
 * @param value_dist  distribution of matrix values
 * @param engine  a random engine
 * @param exec  executor where the matrix should be allocated
 * @param args  additional arguments for the matrix constructor
 *
 * @return the unique pointer of gko::matrix::Dense<ValueType>
 */
template <typename ValueType, typename ValueDistribution, typename Engine,
          typename... MatrixArgs>
std::unique_ptr<gko::matrix::Dense<ValueType>> generate_random_dense_matrix(
    size_type num_rows, size_type num_cols, ValueDistribution&& value_dist,
    Engine&& engine, std::shared_ptr<const Executor> exec, MatrixArgs&&... args)
{
    auto result = gko::matrix::Dense<ValueType>::create(
        exec, gko::dim<2>{num_rows, num_cols},
        std::forward<MatrixArgs>(args)...);
    result->read(
        matrix_data<ValueType, int>{gko::dim<2>{num_rows, num_cols},
                                    std::forward<ValueDistribution>(value_dist),
                                    std::forward<Engine>(engine)});
    return result;
}


/**
 * Generates a random triangular matrix.
 *
 * @tparam ValueType  the type for matrix values
 * @tparam IndexType  the type for row and column indices
 * @tparam NonzeroDistribution  type of nonzero distribution
 * @tparam ValueDistribution  type of value distribution
 * @tparam Engine  type of random engine
 *
 * @param size  number of rows and columns
 * @param ones_on_diagonal  `true` generates only ones on the diagonal,
 *                          `false` generates random values on the diagonal
 * @param lower_triangular  `true` generates a lower triangular matrix,
 *                          `false` an upper triangular matrix
 * @param nonzero_dist  distribution of nonzeros per row
 * @param value_dist  distribution of matrix values
 * @param engine  a random engine
 *
 * @return the generated matrix_data with entries according to the given
 *         dimensions and nonzero count and value distributions.
 */
template <typename ValueType, typename IndexType, typename NonzeroDistribution,
          typename ValueDistribution, typename Engine>
matrix_data<ValueType, IndexType> generate_random_triangular_matrix_data(
    size_type size, bool ones_on_diagonal, bool lower_triangular,
    NonzeroDistribution&& nonzero_dist, ValueDistribution&& value_dist,
    Engine&& engine)
{
    using std::begin;
    using std::end;

    matrix_data<ValueType, IndexType> data{gko::dim<2>{size, size}, {}};

    std::vector<bool> present_cols(size);

    for (IndexType row = 0; row < size; ++row) {
        // randomly generate number of nonzeros in this row
        const auto min_col = lower_triangular ? 0 : row;
        const auto max_col =
            lower_triangular ? row : static_cast<IndexType>(size) - 1;
        const auto max_row_nnz = max_col - min_col + 1;
        const auto nnz_in_row = std::max(
            size_type(0), std::min(static_cast<size_type>(nonzero_dist(engine)),
                                   static_cast<size_type>(max_row_nnz)));
        std::uniform_int_distribution<IndexType> col_dist{min_col, max_col};
        if (nnz_in_row > max_row_nnz / 2) {
            present_cols.assign(size, true);
            // remove max_row_nnz - nnz_in_row entries from present_cols
            size_type count = max_row_nnz;
            while (count > nnz_in_row) {
                const auto new_col = col_dist(engine);
                if (present_cols[new_col]) {
                    present_cols[new_col] = false;
                    count--;
                }
            }
            for (auto col = min_col; col <= max_col; col++) {
                if (present_cols[col] || col == row) {
                    data.nonzeros.emplace_back(
                        row, col,
                        row == col && ones_on_diagonal
                            ? one<ValueType>()
                            : detail::get_rand_value<ValueType>(value_dist,
                                                                engine));
                }
            }
        } else {
            // add nnz_in_row entries to present_cols
            present_cols.assign(size, false);
            size_type count = 0;
            while (count < nnz_in_row) {
                const auto col = col_dist(engine);
                if (!present_cols[col]) {
                    present_cols[col] = true;
                    count++;
                    data.nonzeros.emplace_back(
                        row, col,
                        row == col && ones_on_diagonal
                            ? one<ValueType>()
                            : detail::get_rand_value<ValueType>(value_dist,
                                                                engine));
                }
            }
            if (!present_cols[row]) {
                data.nonzeros.emplace_back(
                    row, row,
                    ones_on_diagonal ? one<ValueType>()
                                     : detail::get_rand_value<ValueType>(
                                           value_dist, engine));
            }
        }
    }

    data.ensure_row_major_order();
    return data;
}


/**
 * Generates a random triangular matrix.
 *
 * @tparam MatrixType  type of matrix to generate (must implement
 *                     the interface `ReadableFromMatrixData<>` and provide
 *                     matching `value_type` and `index_type` type aliases)
 * @tparam NonzeroDistribution  type of nonzero distribution
 * @tparam ValueDistribution  type of value distribution
 * @tparam Engine  type of random engine
 *
 * @param size  number of rows and columns
 * @param ones_on_diagonal  `true` generates only ones on the diagonal,
 *                          `false` generates random values on the diagonal
 * @param lower_triangular  `true` generates a lower triangular matrix,
 *                          `false` an upper triangular matrix
 * @param nonzero_dist  distribution of nonzeros per row
 * @param value_dist  distribution of matrix values
 * @param engine  a random engine
 * @param exec  executor where the matrix should be allocated
 * @param args  additional arguments for the matrix constructor
 *
 * @return the unique pointer of MatrixType
 */
template <typename MatrixType = matrix::Dense<>, typename NonzeroDistribution,
          typename ValueDistribution, typename Engine, typename... MatrixArgs>
std::unique_ptr<MatrixType> generate_random_triangular_matrix(
    size_type size, bool ones_on_diagonal, bool lower_triangular,
    NonzeroDistribution&& nonzero_dist, ValueDistribution&& value_dist,
    Engine&& engine, std::shared_ptr<const Executor> exec, MatrixArgs&&... args)
{
    using value_type = typename MatrixType::value_type;
    using index_type = typename MatrixType::index_type;
    auto result = MatrixType::create(exec, std::forward<MatrixArgs>(args)...);
    result->read(generate_random_triangular_matrix_data<value_type, index_type>(
        size, ones_on_diagonal, lower_triangular,
        std::forward<NonzeroDistribution>(nonzero_dist),
        std::forward<ValueDistribution>(value_dist),
        std::forward<Engine>(engine)));
    return result;
}


/**
 * Generates a random lower triangular matrix.
 *
 * @tparam MatrixType  type of matrix to generate (must implement
 *                     the interface `ReadableFromMatrixData<>` and provide
 *                     matching `value_type` and `index_type` type aliases)
 * @tparam NonzeroDistribution  type of nonzero distribution
 * @tparam ValueDistribution  type of value distribution
 * @tparam Engine  type of random engine
 * @tparam MatrixArgs  the arguments from the matrix to be forwarded.
 *
 * @param size  number of rows and columns
 * @param ones_on_diagonal  `true` generates only ones on the diagonal,
 *                          `false` generates random values on the diagonal
 * @param nonzero_dist  distribution of nonzeros per row
 * @param value_dist  distribution of matrix values
 * @param engine  a random engine
 * @param exec  executor where the matrix should be allocated
 * @param args  additional arguments for the matrix constructor
 *
 * @return the unique pointer of MatrixType
 */
template <typename MatrixType = matrix::Dense<>, typename NonzeroDistribution,
          typename ValueDistribution, typename Engine, typename... MatrixArgs>
std::unique_ptr<MatrixType> generate_random_lower_triangular_matrix(
    size_type size, bool ones_on_diagonal, NonzeroDistribution&& nonzero_dist,
    ValueDistribution&& value_dist, Engine&& engine,
    std::shared_ptr<const Executor> exec, MatrixArgs&&... args)
{
    return generate_random_triangular_matrix<MatrixType>(
        size, ones_on_diagonal, true, nonzero_dist, value_dist, engine,
        std::move(exec), std::forward<MatrixArgs>(args)...);
}


/**
 * Generates a random upper triangular matrix.
 *
 * @tparam MatrixType  type of matrix to generate (must implement
 *                     the interface `ReadableFromMatrixData<>` and provide
 *                     matching `value_type` and `index_type` type aliases)
 * @tparam NonzeroDistribution  type of nonzero distribution
 * @tparam ValueDistribution  type of value distribution
 * @tparam Engine  type of random engine
 * @tparam MatrixArgs  the arguments from the matrix to be forwarded.
 *
 * @param size  number of rows and columns
 * @param ones_on_diagonal  `true` generates only ones on the diagonal,
 *                          `false` generates random values on the diagonal
 * @param nonzero_dist  distribution of nonzeros per row
 * @param value_dist  distribution of matrix values
 * @param engine  a random engine
 * @param exec  executor where the matrix should be allocated
 * @param args  additional arguments for the matrix constructor
 *
 * @return the unique pointer of MatrixType
 */
template <typename MatrixType = matrix::Dense<>, typename NonzeroDistribution,
          typename ValueDistribution, typename Engine, typename... MatrixArgs>
std::unique_ptr<MatrixType> generate_random_upper_triangular_matrix(
    size_type size, bool ones_on_diagonal, NonzeroDistribution&& nonzero_dist,
    ValueDistribution&& value_dist, Engine&& engine,
    std::shared_ptr<const Executor> exec, MatrixArgs&&... args)
{
    return generate_random_triangular_matrix<MatrixType>(
        size, ones_on_diagonal, false, nonzero_dist, value_dist, engine,
        std::move(exec), std::forward<MatrixArgs>(args)...);
}


/**
 * Generates a random square band matrix.
 *
 * @tparam ValueType  the type for matrix values
 * @tparam IndexType  the type for row and column indices
 * @tparam ValueDistribution  type of value distribution
 * @tparam Engine  type of random engine
 *
 * @param size  number of rows and columns
 * @param lower_bandwidth number of nonzeros in each row left of the main
 * diagonal
 * @param upper_bandwidth number of nonzeros in each row right of the main
 * diagonal
 * @param value_dist  distribution of matrix values
 * @param engine  a random engine
 *
 * @return the generated matrix_data with entries according to the given
 *         dimensions and nonzero count and value distributions.
 */
template <typename ValueType, typename IndexType, typename ValueDistribution,
          typename Engine, typename... MatrixArgs>
matrix_data<ValueType, IndexType> generate_random_band_matrix_data(
    size_type size, size_type lower_bandwidth, size_type upper_bandwidth,
    ValueDistribution&& value_dist, Engine&& engine)
{
    matrix_data<ValueType, IndexType> data{gko::dim<2>{size, size}, {}};
    for (size_type row = 0; row < size; ++row) {
        for (size_type col = row < lower_bandwidth ? 0 : row - lower_bandwidth;
             col <= std::min(row + upper_bandwidth, size - 1); col++) {
            auto val = detail::get_rand_value<ValueType>(value_dist, engine);
            data.nonzeros.emplace_back(row, col, val);
        }
    }
    return data;
}


/**
 * Generates a random banded matrix.
 *
 * @tparam MatrixType  type of matrix to generate (must implement
 *                     the interface `ReadableFromMatrixData<>` and provide
 *                     matching `value_type` and `index_type` type aliases)
 *
 * @param exec  executor where the matrix should be allocated
 * @param args  additional arguments for the matrix constructor
 *
 * The other (template) parameters match generate_random_band_matrix_data.
 *
 * @return the unique pointer of MatrixType
 */
template <typename MatrixType = matrix::Dense<>, typename ValueDistribution,
          typename Engine, typename... MatrixArgs>
std::unique_ptr<MatrixType> generate_random_band_matrix(
    size_type size, size_type lower_bandwidth, size_type upper_bandwidth,
    ValueDistribution&& value_dist, Engine&& engine,
    std::shared_ptr<const Executor> exec, MatrixArgs&&... args)
{
    using value_type = typename MatrixType::value_type;
    using index_type = typename MatrixType::index_type;
    auto result = MatrixType::create(exec, std::forward<MatrixArgs>(args)...);
    result->read(generate_random_band_matrix_data<value_type, index_type>(
        size, lower_bandwidth, upper_bandwidth,
        std::forward<ValueDistribution>(value_dist),
        std::forward<Engine>(engine)));
    return result;
}


}  // namespace test
}  // namespace gko


#endif  // GKO_CORE_TEST_UTILS_MATRIX_GENERATOR_HPP_
