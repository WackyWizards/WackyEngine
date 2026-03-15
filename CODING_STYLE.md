# Coding Style

C++20 Standards.
Don't use emdashes.

## Indentation
- Use **tabs**, not spaces.

## Comments
- Prefer block comments:

/**
 * This is the preferred comment style.
 */

- Avoid // comments for documentation.

## Control Flow
- Always use braces for conditionals.

Correct:

```cpp
if (condition)
{
    DoSomething();
}
```

Incorrect:

```cpp
if (condition)
    DoSomething();
```

## Whitespace

Avoid using one-liners.
While they are neat, they are harder to expand upon.
Use proper control flow.

```cpp
Correct:
if (!e)
{
    e = std::make_unique<Entity>();
}
```

Incorrect:
```cpp
if (!e) e = std::make_unique<Entity>();
```

## Pointers
Avoid using raw pointers unless necessary.
Prefer smart pointers where possible.

## General Philosophy
- Clarity over cleverness.
- Explicit structure preferred over condensed syntax.