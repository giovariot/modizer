//
//  BackendStubs.cpp
//  ModizerMini
//
//  Decoders that still need a pull-based adapter (they run their own audio
//  thread or need a custom callback). Returning nullptr lets the engine fall
//  back to the generic probing order.
//

#include "ChiptuneBackend.h"

