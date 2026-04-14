#ifndef COMPONENT_INTERNALS_H
#define COMPONENT_INTERNALS_H

#include "utils/hash.h"
#include "utils/language.h"
#include "utils/tuples.h"

#include <concepts>
#include <memory>
#include <vector>

class AbstractTask;

namespace components {
class TaskSpecificComponent;
class TaskIndependentComponentBase;

namespace internals {
template<typename T>
concept BasicType =
    std::convertible_to<std::decay_t<T>, std::string> ||
    std::is_same_v<std::decay_t<T>, int> ||
    std::is_same_v<std::decay_t<T>, double> ||
    std::is_same_v<std::decay_t<T>, bool> || std::is_enum_v<std::decay_t<T>>;

template<typename T>
concept TaskSpecificType = std::derived_from<T, TaskSpecificComponent>;

template<typename T>
concept TaskIndependentType =
    std::derived_from<T, TaskIndependentComponentBase>;

/*
  Helper to delay the evaluation of the static assertion below until the
  instantiation of the template.
*/
template<typename>
inline constexpr bool always_false_v = false;

template<typename T>
struct BoundArgs {
    static_assert(
        always_false_v<T>,
        "Unsupported type for task binding in BoundArgs<T>.");
};

template<BasicType T>
struct BoundArgs<T> {
    using type = T;
};

/*
  SFINAE-based trait to detect whether T has a BoundType member type.
  This is used as a proxy for detecting TaskIndependentComponent types
  without requiring T to be complete (unlike std::derived_from).
*/
template<typename T, typename = void>
struct HasBoundType : std::false_type {};

template<typename T>
struct HasBoundType<T, std::void_t<typename T::BoundType>> : std::true_type {};

/*
  Helper to compute the bound type for shared_ptr<T>: if T is a
  task-independent component (has BoundType), use T::BoundType;
  otherwise, pass through the shared_ptr unchanged.
*/
template<typename T, typename = void>
struct SharedPtrBoundArgs {
    using type = std::shared_ptr<T>;
};

template<typename T>
struct SharedPtrBoundArgs<T, std::void_t<typename T::BoundType>> {
    using type = T::BoundType;
};

template<typename T>
struct BoundArgs<std::shared_ptr<T>> {
    using type = SharedPtrBoundArgs<T>::type;
};

template<typename T>
struct BoundArgs<std::vector<T>> {
    using type = std::vector<std::decay_t<typename BoundArgs<T>::type>>;
};

template<typename... Ts>
struct BoundArgs<std::tuple<Ts...>> {
    using type = std::tuple<std::decay_t<typename BoundArgs<Ts>::type>...>;
};

template<typename T>
using BoundArgs_t = BoundArgs<std::decay_t<T>>::type;

template<typename Args, typename T>
concept ComponentArgsFor = utils::ConstructibleFromArgsTuple<
    T, typename utils::PrependedTuple<
           std::shared_ptr<AbstractTask>, BoundArgs_t<Args>>::type>;

template<typename ComponentType, typename T>
concept ComponentTypeOf =
    std::derived_from<ComponentType, TaskSpecificComponent> &&
    std::derived_from<T, ComponentType>;

template<typename T>
BoundArgs_t<std::shared_ptr<T>> bind_task_recursively(
    const std::shared_ptr<T> &component,
    [[maybe_unused]] const std::shared_ptr<AbstractTask> &task) {
    if constexpr (HasBoundType<T>::value) {
        if (component) {
            return component->bind_task(task);
        }
        return nullptr;
    } else {
        return component;
    }
}

template<BasicType T>
BoundArgs_t<T>
bind_task_recursively(const T &t, const std::shared_ptr<AbstractTask> &) {
    return t;
}

template<typename T>
BoundArgs_t<std::vector<T>> bind_task_recursively(
    const std::vector<T> &vec, const std::shared_ptr<AbstractTask> &task) {
    BoundArgs_t<std::vector<T>> result;
    result.reserve(vec.size());
    for (const auto &elem : vec) {
        result.push_back(bind_task_recursively(elem, task));
    }
    return result;
}

template<typename... Args>
BoundArgs_t<std::tuple<Args...>> bind_task_recursively(
    const std::tuple<Args...> &args,
    const std::shared_ptr<AbstractTask> &task) {
    return std::apply(
        [&](const Args &...elems) {
            return std::make_tuple(bind_task_recursively(elems, task)...);
        },
        args);
}

template<BasicType T>
void collect_task_preserving_components(
    const T &, std::vector<TaskIndependentComponentBase *> &) {
}

template<typename T>
void collect_task_preserving_components(
    const std::shared_ptr<T> &component,
    [[maybe_unused]] std::vector<TaskIndependentComponentBase *> &out) {
    if constexpr (HasBoundType<T>::value) {
        if (component) {
            out.push_back(component.get());
            component->get_task_preserving_subcomponents(out);
        }
    }
}

template<typename T>
void collect_task_preserving_components(
    const std::vector<T> &vec,
    std::vector<TaskIndependentComponentBase *> &out) {
    for (const auto &elem : vec) {
        collect_task_preserving_components(elem, out);
    }
}

template<typename... Ts>
void collect_task_preserving_components(
    const std::tuple<Ts...> &tuple,
    std::vector<TaskIndependentComponentBase *> &out) {
    std::apply(
        [&](const Ts &...elems) {
            (collect_task_preserving_components(elems, out), ...);
        },
        tuple);
}
}
}
#endif
