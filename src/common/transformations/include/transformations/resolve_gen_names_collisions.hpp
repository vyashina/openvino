// Copyright (C) 2018-2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "openvino/pass/pass.hpp"
#include "transformations_visibility.hpp"

namespace ov {
namespace pass {

/**
 * @ingroup ie_transformation_common_api
 * @brief ResolveGeneratedNameCollisions transformation helps to fix names collisions
 * if some autogenerated name has a conflict with the name from the original graph
 * 
 * Every transformation call can change the graph structure and create some additional operations,
 * autogenerated name is used if new operation doesn't have friendly name.
 * This transformations should be called after the transformation pipeline in order to fix names collisions.
 */
class TRANSFORMATIONS_API ResolveGeneratedNameCollisions : public ModelPass {
public:
    OPENVINO_RTTI("ResolveGeneratedNameCollisions", "0");
    bool run_on_model(const std::shared_ptr<ov::Model>& model) override;
};

}  // namespace pass
}  // namespace ov
