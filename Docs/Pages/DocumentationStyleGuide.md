# Documentation Style Guide

## Overview

1. **Everything is documented.** Public, protected, private; API and SPI; tests; tools. No undocumented symbols are allowed in the repository’s default branch.
2. **Docs compile and lint in CI.** The build fails on any undocumented symbol, mismatched `@param`, or Doxygen warning.
3. **Docs tell a story.** Every file and module explains intent and role in the architecture. Classes and functions include practical examples. All examples are compilable and covered by CI.
4. **The first line is the contract.** Every brief line is a crisp, standalone statement of purpose (one sentence, imperative mood).
5. **Correctness beats verbosity.** Prefer tight, accurate prose with precise tags over long meandering commentary.
6. **Consistency wins.** Use the tag sets below exactly; don’t improvise new headings. Use the same terminology as the architecture pages and glossary.
7. **Traceability.** Cross-link to related types, invariants, preconditions, and example snippets. No orphaned documentation.

---

## File-Level Documentation

At the top of every `.hpp`, `.cpp`, and module interface file:

```cpp
/**
 * @file <relative/path/ComponentFoo.hpp>
 * @brief Contract: what this file *provides* at a glance.
 * @details
 * High-level purpose, design intent, and how this file fits into the wider architecture:
 * - Role in the subsystem/module (link to @ref sec-arch-module-foo).
 * - Key invariants the code maintains.
 * - Error/exception policy at this boundary.
 * - Concurrency model: threads, executors, or single-threaded lifecycle.
 * - Performance envelope and known trade-offs.
 * @ingroup grp-<module-or-subsystem>
 */
```

**Rules**

* `@file` + `@brief` + `@details` are required.
* `@ingroup` connects the file to a module group (defined via `@defgroup`/`@addtogroup`).
* If the file defines multiple public types, also add a *“Contains”* bullet list in `@details` that links to each main type.

---

## Groups and Modules

Define stable API surfaces as groups, and attach types and files to them:

```cpp
/// @defgroup grp-io IO Subsystem
/// High-level docs for IO, guarantees, performance profile, and extension points.

/// @addtogroup grp-io
/// (Optionally add subgroups)
```

Attach files/classes with `@ingroup grp-io`.

---

## Symbol-Level Rules and Required Tags

The following tables specify **minimum tags** per element. Add optional tags as applicable.

### Namespaces

```cpp
/**
 * @brief Purpose of the namespace within the architecture.
 * @details Scope boundaries, typical consumers, and non-goals.
 * @ingroup grp-<module>
 */
namespace TGE::IO { /* ... */ }
```

**Required:** `@brief`, `@details`, `@ingroup`

---

### Classes / Structs / Unions

```cpp
/**
 * @brief One-sentence contract (what this type guarantees/does).
 * @details
 * - Responsibilities and key invariants.
 * - Collaboration: which components it depends on (with @ref links).
 * - Lifecycle, ownership semantics, and thread-safety level.
 * - Performance characteristics (time/space; amortized costs).
 * - Error/exception guarantees (no-throw/strong/basic; what is thrown).
 * @tparam T Constraint and semantic meaning (link to @ref concept-… if any).
 * @tparam Allocator … (repeat for each template parameter).
 * @invariant Explicit invariants that must always hold.
 * @par Thread-safety
 * Reads/writes; concurrent semantics; internal synchronization.
 * @par Examples
 * @snippet examples/WidgetExamples.cpp BasicConstruction
 * @snippet examples/WidgetExamples.cpp StreamingToFormatter
 * @ingroup grp-<module>
 */
template<class T, class Allocator = std::allocator<T>>
class Widget { /* … */ };
```

**Required:** `@brief`, `@details`, `@ingroup`, `@tparam` for each template parameter, `@par Thread-safety`, `@invariant`, at least one `@snippet` or `@code` example.

**Optional but encouraged:** `@note`, `@warning`, `@remark`, `@todo`, `@since`, `@deprecated`, `@see` / `@sa`.

---

### Concepts

```cpp
/**
 * @concept concept-RangeLike
 * @brief Semantic contract for range-like types used in FooBar.
 * @details Structural requirements and semantic laws with examples.
 * @tparam T candidate type
 * @par Examples
 * @code{.cpp}
 * static_assert(concept_RangeLike<std::vector<int>>);
 * @endcode
 * @ingroup grp-<module>
 */
template<typename T>
concept concept_RangeLike = requires(T t) {
  { std::begin(t) } -> /* … */;
  { std::end(t) }   -> /* … */;
};
```

**Required:** `@concept`, `@brief`, `@details`, examples, `@ingroup`.

---

### Enums and Enum Values

```cpp
/**
 * @brief States of the Frobnicator lifecycle.
 * @details Map to state machine @ref sec-arch-frobnicator-sm.
 * @ingroup grp-<module>
 */
enum class FrobnicatorState {
  /** @brief Not initialized; only default operations allowed. */
  Uninitialized,
  /** @brief Configured and ready to start. */
  Ready,
  /** @brief Running and processing events. */
  Running,
  /** @brief Permanently shut down; cannot be restarted. */
  Terminated
};
```

**Required:** Enum `@brief`, `@details`, per-value brief docs, `@ingroup`.

---

### Type Aliases

```cpp
/** @brief Owning handle for a scheduled task; cancels on destruction. */
using TaskHandle = std::unique_ptr<detail::TaskControl>;
```

**Required:** `@brief` describing semantics and ownership.

---

### Data Members (public/protected/private)

```cpp
/** @brief Maximum items retained in cache; 0 disables caching. */
std::size_t maxItems{};
```

**Required for all non-local data members:** `@brief`, units/defaults/constraints.

---

### Constructors / Destructors

```cpp
/**
 * @brief Construct a Widget with a bounded capacity.
 * @param capacity Maximum items; must be > 0.
 * @throws std::invalid_argument if @p capacity == 0.
 * @post size() == 0
 */
explicit Widget(std::size_t capacity);

/**
 * @brief Releases resources; non-throwing.
 * @exceptions No-throw.
 */
~Widget() noexcept;
```

**Required:** `@brief`, `@param` for each parameter, `@throws` or `@exceptions` policy, `@pre`/`@post` as applicable.

---

### Member Functions / Free Functions

```cpp
/**
 * @brief Insert or assign a value.
 * @details Strong exception guarantee.
 * @param key   Unique key. Pre: not empty.
 * @param value Value to insert or assign.
 * @return true if insertion took place, false if assigned.
 * @retval true  Inserted new element.
 * @retval false Assigned existing element.
 * @throws std::bad_alloc on allocation failure.
 * @pre   capacity() > size()
 * @post  contains(key)
 * @complexity Amortized O(1).
 * @par Thread-safety
 * Safe for concurrent reads; exclusive for writes.
 * @par Examples
 * @snippet examples/MapExamples.cpp InsertOrAssign
 * @ingroup grp-<module>
 */
bool insert_or_assign(Key key, Value value);
```

**Required:** `@brief`, `@param` for each parameter with direction `[in]`/`[out]` if non-obvious, `@return` or `@retval`, `@throws` (or explicit “does not throw”), `@pre`, `@post`, `@complexity`, `@par Thread-safety`, at least one example, `@ingroup`.

**Overloads:** use `@overload` on non-primary overloads to inherit description or `@copydoc` to reuse text.

**Friends / Relates:** for non-member operators or helpers associated with a type, attach with `@relates TypeName` (or `@relatesalso`) so they appear with the class.

---

### Templates (Function/Class)

* **All template parameters must have `@tparam`** explaining semantic role and constraints.
* If constrained via a concept, **link it** (`@ref concept-RangeLike`).
* Document deduction guides when they change semantics.

---

### `std::formatter` Specializations (and other customizations)

```cpp
/**
 * @brief Formatter for Widget supporting {:n} (name) and {:d} (debug) specifiers.
 * @details
 * Supported presentation types:
 * - `n`: human-friendly (stable)
 * - `d`: debug/diagnostic (unstable)
 * @par Examples
 * @code{.cpp}
 * Widget w{ };
 * std::string s1 = std::format("{:n}", w);
 * std::string s2 = std::format("{:d}", w);
 * @endcode
 * @throws std::format_error on invalid fmt spec.
 * @ingroup grp-formatting
 */
template<>
struct std::formatter<TGE::Widget> : std::formatter<std::string> {
  // ...
};
```

**Required:** `@brief`, supported specifiers (enumerate), examples, `@throws`, `@ingroup`.

---

## Examples: Structure and Enforcement

**Examples must be compilable.** Keep them under `examples/` and reference with `@snippet` to guarantee sync with code.

* `examples/<TypeName>Examples.cpp` per major class.
* `examples/<Module>Usage.cpp` per module.
* Build these examples in CI (as small targets) and run a smoke test.
* For cross-cutting concerns (formatting, error handling), create dedicated example files.

**Snippet discipline**

* Prefer `@snippet` over inline `@code` for anything longer than 5 lines.
* Tag regions with named markers:

```cpp
// --- BasicConstruction
Widget w{16};
// --- BasicConstruction
```

---

## Error Handling, Contracts, and Complexity

Every callable that might fail must document:

* **`@throws`** precise type(s); or “Does not throw”.
* **`@pre`** preconditions (state requirements, argument domains).
* **`@post`** postconditions (state changes, guarantees).
* **`@invariant`** on types (what must always hold).
* **`@complexity`** (Big-O; amortized when relevant).
* **`@par Thread-safety`** reentrancy, synchronization, and allowed concurrent patterns.

Use the exact headings above for consistent rendering.

---

## Deprecation, Versioning, and Stability

* **`@since`** first version where the symbol appeared.
* **`@deprecated`** with replacement guidance and removal timeline.
* Maintain a **changelog page** and link symbols with `@since`/`@deprecated`.

---

## Cross-Referencing and Navigation

* Use `@ref` for internal anchors and pages; `@link … @endlink` for external links only when necessary.
* Provide **“See also”** (`@sa`) sections in key classes and top APIs.
* Use `@anchor` to name deep-link targets for important paragraphs.

---

## Tests and Documentation

* Test fixtures and public test utilities must be documented as first-class APIs if intended for user extension.
* Unit tests should reference examples when demonstrating public behavior; examples should mirror real usage, not mocked fantasies.

---

## Style Guide (How to Write)

* **Brief line:** single sentence, imperative (“Return an iterator…”, not “Returns…” is also acceptable but be consistent).
* **Voice:** plain, precise; avoid marketing words.
* **Terminology:** must match the glossary and architecture pages.
* **Numbers and units:** always specify units and ranges.
* **Code blocks:** `@code{.cpp}` with minimal includes; rely on snippets for longer examples.
* **Line width:** wrap prose at ~100–120 columns; code examples as needed.

---

## Acceptance Criteria

1. **Coverage**

   * No Doxygen warnings or undocumented items.
   * All `@param`/`@tparam` documented and matched (names, count).
   * All public and protected members have `@brief` + required tags.
   * All files have a file-level block with `@file`, `@brief`, `@details`, `@ingroup`.

2. **Quality gates**

   * At least one `@snippet`/`@code` example per public class and per public free function.
   * Each major class documents `@par Thread-safety`, `@invariant`, and complexity for relevant operations.
   * `std::formatter` (and similar customizations) enumerate supported specifiers and throw conditions.

3. **Architecture linkage**

   * Every public module/type links to an architecture page via `@ingroup` and at least one `@ref` in `@details`.

4. **Compilable examples**

   * Example targets build and run in CI.
   * Snippet markers exist and are referenced by at least one doc block.

---

## Boilerplate Templates (Copy/Paste Starters)

**File header**

```cpp
/**
 * @file path/To/File.hpp
 * @brief <One-line purpose and contract.>
 * @details
 * <Explain role in architecture, invariants, error policy, concurrency, performance.>
 * @ingroup grp-<module>
 */
```

**Class**

```cpp
/**
 * @brief <What this type guarantees/does.>
 * @details
 * - Responsibilities:
 * - Collaborators:
 * - Invariants:
 * - Error policy:
 * - Performance:
 * - Lifecycle/ownership:
 * @tparam T <meaning/constraints>
 * @par Thread-safety
 * <Rules>
 * @par Examples
 * @snippet examples/<Type>Examples.cpp <Tag>
 * @ingroup grp-<module>
 */
template<class T> class Type { /*…*/ };
```

**Function**

```cpp
/**
 * @brief <Action/contract>
 * @param[in]  foo  <meaning/constraints>
 * @param[out] bar  <meaning/constraints>
 * @return <meaning; use @retval when multiple cases>
 * @retval true  <case>
 * @retval false <case>
 * @throws std::error_condition <when>
 * @pre  <conditions>
 * @post <conditions>
 * @complexity O(n)
 * @par Thread-safety
 * <Rules>
 * @par Examples
 * @snippet examples/<Area>Examples.cpp <Tag>
 * @ingroup grp-<module>
 */
```

**Enum**

```cpp
/**
 * @brief <Meaningful domain states>
 * @details <Links to state machine/architecture page>
 * @ingroup grp-<module>
 */
enum class Mode {
  /** @brief <doc> */ Off,
  /** @brief <doc> */ On
};
```

**Concept**

```cpp
/**
 * @concept concept-<Name>
 * @brief <Semantic contract>
 * @details <Structural + semantic requirements>
 * @tparam T <candidate>
 * @par Examples
 * @code{.cpp}
 * static_assert(concept_<Name><std::vector<int>>);
 * @endcode
 * @ingroup grp-<module>
 */
```

**Formatter**

```cpp
/**
 * @brief Formatter for <Type>; supports {:<specs>}.
 * @details List specifiers and stability guarantees.
 * @throws std::format_error on invalid spec.
 * @par Examples
 * @snippet examples/<Type>Examples.cpp <FormatTag>
 * @ingroup grp-formatting
 */
```

---

## Why this level of strictness?

* **Onboarding speed:** New developers can navigate via architecture pages, then drill into types with examples.
* **API stability:** Explicit contracts (`@pre`, `@post`, invariants) force careful change management and versioning.
* **Operational reliability:** Concurrency and error policies are part of the contract, not tribal knowledge.
* **User empathy:** Examples reduce cognitive load and show the “happy path” and edge cases.
