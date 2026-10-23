#pragma once

namespace beachlife::runtime::retail_cursor
{
class Provider;
}

namespace beachlife::compat
{
// Stateless adapter to compat LoadImageA/SetCursor. Executable cursor handles
// belong to the ResourceCursorSession surrounding the retail cursor binding.
runtime::retail_cursor::Provider& retail_cursor_provider() noexcept;
} // namespace beachlife::compat
