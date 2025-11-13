/*
 * Copyright 2025 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Minimal SPIRV type definitions for D3D12-only fork.
 *
 * This file contains only the SpvDim enum which is used throughout SkSL
 * to represent texture dimensions in a backend-agnostic way. While originally
 * from the SPIRV specification, these dimension types map cleanly to HLSL
 * texture types (Texture1D, Texture2D, Texture3D, TextureCube, etc.).
 *
 * The full SPIRV headers and code generation have been removed in this fork,
 * but we retain this enum for internal type representation.
 */

#ifndef SKSL_SPIRV_H
#define SKSL_SPIRV_H

// Texture dimension types (from SPIRV specification)
typedef enum SpvDim_ {
    SpvDim1D = 0,
    SpvDim2D = 1,
    SpvDim3D = 2,
    SpvDimCube = 3,
    SpvDimRect = 4,
    SpvDimBuffer = 5,
    SpvDimSubpassData = 6,
} SpvDim;

#endif  // SKSL_SPIRV_H
