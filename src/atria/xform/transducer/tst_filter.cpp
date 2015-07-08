// Copyright: 2014, 2015, Ableton AG, Berlin. All rights reserved.

#include <atria/xform/concepts.hpp>
#include <atria/xform/functional.hpp>
#include <atria/xform/reducing/last_rf.hpp>
#include <atria/xform/transduce.hpp>
#include <atria/xform/transducer/filter.hpp>
#include <atria/xform/transducer/map.hpp>

#include <atria/testing/gtest.hpp>

namespace atria {
namespace xform {

TEST(filter, concept)
{
  meta::check<Transducer_spec(decltype(filter(std::less<int>{})),
                              meta::pack<int, int>)>();
}

TEST(transduce, composition)
{
  auto v = std::vector<int> { 1, 2, 3, 6 };
  auto times2 = [] (int x) { return x * 2; };
  auto odd = [] (int x) { return x % 2 == 0; };

  // transducers compose from left to right, this is equivalent to
  // Haskell-like expression:
  //
  //   foldl (+) $ map times2 $ filter odd $ v
  //
  auto res = transduce(
    comp(filter(odd), map(times2)), std::plus<int>{}, 1, v);
  EXPECT_EQ(res, 17);
}

TEST(filter, make_sure_inputs_cant_be_doubly_sinked)
{
  auto orig = std::vector<int> {{1, 2, 3}};
  auto pred = [] (std::vector<int> x) { return x.size() > 2; };

  auto v = orig;
  auto x = filter(pred)(last_rf)(std::vector<int>{}, std::move(v));
  EXPECT_EQ(x, orig);
}

} // namespace xform
} // namespace atria
