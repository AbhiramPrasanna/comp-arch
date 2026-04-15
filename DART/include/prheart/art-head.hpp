#pragma once

#include "measure/measure.hpp"

namespace prheart {

enum class PrheartNodeType : uint8_t {
    None = 0,
    Leaf = 1,
    Node8,
    Node16,
    Node32,
    Node64,
    Node128,
    Node256,
};

}