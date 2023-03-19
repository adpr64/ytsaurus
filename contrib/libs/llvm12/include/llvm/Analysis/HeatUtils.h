#pragma once

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

//===-- HeatUtils.h - Utility for printing heat colors ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Utility for printing heat colors based on profiling information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_HEATUTILS_H
#define LLVM_ANALYSIS_HEATUTILS_H

#include <cstdint>
#include <string>

namespace llvm {

class BlockFrequencyInfo;
class Function;

// Returns number of calls of calledFunction by callerFunction.
uint64_t
getNumOfCalls(Function &callerFunction, Function &calledFunction);

// Returns the maximum frequency of a BB in a function.
uint64_t getMaxFreq(const Function &F, const BlockFrequencyInfo *BFI);

// Calculates heat color based on current and maximum frequencies.
std::string getHeatColor(uint64_t freq, uint64_t maxFreq);

// Calculates heat color based on percent of "hotness".
std::string getHeatColor(double percent);

} // namespace llvm

#endif

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
