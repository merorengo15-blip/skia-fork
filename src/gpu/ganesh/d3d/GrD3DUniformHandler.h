/*
 * Copyright 2025 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrD3DUniformHandler_DEFINED
#define GrD3DUniformHandler_DEFINED

#include "include/private/base/SkTArray.h"
#include "include/private/gpu/ganesh/GrTypesPriv.h"
#include "src/base/SkTBlockList.h"
#include "src/gpu/Swizzle.h"
#include "src/gpu/ganesh/GrSamplerState.h"
#include "src/gpu/ganesh/glsl/GrGLSLUniformHandler.h"

#include <cstdint>

class GrBackendFormat;
class GrGLSLProgramBuilder;
class GrProcessor;
class GrShaderVar;
class SkString;
enum class SkSLType : char;
struct GrShaderCaps;

/*
 * This class handles D3D12/HLSL uniform management. It creates a single uniform buffer
 * (cbuffer) for all uniforms and uses HLSL register bindings (register(b0) for uniforms,
 * register(t#) for textures, register(s#) for samplers).
 */
class GrD3DUniformHandler : public GrGLSLUniformHandler {
public:
    const GrShaderVar& getUniformVariable(UniformHandle u) const override;
    const char* getUniformCStr(UniformHandle u) const override;

    struct D3DUniformInfo : public UniformInfo {
        int fUBOOffset;
    };
    typedef SkTBlockList<D3DUniformInfo> UniformInfoArray;

    // D3D12 register spaces (equivalent to SPIRV descriptor sets for compatibility)
    static constexpr int kUniformDescriptorSet = 0;
    static constexpr int kSamplerTextureDescriptorSet = 1;

    uint32_t getRTFlipOffset() const;

    int numUniforms() const override {
        return fUniforms.count();
    }

    UniformInfo& uniform(int idx) override {
        return fUniforms.item(idx);
    }
    const UniformInfo& uniform(int idx) const override {
        return fUniforms.item(idx);
    }

private:
    explicit GrD3DUniformHandler(GrGLSLProgramBuilder* program);

    SamplerHandle addSampler(const GrBackendFormat&, GrSamplerState, const skgpu::Swizzle&,
                             const char* name, const GrShaderCaps*) override;
    const char* samplerVariable(SamplerHandle handle) const override;
    skgpu::Swizzle samplerSwizzle(SamplerHandle handle) const override;
    void appendUniformDecls(GrShaderFlags visibility, SkString*) const override;
    UniformHandle internalAddUniformArray(const GrProcessor* owner,
                                          uint32_t visibility,
                                          SkSLType type,
                                          const char* name,
                                          bool mangleName,
                                          int arrayCount,
                                          const char** outName) override;

    UniformInfoArray fUniforms;
    UniformInfoArray fSamplers;
    skia_private::TArray<skgpu::Swizzle> fSamplerSwizzles;

    uint32_t fCurrentUBOOffset = 0;
    uint32_t fRTFlipOffset = 0;

    friend class GrD3DPipelineStateBuilder;

    using INHERITED = GrGLSLUniformHandler;
};

#endif
