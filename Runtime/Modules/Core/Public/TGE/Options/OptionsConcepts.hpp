/**
 * @file OptionsConcepts.hpp
 * @brief Lightweight value-semantics requirements for option types.
 */

#pragma once

#include <concepts>
#include <type_traits>

namespace TGE
{
    /**
     * @concept OptionsType
     * @brief Value semantics required for transactional option snapshots.
     *
     * Ordinary option types should be owning public aggregates. Borrowed fields
     * such as string_view, span, references, and raw pointers are unsuitable
     * because provider input buffers are temporary.
     */
    template<class T>
    concept OptionsType =
        std::default_initializable<T> &&
        std::copyable<T> &&
        std::is_object_v<T> &&
        (!std::is_pointer_v<T>);
}
