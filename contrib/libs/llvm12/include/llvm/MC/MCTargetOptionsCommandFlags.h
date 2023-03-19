#pragma once

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

//===-- MCTargetOptionsCommandFlags.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains machine code-specific flags that are shared between
// different command line tools.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCTARGETOPTIONSCOMMANDFLAGS_H
#define LLVM_MC_MCTARGETOPTIONSCOMMANDFLAGS_H

#include "llvm/ADT/Optional.h"
#include <string>

namespace llvm {

class MCTargetOptions;

namespace mc {

bool getRelaxAll();
Optional<bool> getExplicitRelaxAll();

bool getIncrementalLinkerCompatible();

int getDwarfVersion();

bool getDwarf64();

bool getShowMCInst();

bool getFatalWarnings();

bool getNoWarn();

bool getNoDeprecatedWarn();

std::string getABIName();

/// Create this object with static storage to register mc-related command
/// line options.
struct RegisterMCTargetOptionsFlags {
  RegisterMCTargetOptionsFlags();
};

MCTargetOptions InitMCTargetOptionsFromFlags();

} // namespace mc

} // namespace llvm

#endif

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
