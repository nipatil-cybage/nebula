/*
 * Copyright 2017-present Shawn Cao
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "glog/logging.h"
#include "type/Type.h"

/**
 * Define expressions used in the nebula DSL.
 */
namespace nebula {
namespace api {
// base class for an UDAF
enum class UDAFRegistry {
  COUNT,
  MIN,
  MAX,
  AVG
};

struct UDAF {
  UDAF(UDAFRegistry r) : registry{ r } {}
  UDAFRegistry registry;
};

#define UDAF_TRAITS(UDAF, INNER, KIND)                                   \
  template <>                                                            \
  struct UDAFTraits<UDAFRegistry::UDAF> {                                \
    static constexpr bool requireInner = INNER;                          \
    static constexpr nebula::type::Kind type = nebula::type::Kind::KIND; \
  };

template <UDAFRegistry R>
struct UDAFTraits {};

UDAF_TRAITS(COUNT, false, BIGINT)
UDAF_TRAITS(MIN, true, INVALID)
UDAF_TRAITS(MAX, true, INVALID)
UDAF_TRAITS(AVG, true, INVALID)

#undef UDAF_TRAITS
} // namespace api
} // namespace nebula