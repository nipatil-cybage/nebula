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

#include "gtest/gtest.h"
#include <any>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include "Errors.h"
#include "Tree.h"
#include "Type.h"
#include "fmt/format.h"
#include "glog/logging.h"

namespace nebula {
namespace type {
namespace test {
using namespace nebula::common;

TEST(TypeTest, Dummy) {
  EXPECT_EQ(4, 2 + 2);
  EXPECT_EQ(fmt::format("a{}", 1), "a1");
  N_ENSURE_EQ(4, 2 + 2);

  LOG(INFO) << fmt::format("The date is {}", 9);

  for (auto i = 0; i < 10; ++i) {
    LOG(INFO) << "COUNTING: " << i;
  }
}

TEST(TreeTest, Basic) {
  Tree<int> tree(0);
  EXPECT_EQ(tree.size(), 0);

  const auto& child = tree.addChild(3);
  EXPECT_EQ(tree.size(), 1);
  EXPECT_EQ(child.size(), 0);

  const auto& first = tree.childAt(0);
  EXPECT_EQ(first.value(), 3);

  LOG(INFO) << "ROOT: " << tree.value();
  LOG(INFO) << "CHILD: " << child.value();
}

TEST(TypeTest, Traits) {
  EXPECT_EQ(TypeTraits<Kind::BOOLEAN>::typeKind, Kind::BOOLEAN);
  EXPECT_EQ(TypeTraits<Kind::BOOLEAN>::isPrimitive, true);
  EXPECT_EQ(TypeTraits<Kind::BOOLEAN>::width, 1);

  // Type<ROW> row;
  // EXPECT_EQ(row.kind(), Kind::STRUCT);
  // EXPECT_EQ(row.isPrimitive(), false);
  // EXPECT_EQ(row.isFixedWidth(), false);
}

TEST(VectorTest, CxxVersion) {
  std::vector<std::any> vec;
  vec.push_back(1);
  vec.push_back("something");
  EXPECT_EQ(vec.size(), 2);
}

/*
* To build a heterogenous container, we have choices
* - static fixed size: 
* -- std::tuple
* -- variadic template parameter pack
* - dynamic sized: 
* -- std::any of an existing contianer such as vector (any_cast)
* -- std::variant with known type set std::variant<T1, T2...>
* this test is on tuple - the pattern comes from cpppatterns.com
*/
template <typename F, typename Tuple, size_t... S>
decltype(auto) apply_tuple_impl(F&& fn, Tuple&& t, std::index_sequence<S...>) {
  return std::forward<F>(fn)(std::get<S>(std::forward<Tuple>(t))...);
}

template <typename F, typename Tuple>
decltype(auto) apply_from_tuple(F&& fn, Tuple&& t) {
  std::size_t constexpr tSize = std::tuple_size<typename std::remove_reference<Tuple>::type>::value;
  return apply_tuple_impl(std::forward<F>(fn),
                          std::forward<Tuple>(t),
                          std::make_index_sequence<tSize>());
}

template <typename T>
void handle(T v);

template <>
void handle(int v) {
  LOG(INFO) << "INT=" << v;
}

template <>
void handle(bool v) {
  LOG(INFO) << "BOOL=" << v;
}

template <>
void handle(char const* v) {
  LOG(INFO) << "STR=" << v;
}

template <>
void handle(double v) {
  LOG(INFO) << "FLOAT=" << v;
}

#define DI(I)                \
  if constexpr (size > I) {  \
    handle(std::get<I>(tp)); \
  }

template <typename Tuple>
decltype(auto) loop(Tuple&& tp) {
  std::size_t constexpr size = std::tuple_size<typename std::remove_reference_t<Tuple>>::value;
  DI(0)
  DI(1)
  DI(2)
  DI(3)
  DI(4)
  DI(5)
}

#undef DI

TEST(TupleTest, TestTuple) {
  auto sum = [](int a, int b, int c, int d) {
    LOG(INFO) << "a=" << a << ", b=" << b;
    return a + b + c + d;
  };

  int result = apply_from_tuple(sum, std::make_tuple(10, 20, 30, 40));
  EXPECT_EQ(result, 100);

  // test loop on each item
  auto t = std::make_tuple(10, true, "C++", 1.0);
  loop(t);
}

} // namespace test
} // namespace type
} // namespace nebula