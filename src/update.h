#pragma once

// UPDATE boot mode: connect to the internet, fetch the manifest and apply a new firmware, then go
// back to the previous mode. Phase 1 only provides the frame (screen, return path); the download
// itself arrives in phase 6. Never returns.
void update_run(void);
