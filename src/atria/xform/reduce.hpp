// Copyright: 2014, 2015, Ableton AG, Berlin. All rights reserved.

#pragma once

#include <atria/xform/config.hpp>
#include <atria/xform/functional.hpp>
#include <atria/xform/state_traits.hpp>
#include <atria/xform/abort_reduce.hpp>
#include <atria/xform/any_state.hpp>
#include <atria/xform/skip.hpp>

#include <atria/estd/type_traits.hpp>
#include <atria/estd/utility.hpp>

#include <algorithm>
#include <numeric>
#include <tuple>

namespace atria {
namespace xform {

namespace detail {

template <typename ReducingFnT,
          typename StateT,
          typename InputRangeT>
auto reduce_nested_non_empty_accumulate(ReducingFnT&& step,
                                        StateT&& state,
                                        InputRangeT&& range)
  -> estd::decay_t<StateT>
{
  return std::accumulate(
    std::begin(range),
    std::end(range),
    std::forward<StateT>(state),
    std::forward<ReducingFnT>(step));
}

template <typename ReducingFnT,
          typename StateT,
          typename InputIterT>
auto reduce_nested_non_empty_tail_recursive_impl(ReducingFnT&& step,
                                                 StateT&& state,
                                                 InputIterT&& first,
                                                 InputIterT&& last)
  -> estd::decay_t<StateT>
{
  if (state_is_reduced(state) || first == last)
    return std::forward<StateT>(state);

  auto next_state = step(std::forward<StateT>(state), *first);
  return reduce_nested_non_empty_tail_recursive_impl(
    std::forward<ReducingFnT>(step),
    std::move(next_state),
    std::forward<InputIterT>(++first),
    std::forward<InputIterT>(last));
}

template <typename ReducingFnT,
          typename StateT,
          typename InputRangeT>
auto reduce_nested_non_empty_tail_recursive(ReducingFnT&& step,
                                            StateT&& initial,
                                            InputRangeT&& range)
  -> estd::decay_t<decltype(step(initial, *std::begin(range)))>
{
  auto first = std::begin(range);
  auto last  = std::end(range);
  auto state = step(std::forward<StateT>(initial), *first);
  return reduce_nested_non_empty_tail_recursive_impl(
    std::forward<ReducingFnT>(step),
    std::move(state),
    ++first,
    last);
}

template <typename ReducingFnT,
          typename StateT,
          typename InputRangeT>
auto reduce_nested_non_empty_non_variadic(ReducingFnT&& step,
                                          StateT&& initial,
                                          InputRangeT&& range)
  -> estd::decay_t<decltype(step(initial, *std::begin(range)))>
{
  auto first = std::begin(range);
  auto last  = std::end(range);
  auto state = step(std::forward<StateT>(initial), *first);
  // This may be expressed more brief with a:
  //    while(++first != last)
  // but the for loop seems to make compilers generate better code.
  for (++first; !state_is_reduced(state) && first != last; ++first) {
    // `x = std::move(x)` is undefined behaviour, hence the two
    // steps approach to protect for when `step` just forwards
    // the state back.
    auto new_state = step(std::move(state), *first);
    state = std::move(new_state);
  }
  return state;
}

template <std::size_t Index, std::size_t Max>
struct tuple_all_neq_t
{
  template <typename Tuple1T, typename Tuple2T>
  bool operator()(Tuple1T&& t1, Tuple2T&& t2)
  {
    return
      std::get<Index>(std::forward<Tuple1T>(t1)) !=
      std::get<Index>(std::forward<Tuple2T>(t2)) &&
      tuple_all_neq_t<Index + 1, Max>{} (
        std::forward<Tuple1T>(t1),
        std::forward<Tuple2T>(t2));
  }
};

template <std::size_t Max>
struct tuple_all_neq_t<Max, Max>
{
  template <typename Tuple1T, typename Tuple2T>
  bool operator()(Tuple1T&&, Tuple2T&&)
  {
    return true;
  }
};

template <typename Tuple1T, typename Tuple2T>
bool tuple_all_neq(Tuple1T&& t1, Tuple2T&& t2)
{
  constexpr auto size1 = std::tuple_size<estd::decay_t<Tuple1T> >{};
  constexpr auto size2 = std::tuple_size<estd::decay_t<Tuple2T> >{};
  using impl_t = tuple_all_neq_t<0u, (size1 > size2 ? size2 : size1)>;

  return impl_t{} (
    std::forward<Tuple1T>(t1),
    std::forward<Tuple2T>(t2));
}

template <typename ReducingFnT,
          typename StateT,
          std::size_t ...Indices,
          typename ...InputRangeTs>
auto reduce_nested_non_empty_variadic_impl(ReducingFnT&& step,
                                 StateT&& initial,
                                 estd::index_sequence<Indices...>,
                                 InputRangeTs&& ...ranges)
  -> estd::decay_t<decltype(step(initial, *std::begin(ranges)...))>
{
  auto firsts = std::make_tuple(std::begin(ranges)...);
  auto lasts  = std::make_tuple(std::end(ranges)...);
  auto state  = step(std::forward<StateT>(initial),
                     *std::get<Indices>(firsts)...);

  meta::noop(++std::get<Indices>(firsts)...);
  while (!state_is_reduced(state) && detail::tuple_all_neq(firsts, lasts)) {
    auto new_state = step(std::move(state), *std::get<Indices>(firsts)...);
    state = std::move(new_state);
    meta::noop(++std::get<Indices>(firsts)...);
  }

  return state;
}

template <typename ReducingFnT,
          typename StateT,
          typename ...InputRangeTs>
auto reduce_nested_non_empty_variadic(ReducingFnT&& step, StateT&& state, InputRangeTs&& ...ranges)
  -> ABL_DECLTYPE_RETURN(
    reduce_nested_non_empty_variadic_impl(
      std::forward<ReducingFnT>(step),
      std::forward<StateT>(state),
      estd::make_index_sequence<sizeof...(InputRangeTs)> {},
      std::forward<InputRangeTs>(ranges)...))

#if ABL_REDUCE_TAIL_RECURSIVE
#  define ABL_REDUCE_NESTED_NON_EMPTY_NON_VARIADIC_IMPL \
  ::atria::xform::detail::reduce_nested_non_empty_tail_recursive
#elif ABL_REDUCE_WITH_ACCUMULATE
#  define ABL_REDUCE_NESTED_NON_EMPTY_NON_VARIADIC_IMPL \
  ::atria::xform::detail::reduce_nested_non_empty_accumulate
#elif ABL_REDUCE_ALWAYS_VARIADIC
#  define ABL_REDUCE_NESTED_NON_EMPTY_NON_VARIADIC_IMPL \
  ::atria::xform::detail::reduce_nested_non_empty_variadic
#else
#  define ABL_REDUCE_NESTED_NON_EMPTY_NON_VARIADIC_IMPL \
  ::atria::xform::detail::reduce_nested_non_empty_non_variadic
#endif

struct reduce_nested_non_empty_t
{
  template <typename StateT,
            typename ReducingFnT,
            typename InputRangeT>
  auto operator()(StateT&& state,
                  ReducingFnT&& step,
                  InputRangeT&& range)
    -> ABL_DECLTYPE_RETURN(
      ABL_REDUCE_NESTED_NON_EMPTY_NON_VARIADIC_IMPL(
        std::forward<ReducingFnT>(step),
        std::forward<StateT>(state),
        std::forward<InputRangeT>(range)))

  template <typename StateT,
            typename ReducingFnT,
            typename InputRangeT,
            typename ...InputRangeTs>
  auto operator()(StateT&& state,
                  ReducingFnT&& step,
                  InputRangeT&& range,
                  InputRangeTs&& ...ranges)
    -> ABL_DECLTYPE_RETURN(
      detail::reduce_nested_non_empty_variadic(
        std::forward<ReducingFnT>(step),
        std::forward<StateT>(state),
        std::forward<InputRangeT>(range),
        std::forward<InputRangeTs>(ranges)...))
};

template <typename RangeT>
bool is_non_empty(const RangeT& r)
{
  using std::begin;
  using std::end;
  return begin(r) != end(r);
}

template <typename RangeT, typename ...RangeTs>
bool is_non_empty(const RangeT& r, const RangeTs& ...rs)
{
  return is_non_empty(r) && is_non_empty(rs...);
}

} // namespace detail

//!
// Similar to @a reduce, but does not unwrap `reduced` values.  This is
// useful when calling reduce recursively inside a transducer.
// @see take
//
template <typename ReducingFnT,
          typename StateT,
          typename ...InputRangeTs>
auto reduce_nested(ReducingFnT&& step, StateT&& state, InputRangeTs&& ...ranges)
  -> ABL_DECLTYPE_RETURN(
    detail::is_non_empty(ranges...)
      ? call(detail::reduce_nested_non_empty_t{},
             std::forward<StateT>(state),
             std::forward<ReducingFnT>(step),
             std::forward<InputRangeTs>(ranges)...)
      : skip(std::forward<StateT>(state)))

//!
// Similar to clojure.core/reduce.  Unlike `std::accumulate`, this
// reduces over a range (doesn't take to distinct iterators) and can
// reduce over several ranges at the same time.  It also supports
// early termination for transducers.
//
template <typename ReducingFnT,
          typename StateT,
          typename ...InputRangeTs>
auto reduce(ReducingFnT&& step, StateT&& state, InputRangeTs&& ...ranges)
  -> estd::decay_t<StateT>
{
  return detail::is_non_empty(ranges...)
    ? state_complete(
      detail::reduce_nested_non_empty_t{} (
        std::forward<StateT>(state),
        std::forward<ReducingFnT>(step),
        std::forward<InputRangeTs>(ranges)...))
    : std::forward<StateT>(state);
}

namespace impure {

namespace detail {

template <typename ResultT, typename ArgT>
auto from_any_state(ArgT&& s)
  -> estd::enable_if_t<std::is_same<estd::decay_t<ArgT>, any_state>{},
                       estd::decay_t<ResultT> >
{
  return std::forward<ArgT>(s).template as<ResultT>();
}

template <typename ResultT, typename ArgT>
auto from_any_state(ArgT&& s)
  -> estd::enable_if_t<!std::is_same<estd::decay_t<ArgT>, any_state>{},
                       ArgT&&>
{
  return std::forward<ArgT>(s);
}

} // namespace detail

template <typename ReducingFnT,
          typename StateT,
          typename ...InputRangeTs>
auto reduce(ReducingFnT&& step, StateT&& state, InputRangeTs&& ...ranges)
  -> estd::decay_t<StateT>
{
  try {
    return detail::from_any_state<StateT>(
      state_complete(
        reduce_nested(
          std::forward<ReducingFnT>(step),
          std::forward<StateT>(state),
          std::forward<InputRangeTs>(ranges)...)));
  } catch (reduce_aborted_error<estd::decay_t<StateT> >& err) {
    return std::move(err.result);
  } catch (reduce_aborted_error<any_state>& err) {
    return std::move(err.result).as<StateT>();
  }
}

} // namespace impure

} // namespace xform
} // namespace atria
