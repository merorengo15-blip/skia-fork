/*
 * Copyright 2025 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrD3DVaryingHandler_DEFINED
#define GrD3DVaryingHandler_DEFINED

#include "src/gpu/ganesh/glsl/GrGLSLVarying.h"

class GrGLSLProgramBuilder;

/*
 * This class handles D3D12/HLSL varying management. Each added varying takes the
 * next available TEXCOORD semantic slot (TEXCOORD0, TEXCOORD1, etc.) for D3D12.
 */
class GrD3DVaryingHandler : public GrGLSLVaryingHandler {
public:
    GrD3DVaryingHandler(GrGLSLProgramBuilder* program) : INHERITED(program) {}

    typedef GrGLSLVaryingHandler::VarArray VarArray;

private:
    void onFinalize() override;

    using INHERITED = GrGLSLVaryingHandler;
};

#endif
