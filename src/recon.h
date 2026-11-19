// recon.h - one-shot diagnostic dump, so one deploy retires many unknowns.
//
// PLAN.md 2.2. Nothing here changes game state: it reads memory that is already
// there, resolves interfaces by name, and lists windows and modules.
#pragma once

namespace cwxr {

void ReconDump();

} // namespace cwxr
