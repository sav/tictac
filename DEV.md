# Modern C++ -- Best Practices

> LLM directives for C++. Follow unless the project or user says otherwise. Prefer what
> experts ship over the newest feature. Default to clarity, safety, and the STL.

Target C++20/23 (C++26 when opted in). Prefer compile-time over runtime, value semantics
over heap, and the Core Guidelines [0].

## 1. Principles

- **Prefer the STL** and known libs (`{fmt}`, Abseil, Boost) over reinventing -- but no
  dependency for trivia.
- **Make illegal states unrepresentable:** encode invariants in types (`NonNull<T>`,
  `enum` over `int`, `span` over pointer+length), not comments or scattered checks.
- **Value semantics by default**; use references/pointers/heap only for identity or shared
  lifetime.
- **RAII owns everything.** No manual `new`/`delete` or `close()` in normal flow.
- **Be explicit about ownership/lifetime** in signatures.
- **Polymorphism:** prefer compile-time (concepts, templates, CRTP); reserve `virtual`
  for real runtime needs (heterogeneous collections, plugins, stable ABIs).
- **Validate at trust boundaries** (public APIs, input, I/O, deserialization); use
  `assert` for internal invariants only (stripped under NDEBUG -- never guard
  release-critical constraints). Expected failures need real handling, not asserts.
- Depend on interfaces/parameters, not globals; keep side effects at the edges.
- Fixing a bug in a tested project: add the regression test. Don't invent APIs -- say so
  if unsure a facility exists.

## 2. Tooling

- **Warnings as errors** in CI: `-Wall -Wextra -Wpedantic -Werror` (`/W4 /WX` MSVC),
  plus `-Wshadow -Wconversion -Wsign-conversion` where tolerated.
- Wire in **sanitizers** (ASan, UBSan, TSan), **clang-tidy**, **clang-format** as opt-in;
  match the project's `.clang-format`. Compile/build only when asked, and fix errors
  manually rather than rebuilding after every edit.
- Use `constexpr`/`consteval`/`constinit` to express intent, not everywhere.
- Prefer `using` to `typedef`.

## 3. Coding

### 3.1 Baseline

- **Use C++23 freely** (`std::print`, `expected`, `mdspan`, `flat_map`, `generator`).
  For unevenly-supported features, don't degrade -- guard with feature-test macros
  (`__cpp_lib_*`) and a fallback, keeping the C++23 path primary.
- Use `auto` when the type is obvious/verbose/incidental; spell it out when it documents
  intent or a caller depends on it.
- Prefer **`{}` init** (uniform, no narrowing) -- but `vector<int> v{3}` is one element,
  `v(3)` is three; use `()` for size/count.
- Never a raw owning pointer: `unique_ptr`/`shared_ptr` for owned heap, containers for
  sequences, `T*`/`span`/`string_view` for views.
- Fixed-width ints (`std::int32_t`, `std::size_t`, `std::ptrdiff_t`) when width matters (serialization, wire, hw).
- Mark single-arg constructors and conversion operators `explicit` unless implicit is
  wanted.
- Prefer `enum class`; fix the underlying type for ABI, forward decls, bit width, or
  serialization. Unscoped `enum` only for deliberate int conversion.
- **No** C-style casts (use `static_cast` etc., or none), C arrays / pointer+length in
  interfaces (use `array`/`vector`/`span`), `#define` for constants/functions, or
  `using namespace std;` in headers.
- **Always prefix C function calls:** use `std::` when a C++ standard library equivalent exists
  (e.g., `std::filesystem` instead of C file functions); otherwise prefix with `::` to
  explicitly use the global namespace (e.g., `::pipe()`, `::write()`, `::read()`, `::fork()`,
  `::close()`). Never leave C function calls unprefixed.
- `nullptr`, not `NULL`/`0`. `'\n'`, not `std::endl`.
- No dangling references/`string_view`/`span` to locals. No premature `shared_ptr` or
  optimization. No silently swallowed errors / empty `catch (...)`.
- Namespace things; anonymous namespaces (not `static`) for TU-local linkage.
- **Order includes** project headers first (the matching `foo.hpp` leading), then a blank
  line, then standard-library headers -- each group sorted; third-party headers last.
- Define a symbol declared in `foo.hpp` in the matching `foo.cpp` (or inline in `foo.hpp`
  if trivial and include-light) -- never in a differently-named TU; justify deviations with
  a comment at the definition site.
- Define functions in the `.cpp` in the same order they are declared in the `.hpp`.
- When a function `foobar` is comprised of distinct "sections", break each out into a
  `foobar_<secname>` helper rather than leaving one long body. If `foobar` is a public API,
  keep the section helpers TU-local: put them in an anonymous namespace directly above
  `foobar`'s definition, so they don't leak into the header.
- Follow existing project style; else pick one and stay consistent.
- **Comment the why, not the what**; API docstrings and file headers excepted.
- **No divider comments** (`// -- Foo ------`) outside functions. Inside a function you may
  label sections with a plain `// Foo` -- never the dashed form, and never at file scope.
- When hand-layout genuinely reads better than the formatter's (e.g. a wide table of
  bindings), wrap that region in `// clang-format off` / `// clang-format on` rather than
  fighting it with filler markers like trailing `//`. Use it only where it earns its keep;
  don't sprinkle `// clang-format off` across the codebase.

### 3.2 `const` and passing

- **`const` by default** for locals, member functions, parameters, pointees.
- **Left-const style:** `const` goes on the right side of what it modifies. `int const x`, `T const& param`, `int const* ptr`. If there's nothing to the left (e.g., a leading parameter), `const` modifies the right: `const char* msg` is OK at function start, but declare it `char const*` for consistency with the rest of the codebase. Member function `const` stays on the right: `int run() const;` (it qualifies the method, not the return type).
- Pass cheap-to-copy by value; large read-only by `T const&` or a view; sink args by
  value + `std::move`. Prefer returning values over out-params.
- Return by value; trust NRVO -- don't `std::move` a local in `return`.
- Never return a reference/`string_view`/`span` to a local or temporary.
- Return `T&`/`optional`/`expected`, not a nullable `T*` that might be dereferenced; take
  a reference or `gsl::not_null` when null is invalid -- unrepresentable states over UB.

### 3.3 Errors and control flow

- Short, single-purpose functions; early returns over deep nesting.
- **Return early with guard clauses.** Prefer a flat sequence of `if (cond) return ...;`
  at the top of a function -- one guard per case, each returning as soon as it decides --
  over an `if / else if / ... else` chain that mutates a result and falls through to a
  single trailing `return`. Dispatch on a value's type or an action code the same way:
  one guard per branch, returning its own result. It keeps each case self-contained and
  the happy path un-nested.
- **One consistent error strategy:** exceptions for exceptional failures (STL default);
  `expected<T,E>` for recoverable/value-like errors or where exceptions are banned;
  `optional<T>` for a self-explanatory absence; `error_code` at C/system boundaries.
  If the project bans exceptions, honor it.
- Mark `noexcept` only when genuinely non-throwing -- especially move ops and swap
  (containers rely on it). Use `[[nodiscard]]` where the return must not be ignored.

### 3.4 Classes

- **Rule of Zero**: compose RAII members so compiler-generated specials are correct;
  most classes declare none of the five. If you declare one, obey the **Rule of Five**.
- Prefer composition to inheritance. Polymorphic bases: `virtual` (or `protected`)
  destructor, consider deleting copy; mark leaves `final`, overrides `override`.
- Keep invariants inside the class (private data, establishing constructor); no setters
  that break them. Strong types over primitives (`Meters`/`UserId`, not `double`/`int`).
- `struct` for invariant-free aggregates; `class` when there's an invariant to protect.

### 3.5 Containers

- **Default to `std::vector`**; reach for `deque`/`list`/associative only with reason.
- `flat_map`/`flat_set` (C++23) for small lookup-heavy maps; `unordered_*` for large
  hash lookups; `map`/`set` when ordering matters. `reserve()` when size is known.
- Prefer **algorithms/ranges** over raw loops when clearer.
- **Range views** are lazy and reference-capturing: never outlive their range; beware
  views over temporaries or repeated side-effecting transforms.
- `span`/`string_view` don't own or extend lifetime; don't store as long-lived members
  unless lifetime is guaranteed.
- Use structured bindings for readability.

### 3.6 Moves and performance

- Move where a copy is wasted; don't `std::move` a `const` object.
- Emplace when it saves a move, but `push_back(std::move(x))` is fine -- pick readability.
- Default to `unique_ptr` + borrowing; `shared_ptr` only for genuinely shared ownership;
  break cycles with `weak_ptr`.

### 3.7 Templates and concepts

- Prefer **concepts** over raw SFINAE; use standard ones (`ranges::range`, `invocable`,
  `convertible_to`) before inventing. Always constrain template parameters.
- `auto` params for simple generic functions; explicit header when naming/relating types.
- Prefer `if constexpr` to tag dispatch. Don't over-genericize -- add generality when a
  second caller needs it. `static_assert` with clear messages at the boundary.

### 3.8 Concurrency

- Prefer higher-level abstractions to raw threads (`async`, thread pools,
  `execution::par`). Prefer immutable data / message passing over shared mutable state.
- Protect shared state with `mutex` + `scoped_lock`/`lock_guard` (RAII); `scoped_lock`
  for multiple mutexes. `atomic` for simple flags/counters -- don't hand-roll lock-free.
- Prefer `jthread` (auto-join, `stop_token`) over `thread`; never destruct un-joined.
- Any concurrent access with a write needs synchronization; test under TSan.
  `condition_variable`: always wait with a predicate.

### 3.9 I/O and text

- **`std::print`/`format`** (fall back to `{fmt}`); avoid iostream `<<` chains and
  `printf`.
- Treat text as UTF-8; use a real Unicode library, don't hand-roll.
- `std::filesystem` for paths. `std::from_chars` for parsing numbers, over
  `stoi`/`stringstream`.

## References

- [0] [C++ Core Guidelines](/home/sav/u/cpp/CppCoreGuidelines/CppCoreGuidelines.md)
