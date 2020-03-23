//*****************************************************************************
// Copyright 2017-2020 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//*****************************************************************************

#include "ngraph/op/strided_slice.hpp"
#include "ngraph/attribute_visitor.hpp"
#include "ngraph/op/constant.hpp"
#include "ngraph/validation_util.hpp"

#include <algorithm>

using namespace std;
using namespace ngraph;

constexpr NodeTypeInfo op::v1::StridedSlice::type_info;

op::v1::StridedSlice::StridedSlice(const Output<Node>& data,
                                   const Output<Node>& begin,
                                   const Output<Node>& end,
                                   const Output<Node>& strides,
                                   const std::vector<int64_t>& begin_mask,
                                   const std::vector<int64_t>& end_mask,
                                   const std::vector<int64_t>& new_axis_mask,
                                   const std::vector<int64_t>& shrink_axis_mask,
                                   const std::vector<int64_t>& ellipsis_mask)
    : Op({data, begin, end, strides})
    , m_begin_mask{begin_mask}
    , m_end_mask{end_mask}
    , m_new_axis_mask{new_axis_mask}
    , m_shrink_axis_mask{shrink_axis_mask}
    , m_ellipsis_mask{ellipsis_mask}
{
    constructor_validate_and_infer_types();
}

shared_ptr<Node> op::v1::StridedSlice::calculate_default_strides(const Output<Node>& begin,
                                                                 const Output<Node>& end) const
{
    const auto begin_pshape = begin.get_partial_shape();
    const auto end_pshape = end.get_partial_shape();
    const bool is_begin_length_static =
        begin_pshape.rank().is_static() && begin_pshape.rank().get_length() == 1
            ? begin_pshape[0].is_static()
            : false;
    const bool is_end_length_static =
        end_pshape.rank().is_static() && end_pshape.rank().get_length() == 1
            ? end_pshape[0].is_static()
            : false;
    NODE_VALIDATION_CHECK(this,
                          is_begin_length_static || is_end_length_static,
                          "First dimension of begin or end inputs must be static in order to "
                          "calculate default strides value");

    size_t strides_length = 0;
    if (is_begin_length_static)
    {
        strides_length = begin_pshape[0].get_length();
    }
    else if (is_end_length_static)
    {
        strides_length = end_pshape[0].get_length();
    }

    return op::Constant::create(
        element::i64, Shape{strides_length}, vector<int64_t>(strides_length, 1));
}

op::v1::StridedSlice::StridedSlice(const Output<Node>& data,
                                   const Output<Node>& begin,
                                   const Output<Node>& end,
                                   const std::vector<int64_t>& begin_mask,
                                   const std::vector<int64_t>& end_mask,
                                   const std::vector<int64_t>& new_axis_mask,
                                   const std::vector<int64_t>& shrink_axis_mask,
                                   const std::vector<int64_t>& ellipsis_mask)
    : StridedSlice(data,
                   begin,
                   end,
                   calculate_default_strides(begin, end),
                   begin_mask,
                   end_mask,
                   new_axis_mask,
                   shrink_axis_mask,
                   ellipsis_mask)
{
}

bool ngraph::op::v1::StridedSlice::visit_attributes(AttributeVisitor& visitor)
{
    visitor.on_attribute("begin_mask", m_begin_mask);
    visitor.on_attribute("end_mask", m_end_mask);
    visitor.on_attribute("new_axis_mask", m_new_axis_mask);
    visitor.on_attribute("shrink_axis_mask", m_shrink_axis_mask);
    visitor.on_attribute("ellipsis_mask", m_ellipsis_mask);
    return true;
}

void op::v1::StridedSlice::validate_and_infer_types()
{
    const auto& begin_mask_et = get_input_element_type(1);
    const auto& end_mask_et = get_input_element_type(2);
    NODE_VALIDATION_CHECK(this,
                          begin_mask_et.is_integral_number(),
                          "Begin mask must be an integral number, but is: ",
                          begin_mask_et);
    NODE_VALIDATION_CHECK(this,
                          end_mask_et.is_integral_number(),
                          "End mask must be an integral number, but is: ",
                          end_mask_et);

    auto are_mask_elem_in_range = [](size_t e) { return e == 0 || e == 1; };
    NODE_VALIDATION_CHECK(
        this,
        std::all_of(m_begin_mask.begin(), m_begin_mask.end(), are_mask_elem_in_range) &&
            std::all_of(m_end_mask.begin(), m_end_mask.end(), are_mask_elem_in_range) &&
            std::all_of(m_new_axis_mask.begin(), m_new_axis_mask.end(), are_mask_elem_in_range) &&
            std::all_of(
                m_shrink_axis_mask.begin(), m_shrink_axis_mask.end(), are_mask_elem_in_range) &&
            std::all_of(m_ellipsis_mask.begin(), m_ellipsis_mask.end(), are_mask_elem_in_range),
        "All masks of StridedSlice must have be 0 or 1");

    const vector<size_t> attr_sizes = {m_begin_mask.size(),
                                       m_end_mask.size(),
                                       m_new_axis_mask.size(),
                                       m_shrink_axis_mask.size(),
                                       m_ellipsis_mask.size()};
    const auto are_attr_sizes_eq =
        std::all_of(attr_sizes.begin(), attr_sizes.end(), [&attr_sizes](size_t s) {
            return (s == 0) || (attr_sizes[0] == s);
        });
    NODE_VALIDATION_CHECK(
        this, are_attr_sizes_eq, "All masks of StridedSlice must have the same size");

    const auto& data_rank = get_input_partial_shape(0).rank();
    const auto& begin_shape = get_input_partial_shape(1);
    if (begin_shape.rank().is_static())
    {
        NODE_VALIDATION_CHECK(this,
                              begin_shape.rank().get_length() == 1,
                              "Begin input must be 1D (begin rank: ",
                              begin_shape.rank(),
                              ").");
    }
    const auto& end_shape = get_input_partial_shape(2);
    if (end_shape.rank().is_static())
    {
        NODE_VALIDATION_CHECK(this,
                              end_shape.rank().get_length() == 1,
                              "End input must be 1D (end rank: ",
                              end_shape.rank(),
                              ").");
    }

    set_input_is_relevant_to_shape(1);
    set_input_is_relevant_to_shape(2);
    set_input_is_relevant_to_shape(3);

    auto begin_const = as_type_ptr<op::Constant>(input_value(1).get_node_shared_ptr());
    auto end_const = as_type_ptr<op::Constant>(input_value(2).get_node_shared_ptr());
    auto strides_const = as_type_ptr<op::Constant>(input_value(3).get_node_shared_ptr());

    if (begin_const && end_const && strides_const && data_rank.is_static())
    {
        auto begin = begin_const->get_vector<int64_t>();
        auto end = end_const->get_vector<int64_t>();
        auto strides = strides_const->get_vector<int64_t>();
        const auto begin_mask = convert_mask_to_axis_set(get_begin_mask());
        const auto end_mask = convert_mask_to_axis_set(get_end_mask());

        // Handle a case when begin/end/stride size is less than data rank
        // and begin/end mask are passed
        // Example:
        // data_shape: {3, 3, 3, 3}
        // begin_values: [1, 1] - after extending --> [0, 0, 1, 1]
        // end_values: [2, 2] - after extending --> [0, 0, 2, 2]
        // strides : [0, 1] - after extending --> [1, 1, 0, 1] (`1` is neutral as a strides value)
        // begin_mask: [0, 1] - ignore 0 and 1 axes (apply begin slice values to 2 and 3 axes)
        // end_mask: [0, 1] - ignore 0 and 1 axes (apply end slice values to 2 and 3 axes)
        // expected_output_shape: {3, 3, 1, 1}
        for (const auto& mask_axis : begin_mask)
        {
            if (begin.size() < data_rank.get_length())
            {
                begin.insert(std::next(begin.begin(), mask_axis), 0);
                strides.insert(std::next(strides.begin(), mask_axis), 1);
            }
        }
        for (const auto& mask_axis : end_mask)
        {
            if (end.size() < data_rank.get_length())
            {
                end.insert(std::next(end.begin(), mask_axis), 0);
            }
        }

        set_output_type(0,
                        get_input_element_type(0),
                        infer_slice_shape(this,
                                          get_input_partial_shape(0),
                                          begin,
                                          end,
                                          strides,
                                          begin_mask,
                                          end_mask,
                                          convert_mask_to_axis_set(get_new_axis_mask()),
                                          convert_mask_to_axis_set(get_shrink_axis_mask()),
                                          convert_mask_to_axis_set(get_ellipsis_mask())));
    }
    else
    {
        set_output_type(0, get_input_element_type(0), PartialShape::dynamic(data_rank));
    }
}

AxisSet op::v1::StridedSlice::convert_mask_to_axis_set(const std::vector<int64_t>& mask) const
{
    AxisSet axis_set{};
    for (size_t i = 0; i < static_cast<size_t>(mask.size()); ++i)
    {
        if (mask[i] == 1)
        {
            axis_set.emplace(i);
        }
    }
    return axis_set;
}

shared_ptr<Node> op::v1::StridedSlice::copy_with_new_args(const NodeVector& new_args) const
{
    check_new_args_count(this, new_args);
    return make_shared<v1::StridedSlice>(new_args.at(0),
                                         new_args.at(1),
                                         new_args.at(2),
                                         new_args.at(3),
                                         m_begin_mask,
                                         m_end_mask,
                                         m_new_axis_mask,
                                         m_shrink_axis_mask,
                                         m_ellipsis_mask);
}

void op::v1::StridedSlice::generate_adjoints(autodiff::Adjoints& /* adjoints */,
                                             const OutputVector& /* deltas */)
{
    throw ngraph_error("generate_adjoints not implemented for StridedSlice");
}
