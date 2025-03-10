// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
// This file is copied from
// https://github.com/ClickHouse/ClickHouse/blob/master/src/Columns/ColumnNullable.h
// and modified by Doris

#pragma once

#include "vec/columns/column.h"
#include "vec/columns/column_impl.h"
#include "vec/columns/columns_number.h"
#include "vec/common/assert_cast.h"
#include "vec/common/typeid_cast.h"

namespace doris::vectorized {

using NullMap = ColumnUInt8::Container;
using ConstNullMapPtr = const NullMap*;

/// Class that specifies nullable columns. A nullable column represents
/// a column, which may have any type, provided with the possibility of
/// storing NULL values. For this purpose, a ColumNullable object stores
/// an ordinary column along with a special column, namely a byte map,
/// whose type is ColumnUInt8. The latter column indicates whether the
/// value of a given row is a NULL or not. Such a design is preferred
/// over a bitmap because columns are usually stored on disk as compressed
/// files. In this regard, using a bitmap instead of a byte map would
/// greatly complicate the implementation with little to no benefits.
class ColumnNullable final : public COWHelper<IColumn, ColumnNullable> {
private:
    friend class COWHelper<IColumn, ColumnNullable>;

    ColumnNullable(MutableColumnPtr&& nested_column_, MutableColumnPtr&& null_map_);
    ColumnNullable(const ColumnNullable&) = default;

public:
    /** Create immutable column using immutable arguments. This arguments may be shared with other columns.
      * Use IColumn::mutate in order to make mutable column and mutate shared nested columns.
      */
    using Base = COWHelper<IColumn, ColumnNullable>;
    static Ptr create(const ColumnPtr& nested_column_, const ColumnPtr& null_map_) {
        return ColumnNullable::create(nested_column_->assume_mutable(),
                                      null_map_->assume_mutable());
    }

    template <typename... Args,
              typename = typename std::enable_if<IsMutableColumns<Args...>::value>::type>
    static MutablePtr create(Args&&... args) {
        return Base::create(std::forward<Args>(args)...);
    }

    const char* get_family_name() const override { return "Nullable"; }
    std::string get_name() const override { return "Nullable(" + nested_column->get_name() + ")"; }
    MutableColumnPtr clone_resized(size_t size) const override;
    size_t size() const override { return nested_column->size(); }
    bool is_null_at(size_t n) const override {
        return assert_cast<const ColumnUInt8&>(*null_map).get_data()[n] != 0;
    }
    Field operator[](size_t n) const override;
    void get(size_t n, Field& res) const override;
    bool get_bool(size_t n) const override {
        return is_null_at(n) ? 0 : nested_column->get_bool(n);
    }
    UInt64 get64(size_t n) const override { return nested_column->get64(n); }
    StringRef get_data_at(size_t n) const override;

    /// Will insert null value if pos=nullptr
    void insert_data(const char* pos, size_t length) override;
    StringRef serialize_value_into_arena(size_t n, Arena& arena, char const*& begin) const override;
    const char* deserialize_and_insert_from_arena(const char* pos) override;
    void insert_range_from(const IColumn& src, size_t start, size_t length) override;
    void insert_indices_from(const IColumn& src, const int* indices_begin, const int* indices_end) override;
    void insert(const Field& x) override;
    void insert_from(const IColumn& src, size_t n) override;

    void insert_from_not_nullable(const IColumn& src, size_t n);
    void insert_range_from_not_nullable(const IColumn& src, size_t start, size_t length);
    void insert_many_from_not_nullable(const IColumn& src, size_t position, size_t length);

    void insert_default() override {
        get_nested_column().insert_default();
        get_null_map_data().push_back(1);
    }

    void insert_many_defaults(size_t length) override {
        get_nested_column().insert_many_defaults(length);
        get_null_map_data().resize_fill(get_null_map_data().size() + length, 1);
    }

    void insert_null_elements(int num) {
        get_nested_column().insert_many_defaults(num);
        get_null_map_column().insert_elements(1, num);
    }

    void pop_back(size_t n) override;
    ColumnPtr filter(const Filter& filt, ssize_t result_size_hint) const override;
    ColumnPtr filter_by_selector(const uint16_t* sel, size_t sel_size,
                                 ColumnPtr* ptr = nullptr) override;
    ColumnPtr permute(const Permutation& perm, size_t limit) const override;
    //    ColumnPtr index(const IColumn & indexes, size_t limit) const override;
    int compare_at(size_t n, size_t m, const IColumn& rhs_, int null_direction_hint) const override;
    void get_permutation(bool reverse, size_t limit, int null_direction_hint,
                         Permutation& res) const override;
    void reserve(size_t n) override;
    void resize(size_t n) override;
    size_t byte_size() const override;
    size_t allocated_bytes() const override;
    void protect() override;
    ColumnPtr replicate(const Offsets& replicate_offsets) const override;
    void replicate(const uint32_t* counts, size_t target_size, IColumn& column) const override;
    void update_hash_with_value(size_t n, SipHash& hash) const override;
    void get_extremes(Field& min, Field& max) const override;

    MutableColumns scatter(ColumnIndex num_columns, const Selector& selector) const override {
        return scatter_impl<ColumnNullable>(num_columns, selector);
    }

    //    void gather(ColumnGathererStream & gatherer_stream) override;

    void for_each_subcolumn(ColumnCallback callback) override {
        callback(nested_column);
        callback(null_map);
    }

    bool structure_equals(const IColumn& rhs) const override {
        if (auto rhs_nullable = typeid_cast<const ColumnNullable*>(&rhs))
            return nested_column->structure_equals(*rhs_nullable->nested_column);
        return false;
    }

    bool is_date_type() override { return get_nested_column().is_date_type(); }
    bool is_datetime_type() override { return get_nested_column().is_datetime_type(); }
    void set_date_type() override { get_nested_column().set_date_type(); }
    void set_datetime_type() override { get_nested_column().set_datetime_type(); }

    bool is_nullable() const override { return true; }
    bool is_bitmap() const override { return get_nested_column().is_bitmap(); }
    bool is_column_decimal() const override { return get_nested_column().is_column_decimal(); }
    bool is_column_string() const override { return get_nested_column().is_column_string(); }
    bool is_fixed_and_contiguous() const override { return false; }
    bool values_have_fixed_size() const override { return nested_column->values_have_fixed_size(); }
    size_t size_of_value_if_fixed() const override {
        return null_map->size_of_value_if_fixed() + nested_column->size_of_value_if_fixed();
    }
    bool only_null() const override { return nested_column->is_dummy(); }

    /// Return the column that represents values.
    IColumn& get_nested_column() { return *nested_column; }
    const IColumn& get_nested_column() const { return *nested_column; }

    const ColumnPtr& get_nested_column_ptr() const { return nested_column; }

    MutableColumnPtr get_nested_column_ptr() { return nested_column->assume_mutable(); }

    /// Return the column that represents the byte map.
    const ColumnPtr& get_null_map_column_ptr() const { return null_map; }

    ColumnUInt8& get_null_map_column() { return assert_cast<ColumnUInt8&>(*null_map); }
    const ColumnUInt8& get_null_map_column() const {
        return assert_cast<const ColumnUInt8&>(*null_map);
    }

    void clear() override {
        null_map->clear();
        nested_column->clear();
    }

    NullMap& get_null_map_data() { return get_null_map_column().get_data(); }
    const NullMap& get_null_map_data() const { return get_null_map_column().get_data(); }

    /// Apply the null byte map of a specified nullable column onto the
    /// null byte map of the current column by performing an element-wise OR
    /// between both byte maps. This method is used to determine the null byte
    /// map of the result column of a function taking one or more nullable
    /// columns.
    void apply_null_map(const ColumnNullable& other);
    void apply_null_map(const ColumnUInt8& map);
    void apply_negated_null_map(const ColumnUInt8& map);

    /// Check that size of null map equals to size of nested column.
    void check_consistency() const;

    bool has_null() const override { return has_null(get_null_map_data().size()); }

    bool has_null(size_t size) const override {
        const UInt8* null_pos = get_null_map_data().data();
        const UInt8* null_pos_end = get_null_map_data().data() + size;
#ifdef __SSE2__
        /** A slightly more optimized version.
        * Based on the assumption that often pieces of consecutive values
        *  completely pass or do not pass the filter.
        * Therefore, we will optimistically check the parts of `SIMD_BYTES` values.
        */
        static constexpr size_t SIMD_BYTES = 16;
        const __m128i zero16 = _mm_setzero_si128();
        const UInt8* null_end_sse = null_pos + size / SIMD_BYTES * SIMD_BYTES;

        while (null_pos < null_end_sse) {
            int mask = _mm_movemask_epi8(_mm_cmpgt_epi8(
                    _mm_loadu_si128(reinterpret_cast<const __m128i*>(null_pos)), zero16));

            if (0 != mask) {
                return true;
            }
            null_pos += SIMD_BYTES;
        }
#endif
        while (null_pos < null_pos_end) {
            if (*null_pos != 0) {
                return true;
            }
            null_pos++;
        }
        return false;
    }

    void replace_column_data(const IColumn& rhs, size_t row, size_t self_row = 0) override {
        DCHECK(size() > 1);
        const ColumnNullable& nullable_rhs = assert_cast<const ColumnNullable&>(rhs);
        null_map->replace_column_data(*nullable_rhs.null_map, row, self_row);

        if (!nullable_rhs.is_null_at(row)) {
            nested_column->replace_column_data(*nullable_rhs.nested_column, row, self_row);
        } else {
            nested_column->replace_column_data_default(self_row);
        }
    }

    void replace_column_data_default(size_t self_row = 0) override {
        LOG(FATAL) << "should not call the method in column nullable";
    }

private:
    WrappedPtr nested_column;
    WrappedPtr null_map;

    template <bool negative>
    void apply_null_map_impl(const ColumnUInt8& map);
};

ColumnPtr make_nullable(const ColumnPtr& column, bool is_nullable = false);

} // namespace doris::vectorized
