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

#include "meta/NNode.h"
#include "meta/Table.h"
#include "type/Serde.h"

/**
 * This module is to build special case for trends which has some hard coded data.
 * Will be deleted after it's done with pilot run.
 */
namespace nebula {
namespace execution {
namespace io {
namespace trends {

class TrendsTable : public nebula::meta::Table {
  static constexpr auto NAME = "pin.trends";

public:
  TrendsTable() : Table(NAME) {
    // TODO(cao) - let's make date as a number
    schema_ = nebula::type::TypeSerializer::from("ROW<_time_:long, query:string, count:long>");
  }

  virtual ~TrendsTable() = default;

  // load trends data in current process
  void load(size_t max = 0);
};

} // namespace trends
} // namespace io
} // namespace execution
} // namespace nebula