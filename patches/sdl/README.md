# SDL patches

The pinned SDL revision remains unchanged. Prepared builds apply `series` to
its exported source; dependency checkouts are never edited.

`0001-guard-empty-display-modes.patch` skips the array copy when a display has
no fullscreen modes. SDL's dummy backend legitimately leaves the source pointer
null in that case. Calling `memcpy` with that pointer violates its nonnull
contract even for zero bytes and fails UBSan. The returned empty, null-terminated
mode list, count and all nonempty-mode behavior are preserved.
