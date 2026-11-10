// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

namespace encore::test
{

/** Number of heap allocations/frees made inside rt::ScopedRealtimeSection on
    any thread since the test run started. The test executable replaces the
    global operator new/delete to count them, so this works on every compiler
    and in Release builds too.
*/
int realtimeAllocationCount() noexcept;

} // namespace encore::test
