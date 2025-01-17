// Copyright (C) 2018-2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "op/conv_integer.hpp"

#include "default_opset.hpp"
#include "utils/conv_factory.hpp"
#include "utils/convpool.hpp"
#include "utils/reshape.hpp"

namespace ngraph {
namespace onnx_import {
namespace op {
namespace set_1 {

OutputVector conv_integer(const Node& node) {
    const OutputVector& inputs = node.get_ng_inputs();

    const auto& input = inputs.at(0);
    const auto& filter = inputs.at(1);
    const auto& input_zero_point =
        (inputs.size() > 2) ? inputs.at(2) : ngraph::op::Constant::create(ngraph::element::i32, {1}, {0});
    const auto& filter_zero_point =
        (inputs.size() > 3) ? inputs.at(3) : ngraph::op::Constant::create(ngraph::element::i32, {1}, {0});

    const auto& converted_input = std::make_shared<default_opset::Convert>(input, element::i32);
    const auto& converted_filter = std::make_shared<default_opset::Convert>(filter, element::i32);

    const auto& converted_input_zero_point = std::make_shared<default_opset::Convert>(input_zero_point, element::i32);
    const auto& converted_filter_zero_point = std::make_shared<default_opset::Convert>(filter_zero_point, element::i32);

    const auto& input_shape = std::make_shared<default_opset::ShapeOf>(input, element::i32);
    const auto& input_rank = std::make_shared<default_opset::ShapeOf>(input_shape, element::i32);
    const auto& input_rank_scalar = reshape::interpret_as_scalar(input_rank);

    const auto& one_node = ngraph::op::Constant::create(ngraph::element::i32, {}, {1});
    const auto& missing_dimensions =
        std::make_shared<default_opset::Range>(one_node, input_rank_scalar, one_node, element::i32);
    const auto& resized_filter_zero_point =
        std::make_shared<default_opset::Unsqueeze>(converted_filter_zero_point, missing_dimensions);

    const auto& shifted_input = std::make_shared<default_opset::Subtract>(converted_input, converted_input_zero_point);
    const auto& shifted_filter = std::make_shared<default_opset::Subtract>(converted_filter, resized_filter_zero_point);

    const auto& groups = node.get_attribute_value<int64_t>("group", 1);
    const auto& strides = convpool::get_strides(node);
    const auto& dilations = convpool::get_dilations(node);
    const auto& paddings = convpool::get_pads(node);
    const ngraph::op::PadType& auto_pad_type = convpool::get_auto_pad(node);
    const auto& padding_below = paddings.first;
    const auto& padding_above = paddings.second;

    const auto conv_node = conv_factory::make_ng_convolution(shifted_input,
                                                             shifted_filter,
                                                             strides,
                                                             dilations,
                                                             padding_below,
                                                             padding_above,
                                                             groups,
                                                             auto_pad_type);

    return {conv_node};
}
}  // namespace set_1
}  // namespace op
}  // namespace onnx_import
}  // namespace ngraph
