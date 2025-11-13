/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/sksl/codegen/SkSLHLSLCodeGenerator.h"

#include "include/core/SkSpan.h"
#include "include/core/SkTypes.h"
#include "include/private/base/SkTArray.h"
#include "src/base/SkEnumBitMask.h"
#include "src/base/SkStringView.h"
#include "src/core/SkTHash.h"
#include "src/core/SkTraceEvent.h"
#include "src/sksl/SkSLAnalysis.h"
#include "src/sksl/SkSLBuiltinTypes.h"
#include "src/sksl/SkSLCompiler.h"
#include "src/sksl/SkSLContext.h"
#include "src/sksl/SkSLDefines.h"
#include "src/sksl/SkSLErrorReporter.h"
#include "src/sksl/SkSLIntrinsicList.h"
#include "src/sksl/SkSLOperator.h"
#include "src/sksl/SkSLOutputStream.h"
#include "src/sksl/SkSLPosition.h"
#include "src/sksl/SkSLProgramSettings.h"
#include "src/sksl/SkSLString.h"
#include "src/sksl/SkSLStringStream.h"
#include "src/sksl/SkSLUtil.h"
#include "src/sksl/codegen/SkSLCodeGenTypes.h"
#include "src/sksl/codegen/SkSLCodeGenerator.h"
#include "src/sksl/codegen/SkSLNativeShader.h"
#include "src/sksl/ir/SkSLBinaryExpression.h"
#include "src/sksl/ir/SkSLBlock.h"
#include "src/sksl/ir/SkSLConstructor.h"
#include "src/sksl/ir/SkSLConstructorArrayCast.h"
#include "src/sksl/ir/SkSLConstructorCompound.h"
#include "src/sksl/ir/SkSLConstructorDiagonalMatrix.h"
#include "src/sksl/ir/SkSLDoStatement.h"
#include "src/sksl/ir/SkSLExpression.h"
#include "src/sksl/ir/SkSLExpressionStatement.h"
#include "src/sksl/ir/SkSLExtension.h"
#include "src/sksl/ir/SkSLFieldAccess.h"
#include "src/sksl/ir/SkSLForStatement.h"
#include "src/sksl/ir/SkSLFunctionCall.h"
#include "src/sksl/ir/SkSLFunctionDeclaration.h"
#include "src/sksl/ir/SkSLFunctionDefinition.h"
#include "src/sksl/ir/SkSLFunctionPrototype.h"
#include "src/sksl/ir/SkSLIRNode.h"
#include "src/sksl/ir/SkSLIfStatement.h"
#include "src/sksl/ir/SkSLIndexExpression.h"
#include "src/sksl/ir/SkSLInterfaceBlock.h"
#include "src/sksl/ir/SkSLLayout.h"
#include "src/sksl/ir/SkSLLiteral.h"
#include "src/sksl/ir/SkSLModifierFlags.h"
#include "src/sksl/ir/SkSLPostfixExpression.h"
#include "src/sksl/ir/SkSLPrefixExpression.h"
#include "src/sksl/ir/SkSLProgram.h"
#include "src/sksl/ir/SkSLProgramElement.h"
#include "src/sksl/ir/SkSLReturnStatement.h"
#include "src/sksl/ir/SkSLSetting.h"
#include "src/sksl/ir/SkSLStatement.h"
#include "src/sksl/ir/SkSLStructDefinition.h"
#include "src/sksl/ir/SkSLSwitchCase.h"
#include "src/sksl/ir/SkSLSwitchStatement.h"
#include "src/sksl/ir/SkSLSwizzle.h"
#include "src/sksl/ir/SkSLTernaryExpression.h"
#include "src/sksl/ir/SkSLType.h"
#include "src/sksl/ir/SkSLVarDeclarations.h"
#include "src/sksl/ir/SkSLVariable.h"
#include "src/sksl/ir/SkSLVariableReference.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace skia_private;

namespace SkSL {

class HLSLCodeGenerator : public CodeGenerator {
public:
    HLSLCodeGenerator(const Context* context,
                      const ShaderCaps* caps,
                      const Program* program,
                      OutputStream* out)
            : CodeGenerator(context, caps, program, out)
            , fLineEnding("\n") {}

    bool generateCode() override;

protected:
    using Precedence = OperatorPrecedence;

    // Requirements flags - what resources the shader needs
    using Requirements = int;
    static constexpr Requirements kNo_Requirements        = 0;
    static constexpr Requirements kInputs_Requirement     = 1 << 0;
    static constexpr Requirements kOutputs_Requirement    = 1 << 1;
    static constexpr Requirements kUniforms_Requirement   = 1 << 2;
    static constexpr Requirements kTextures_Requirement   = 1 << 3;
    static constexpr Requirements kGlobals_Requirement    = 1 << 4;
    static constexpr Requirements kFragCoord_Requirement  = 1 << 5;
    static constexpr Requirements kVertexID_Requirement   = 1 << 6;
    static constexpr Requirements kInstanceID_Requirement = 1 << 7;

    void write(std::string_view s);
    void writeLine(std::string_view s = std::string_view());
    void finishLine();

    void writeHeader();
    void writeUniformBuffer();
    void writeTextures();
    void writeInputStruct();
    void writeOutputStruct();
    void writeStructDefinitions();
    void writeGlobalVariables();
    void writeFunctions();

    std::string getTypeName(const Type& type);
    void writeType(const Type& type);
    void writeStructDefinition(const StructDefinition& s);
    void writeFunctionDeclaration(const FunctionDeclaration& f);
    void writeFunctionPrototype(const FunctionPrototype& f);
    void writeFunction(const FunctionDefinition& f);
    void writeModifiers(ModifierFlags flags);
    void writeVarDeclaration(const VarDeclaration& var);
    void writeGlobalVarDeclaration(const GlobalVarDeclaration& g);
    void writeInterfaceBlock(const InterfaceBlock& intf);

    void writeExpression(const Expression& expr, Precedence parentPrecedence);
    void writeBinaryExpression(const BinaryExpression& b, Precedence parentPrecedence);
    void writeTernaryExpression(const TernaryExpression& t, Precedence parentPrecedence);
    void writePrefixExpression(const PrefixExpression& p, Precedence parentPrecedence);
    void writePostfixExpression(const PostfixExpression& p, Precedence parentPrecedence);
    void writeIndexExpression(const IndexExpression& expr);
    void writeFieldAccess(const FieldAccess& f);
    void writeSwizzle(const Swizzle& swizzle);
    void writeLiteral(const Literal& l);
    void writeVariableReference(const VariableReference& ref);
    void writeFunctionCall(const FunctionCall& c);
    void writeConstructorCompound(const ConstructorCompound& c, Precedence parentPrecedence);
    void writeConstructorDiagonalMatrix(const ConstructorDiagonalMatrix& c,
                                       Precedence parentPrecedence);
    void writeConstructorArrayCast(const ConstructorArrayCast& c, Precedence parentPrecedence);
    void writeAnyConstructor(const AnyConstructor& c, Precedence parentPrecedence);

    void writeStatement(const Statement& s);
    void writeBlock(const Block& b);
    void writeIfStatement(const IfStatement& stmt);
    void writeForStatement(const ForStatement& f);
    void writeDoStatement(const DoStatement& d);
    void writeSwitchStatement(const SwitchStatement& s);
    void writeReturnStatement(const ReturnStatement& r);
    void writeExpressionStatement(const ExpressionStatement& s);

    // Helpers
    std::string getIntrinsicName(IntrinsicKind kind);
    std::string getSemanticName(const Variable& var);
    void determineRequirements(const FunctionDefinition& f);
    bool isInputVariable(const Variable& var);
    bool isOutputVariable(const Variable& var);

    const char* fLineEnding;
    int fIndentation = 0;
    bool fAtLineStart = true;

    Requirements fRequirements = kNo_Requirements;

    // Track variables and their semantic bindings
    THashMap<const Variable*, std::string> fVariableSemantics;
    THashMap<const Variable*, int> fTextureBindings;
    THashMap<const Variable*, int> fSamplerBindings;

    int fNextTextureBinding = 0;
    int fNextSamplerBinding = 0;
    int fNextUniformBinding = 0;

    // Input/output variables that need semantics
    std::vector<const Variable*> fInputVariables;
    std::vector<const Variable*> fOutputVariables;
    std::vector<const Variable*> fUniformVariables;
    std::vector<const Variable*> fTextureVariables;
};

bool ToHLSL(Program& program,
            const ShaderCaps* caps,
            OutputStream& out,
            ValidateSPIRVProc validateSPIRV) {
    TRACE_EVENT0("skia.shaders", "SkSL::ToHLSL");
    HLSLCodeGenerator cg(program.fContext.get(), caps, &program, &out);
    return cg.generateCode();
}

bool ToHLSL(Program& program,
            const ShaderCaps* caps,
            std::string* out,
            ValidateSPIRVProc validateSPIRV) {
    StringStream buffer;
    if (!ToHLSL(program, caps, buffer, validateSPIRV)) {
        return false;
    }
    *out = buffer.str();
    return true;
}

void HLSLCodeGenerator::write(std::string_view s) {
    if (s.empty()) {
        return;
    }
    if (fAtLineStart) {
        for (int i = 0; i < fIndentation; i++) {
            fOut->writeText("    ");
        }
    }
    fOut->writeText(std::string(s).c_str());
    fAtLineStart = false;
}

void HLSLCodeGenerator::writeLine(std::string_view s) {
    this->write(s);
    fOut->writeText(fLineEnding);
    fAtLineStart = true;
}

void HLSLCodeGenerator::finishLine() {
    if (!fAtLineStart) {
        this->writeLine();
    }
}

std::string HLSLCodeGenerator::getTypeName(const Type& type) {
    switch (type.typeKind()) {
        case Type::TypeKind::kScalar:
            if (type.matches(*fContext.fTypes.fFloat) ||
                type.matches(*fContext.fTypes.fHalf)) {
                return "float";
            }
            if (type.matches(*fContext.fTypes.fInt) ||
                type.matches(*fContext.fTypes.fShort)) {
                return "int";
            }
            if (type.matches(*fContext.fTypes.fUInt) ||
                type.matches(*fContext.fTypes.fUShort)) {
                return "uint";
            }
            if (type.matches(*fContext.fTypes.fBool)) {
                return "bool";
            }
            break;

        case Type::TypeKind::kVector:
            if (type.componentType().matches(*fContext.fTypes.fFloat) ||
                type.componentType().matches(*fContext.fTypes.fHalf)) {
                switch (type.columns()) {
                    case 2: return "float2";
                    case 3: return "float3";
                    case 4: return "float4";
                }
            }
            if (type.componentType().matches(*fContext.fTypes.fInt) ||
                type.componentType().matches(*fContext.fTypes.fShort)) {
                switch (type.columns()) {
                    case 2: return "int2";
                    case 3: return "int3";
                    case 4: return "int4";
                }
            }
            if (type.componentType().matches(*fContext.fTypes.fUInt) ||
                type.componentType().matches(*fContext.fTypes.fUShort)) {
                switch (type.columns()) {
                    case 2: return "uint2";
                    case 3: return "uint3";
                    case 4: return "uint4";
                }
            }
            if (type.componentType().matches(*fContext.fTypes.fBool)) {
                switch (type.columns()) {
                    case 2: return "bool2";
                    case 3: return "bool3";
                    case 4: return "bool4";
                }
            }
            break;

        case Type::TypeKind::kMatrix:
            if (type.componentType().matches(*fContext.fTypes.fFloat) ||
                type.componentType().matches(*fContext.fTypes.fHalf)) {
                return "float" + std::to_string(type.columns()) + "x" +
                       std::to_string(type.rows());
            }
            break;

        case Type::TypeKind::kArray:
            return this->getTypeName(type.componentType());

        case Type::TypeKind::kStruct:
            return std::string(type.name());

        case Type::TypeKind::kTexture:
            switch (type.dimensions()) {
                case SpvDim2D:
                    return "Texture2D";
                case SpvDimRect:
                    return "Texture2D";  // No direct equivalent, treat as Texture2D
                default:
                    break;
            }
            break;

        case Type::TypeKind::kSampler:
            return "SamplerState";

        case Type::TypeKind::kSeparateSampler:
            return "SamplerState";

        case Type::TypeKind::kVoid:
            return "void";

        default:
            break;
    }

    return std::string(type.name());
}

void HLSLCodeGenerator::writeType(const Type& type) {
    this->write(this->getTypeName(type));
}

std::string HLSLCodeGenerator::getIntrinsicName(IntrinsicKind kind) {
    // Map SkSL intrinsics to HLSL equivalents
    switch (kind) {
        case k_abs_IntrinsicKind: return "abs";
        case k_acos_IntrinsicKind: return "acos";
        case k_asin_IntrinsicKind: return "asin";
        case k_atan_IntrinsicKind: return "atan";  // Note: may be atan2, handled in writeFunctionCall
        case k_ceil_IntrinsicKind: return "ceil";
        case k_clamp_IntrinsicKind: return "clamp";
        case k_cos_IntrinsicKind: return "cos";
        case k_cross_IntrinsicKind: return "cross";
        case k_degrees_IntrinsicKind: return "degrees";
        case k_distance_IntrinsicKind: return "distance";
        case k_dot_IntrinsicKind: return "dot";
        case k_exp_IntrinsicKind: return "exp";
        case k_exp2_IntrinsicKind: return "exp2";
        case k_faceforward_IntrinsicKind: return "faceforward";
        case k_floor_IntrinsicKind: return "floor";
        case k_fma_IntrinsicKind: return "fma";
        case k_fract_IntrinsicKind: return "frac";  // Note: frac in HLSL, fract in GLSL
        case k_inversesqrt_IntrinsicKind: return "rsqrt";  // Note: rsqrt in HLSL
        case k_length_IntrinsicKind: return "length";
        case k_log_IntrinsicKind: return "log";
        case k_log2_IntrinsicKind: return "log2";
        case k_max_IntrinsicKind: return "max";
        case k_min_IntrinsicKind: return "min";
        case k_mix_IntrinsicKind: return "lerp";  // Note: lerp in HLSL, mix in GLSL
        case k_mod_IntrinsicKind: return "fmod";
        case k_normalize_IntrinsicKind: return "normalize";
        case k_pow_IntrinsicKind: return "pow";
        case k_radians_IntrinsicKind: return "radians";
        case k_reflect_IntrinsicKind: return "reflect";
        case k_refract_IntrinsicKind: return "refract";
        case k_round_IntrinsicKind: return "round";
        case k_saturate_IntrinsicKind: return "saturate";
        case k_sign_IntrinsicKind: return "sign";
        case k_sin_IntrinsicKind: return "sin";
        case k_smoothstep_IntrinsicKind: return "smoothstep";
        case k_sqrt_IntrinsicKind: return "sqrt";
        case k_step_IntrinsicKind: return "step";
        case k_tan_IntrinsicKind: return "tan";
        case k_transpose_IntrinsicKind: return "transpose";
        case k_trunc_IntrinsicKind: return "trunc";
        case k_all_IntrinsicKind: return "all";
        case k_any_IntrinsicKind: return "any";
        case k_equal_IntrinsicKind: return "==";  // Component-wise comparison
        case k_notEqual_IntrinsicKind: return "!=";
        case k_greaterThan_IntrinsicKind: return ">";
        case k_greaterThanEqual_IntrinsicKind: return ">=";
        case k_lessThan_IntrinsicKind: return "<";
        case k_lessThanEqual_IntrinsicKind: return "<=";
        case k_dFdx_IntrinsicKind: return "ddx";
        case k_dFdy_IntrinsicKind: return "ddy";
        case k_fwidth_IntrinsicKind: return "fwidth";
        case k_sample_IntrinsicKind: return "Sample";
        default:
            return "unknown";
    }
}

bool HLSLCodeGenerator::generateCode() {
    // Write HLSL shader model header
    this->writeHeader();

    // Analyze the program to determine requirements
    for (const ProgramElement* element : fProgram.elements()) {
        if (element->is<FunctionDefinition>()) {
            const FunctionDefinition& func = element->as<FunctionDefinition>();
            if (func.declaration().isMain()) {
                this->determineRequirements(func);
            }
        }
    }

    // Write structures
    this->writeStructDefinitions();

    // Write uniform buffer
    if (fRequirements & kUniforms_Requirement) {
        this->writeUniformBuffer();
    }

    // Write textures and samplers
    if (fRequirements & kTextures_Requirement) {
        this->writeTextures();
    }

    // Write input/output structs for vertex/fragment shaders
    if (fRequirements & kInputs_Requirement) {
        this->writeInputStruct();
    }
    if (fRequirements & kOutputs_Requirement) {
        this->writeOutputStruct();
    }

    // Write global variables
    this->writeGlobalVariables();

    // Write all functions
    this->writeFunctions();

    return fContext.fErrors->errorCount() == 0;
}

void HLSLCodeGenerator::writeHeader() {
    this->writeLine("// Generated HLSL code for Direct3D 12");
    this->writeLine("// Shader Model 5.1");
    this->writeLine();
}

void HLSLCodeGenerator::determineRequirements(const FunctionDefinition& f) {
    // Simple analysis - scan for variable usage to determine requirements
    // This is a simplified version; a full implementation would use SkSL's analysis utilities

    // For now, enable all requirements - a real implementation would scan the AST
    fRequirements = kInputs_Requirement | kOutputs_Requirement | kUniforms_Requirement;
}

void HLSLCodeGenerator::writeUniformBuffer() {
    this->writeLine("cbuffer UniformBuffer : register(b0) {");
    fIndentation++;

    for (const ProgramElement* element : fProgram.elements()) {
        if (element->is<GlobalVarDeclaration>()) {
            const GlobalVarDeclaration& global = element->as<GlobalVarDeclaration>();
            const VarDeclaration& decl = global.varDeclaration();
            const Variable& var = *decl.var();

            if (var.modifierFlags().isUniform()) {
                this->write("    ");
                this->writeType(var.type());
                this->write(" ");
                this->write(var.name());
                if (var.type().isArray()) {
                    this->write("[");
                    this->write(std::to_string(var.type().columns()));
                    this->write("]");
                }
                this->writeLine(";");
            }
        }
    }

    fIndentation--;
    this->writeLine("};");
    this->writeLine();
}

void HLSLCodeGenerator::writeTextures() {
    int textureIndex = 0;
    int samplerIndex = 0;

    for (const ProgramElement* element : fProgram.elements()) {
        if (element->is<GlobalVarDeclaration>()) {
            const GlobalVarDeclaration& global = element->as<GlobalVarDeclaration>();
            const VarDeclaration& decl = global.varDeclaration();
            const Variable& var = *decl.var();

            if (var.type().typeKind() == Type::TypeKind::kTexture) {
                this->write("Texture2D ");
                this->write(var.name());
                this->write(" : register(t");
                this->write(std::to_string(textureIndex++));
                this->writeLine(");");

                // Also create corresponding sampler
                this->write("SamplerState ");
                this->write(var.name());
                this->write("_sampler : register(s");
                this->write(std::to_string(samplerIndex++));
                this->writeLine(");");
            }
        }
    }

    if (textureIndex > 0) {
        this->writeLine();
    }
}

void HLSLCodeGenerator::writeInputStruct() {
    this->writeLine("struct VSInput {");
    fIndentation++;

    int location = 0;
    for (const ProgramElement* element : fProgram.elements()) {
        if (element->is<GlobalVarDeclaration>()) {
            const GlobalVarDeclaration& global = element->as<GlobalVarDeclaration>();
            const VarDeclaration& decl = global.varDeclaration();
            const Variable& var = *decl.var();

            if (var.modifierFlags()& ModifierFlag::kIn ||
                (fProgram.fConfig->fKind == ProgramKind::kVertex &&
                 !var.modifierFlags().isUniform())) {
                this->write("    ");
                this->writeType(var.type());
                this->write(" ");
                this->write(var.name());
                this->write(" : TEXCOORD");
                this->write(std::to_string(location++));
                this->writeLine(";");
            }
        }
    }

    fIndentation--;
    this->writeLine("};");
    this->writeLine();
}

void HLSLCodeGenerator::writeOutputStruct() {
    this->writeLine("struct VSOutput {");
    fIndentation++;

    bool hasPosition = false;
    int location = 0;

    for (const ProgramElement* element : fProgram.elements()) {
        if (element->is<GlobalVarDeclaration>()) {
            const GlobalVarDeclaration& global = element->as<GlobalVarDeclaration>();
            const VarDeclaration& decl = global.varDeclaration();
            const Variable& var = *decl.var();

            if (var.modifierFlags()& ModifierFlag::kOut) {
                this->write("    ");
                this->writeType(var.type());
                this->write(" ");
                this->write(var.name());

                // Position gets SV_Position semantic
                if (var.name() == "sk_Position") {
                    this->write(" : SV_Position");
                    hasPosition = true;
                } else if (fProgram.fConfig->fKind == ProgramKind::kFragment) {
                    // Fragment shader outputs get SV_Target
                    this->write(" : SV_Target");
                    this->write(std::to_string(location++));
                } else {
                    // Other outputs get TEXCOORD
                    this->write(" : TEXCOORD");
                    this->write(std::to_string(location++));
                }

                this->writeLine(";");
            }
        }
    }

    fIndentation--;
    this->writeLine("};");
    this->writeLine();
}

void HLSLCodeGenerator::writeStructDefinitions() {
    for (const ProgramElement* element : fProgram.elements()) {
        if (element->is<StructDefinition>()) {
            this->writeStructDefinition(element->as<StructDefinition>());
            this->writeLine();
        }
    }
}

void HLSLCodeGenerator::writeStructDefinition(const StructDefinition& s) {
    this->write("struct ");
    this->write(s.type().name());
    this->writeLine(" {");
    fIndentation++;

    for (const auto& field : s.type().fields()) {
        this->write("    ");
        this->writeType(*field.fType);
        this->write(" ");
        this->write(field.fName);
        this->writeLine(";");
    }

    fIndentation--;
    this->writeLine("};");
}

void HLSLCodeGenerator::writeGlobalVariables() {
    for (const ProgramElement* element : fProgram.elements()) {
        if (element->is<GlobalVarDeclaration>()) {
            const GlobalVarDeclaration& global = element->as<GlobalVarDeclaration>();
            const Variable& var = *global.varDeclaration().var();

            // Skip uniforms, inputs, outputs (already handled)
            if (var.modifierFlags().isUniform() ||
                var.modifierFlags()& ModifierFlag::kIn ||
                var.modifierFlags()& ModifierFlag::kOut ||
                var.type().typeKind() == Type::TypeKind::kTexture) {
                continue;
            }

            // Write global variable
            this->writeVarDeclaration(global.varDeclaration());
            this->writeLine(";");
        }
    }
}

void HLSLCodeGenerator::writeFunctions() {
    for (const ProgramElement* element : fProgram.elements()) {
        if (element->is<FunctionDefinition>()) {
            this->writeFunction(element->as<FunctionDefinition>());
            this->writeLine();
        }
    }
}

void HLSLCodeGenerator::writeFunction(const FunctionDefinition& f) {
    const FunctionDeclaration& decl = f.declaration();

    // Return type
    this->writeType(decl.returnType());
    this->write(" ");

    // Function name (main stays main)
    this->write(decl.name());

    // Parameters
    this->write("(");
    bool first = true;
    for (const Variable* param : decl.parameters()) {
        if (!first) {
            this->write(", ");
        }
        first = false;

        this->writeType(param->type());
        this->write(" ");
        this->write(param->name());
    }
    this->write(")");

    // Function body
    this->writeStatement(*f.body());
    this->writeLine();
}

void HLSLCodeGenerator::writeVarDeclaration(const VarDeclaration& var) {
    this->writeType(var.var()->type());
    this->write(" ");
    this->write(var.var()->name());

    if (var.value()) {
        this->write(" = ");
        this->writeExpression(*var.value(), Precedence::kExpression);
    }
}

void HLSLCodeGenerator::writeStatement(const Statement& s) {
    switch (s.kind()) {
        case Statement::Kind::kBlock:
            this->writeBlock(s.as<Block>());
            break;
        case Statement::Kind::kExpression:
            this->writeExpressionStatement(s.as<ExpressionStatement>());
            break;
        case Statement::Kind::kReturn:
            this->writeReturnStatement(s.as<ReturnStatement>());
            break;
        case Statement::Kind::kIf:
            this->writeIfStatement(s.as<IfStatement>());
            break;
        case Statement::Kind::kFor:
            this->writeForStatement(s.as<ForStatement>());
            break;
        case Statement::Kind::kDo:
            this->writeDoStatement(s.as<DoStatement>());
            break;
        case Statement::Kind::kSwitch:
            this->writeSwitchStatement(s.as<SwitchStatement>());
            break;
        case Statement::Kind::kVarDeclaration:
            this->writeVarDeclaration(s.as<VarDeclaration>());
            this->writeLine(";");
            break;
        case Statement::Kind::kDiscard:
            this->writeLine("discard;");
            break;
        case Statement::Kind::kBreak:
            this->writeLine("break;");
            break;
        case Statement::Kind::kContinue:
            this->writeLine("continue;");
            break;
        default:
            SkDEBUGFAILF("unsupported statement: %s", s.description().c_str());
            break;
    }
}

void HLSLCodeGenerator::writeBlock(const Block& b) {
    this->writeLine("{");
    fIndentation++;
    for (const std::unique_ptr<Statement>& stmt : b.children()) {
        this->writeStatement(*stmt);
    }
    fIndentation--;
    this->writeLine("}");
}

void HLSLCodeGenerator::writeIfStatement(const IfStatement& stmt) {
    this->write("if (");
    this->writeExpression(*stmt.test(), Precedence::kExpression);
    this->write(") ");
    this->writeStatement(*stmt.ifTrue());
    if (stmt.ifFalse()) {
        this->write("else ");
        this->writeStatement(*stmt.ifFalse());
    }
}

void HLSLCodeGenerator::writeForStatement(const ForStatement& f) {
    this->write("for (");
    if (f.initializer()) {
        this->writeStatement(*f.initializer());
    } else {
        this->write("; ");
    }
    if (f.test()) {
        this->writeExpression(*f.test(), Precedence::kExpression);
    }
    this->write("; ");
    if (f.next()) {
        this->writeExpression(*f.next(), Precedence::kExpression);
    }
    this->write(") ");
    this->writeStatement(*f.statement());
}

void HLSLCodeGenerator::writeDoStatement(const DoStatement& d) {
    this->write("do ");
    this->writeStatement(*d.statement());
    this->write(" while (");
    this->writeExpression(*d.test(), Precedence::kExpression);
    this->writeLine(");");
}

void HLSLCodeGenerator::writeSwitchStatement(const SwitchStatement& s) {
    this->write("switch (");
    this->writeExpression(*s.value(), Precedence::kExpression);
    this->writeLine(") {");

    for (const std::unique_ptr<Statement>& stmt : s.cases()) {
        const SwitchCase& c = stmt->as<SwitchCase>();
        if (c.isDefault()) {
            this->writeLine("default:");
        } else {
            this->write("case ");
            this->write(std::to_string(c.value()));
            this->writeLine(":");
        }
        fIndentation++;
        this->writeStatement(*c.statement());
        fIndentation--;
    }

    this->writeLine("}");
}

void HLSLCodeGenerator::writeReturnStatement(const ReturnStatement& r) {
    this->write("return");
    if (r.expression()) {
        this->write(" ");
        this->writeExpression(*r.expression(), Precedence::kExpression);
    }
    this->writeLine(";");
}

void HLSLCodeGenerator::writeExpressionStatement(const ExpressionStatement& s) {
    this->writeExpression(*s.expression(), Precedence::kExpression);
    this->writeLine(";");
}

void HLSLCodeGenerator::writeExpression(const Expression& expr, Precedence parentPrecedence) {
    switch (expr.kind()) {
        case Expression::Kind::kBinary:
            this->writeBinaryExpression(expr.as<BinaryExpression>(), parentPrecedence);
            break;
        case Expression::Kind::kLiteral:
            this->writeLiteral(expr.as<Literal>());
            break;
        case Expression::Kind::kVariableReference:
            this->writeVariableReference(expr.as<VariableReference>());
            break;
        case Expression::Kind::kPrefix:
            this->writePrefixExpression(expr.as<PrefixExpression>(), parentPrecedence);
            break;
        case Expression::Kind::kPostfix:
            this->writePostfixExpression(expr.as<PostfixExpression>(), parentPrecedence);
            break;
        case Expression::Kind::kTernary:
            this->writeTernaryExpression(expr.as<TernaryExpression>(), parentPrecedence);
            break;
        case Expression::Kind::kIndex:
            this->writeIndexExpression(expr.as<IndexExpression>());
            break;
        case Expression::Kind::kFieldAccess:
            this->writeFieldAccess(expr.as<FieldAccess>());
            break;
        case Expression::Kind::kSwizzle:
            this->writeSwizzle(expr.as<Swizzle>());
            break;
        case Expression::Kind::kFunctionCall:
            this->writeFunctionCall(expr.as<FunctionCall>());
            break;
        case Expression::Kind::kConstructorCompound:
            this->writeConstructorCompound(expr.as<ConstructorCompound>(), parentPrecedence);
            break;
        case Expression::Kind::kConstructorDiagonalMatrix:
            this->writeConstructorDiagonalMatrix(expr.as<ConstructorDiagonalMatrix>(),
                                                 parentPrecedence);
            break;
        case Expression::Kind::kConstructorArrayCast:
            this->writeConstructorArrayCast(expr.as<ConstructorArrayCast>(), parentPrecedence);
            break;
        default:
            if (expr.isAnyConstructor()) {
                this->writeAnyConstructor(expr.asAnyConstructor(), parentPrecedence);
            } else {
                SkDEBUGFAILF("unsupported expression: %s", expr.description().c_str());
            }
            break;
    }
}

void HLSLCodeGenerator::writeBinaryExpression(const BinaryExpression& b,
                                              Precedence parentPrecedence) {
    const Expression& left = *b.left();
    const Expression& right = *b.right();
    Operator op = b.getOperator();
    Precedence precedence = op.getBinaryPrecedence();

    bool needParens = precedence >= parentPrecedence;
    if (needParens) {
        this->write("(");
    }

    this->writeExpression(left, precedence);
    this->write(" ");
    this->write(op.operatorName());
    this->write(" ");
    this->writeExpression(right, precedence);

    if (needParens) {
        this->write(")");
    }
}

void HLSLCodeGenerator::writeTernaryExpression(const TernaryExpression& t,
                                               Precedence parentPrecedence) {
    bool needParens = Precedence::kTernary >= parentPrecedence;
    if (needParens) {
        this->write("(");
    }

    this->writeExpression(*t.test(), Precedence::kTernary);
    this->write(" ? ");
    this->writeExpression(*t.ifTrue(), Precedence::kTernary);
    this->write(" : ");
    this->writeExpression(*t.ifFalse(), Precedence::kTernary);

    if (needParens) {
        this->write(")");
    }
}

void HLSLCodeGenerator::writePrefixExpression(const PrefixExpression& p,
                                              Precedence parentPrecedence) {
    bool needParens = Precedence::kPrefix >= parentPrecedence;
    if (needParens) {
        this->write("(");
    }

    this->write(p.getOperator().operatorName());
    this->writeExpression(*p.operand(), Precedence::kPrefix);

    if (needParens) {
        this->write(")");
    }
}

void HLSLCodeGenerator::writePostfixExpression(const PostfixExpression& p,
                                               Precedence parentPrecedence) {
    bool needParens = Precedence::kPostfix >= parentPrecedence;
    if (needParens) {
        this->write("(");
    }

    this->writeExpression(*p.operand(), Precedence::kPostfix);
    this->write(p.getOperator().operatorName());

    if (needParens) {
        this->write(")");
    }
}

void HLSLCodeGenerator::writeIndexExpression(const IndexExpression& expr) {
    this->writeExpression(*expr.base(), Precedence::kPostfix);
    this->write("[");
    this->writeExpression(*expr.index(), Precedence::kExpression);
    this->write("]");
}

void HLSLCodeGenerator::writeFieldAccess(const FieldAccess& f) {
    this->writeExpression(*f.base(), Precedence::kPostfix);
    this->write(".");
    this->write(f.base()->type().fields()[f.fieldIndex()].fName);
}

void HLSLCodeGenerator::writeSwizzle(const Swizzle& swizzle) {
    this->writeExpression(*swizzle.base(), Precedence::kPostfix);
    this->write(".");
    this->write(Swizzle::MaskString(swizzle.components()));
}

void HLSLCodeGenerator::writeLiteral(const Literal& l) {
    const Type& type = l.type();
    if (type.matches(*fContext.fTypes.fFloat) ||
        type.matches(*fContext.fTypes.fHalf)) {
        this->write(skstd::to_string(l.floatValue()));
    } else if (type.matches(*fContext.fTypes.fInt) ||
               type.matches(*fContext.fTypes.fShort)) {
        this->write(std::to_string(l.intValue()));
    } else if (type.matches(*fContext.fTypes.fUInt) ||
               type.matches(*fContext.fTypes.fUShort)) {
        this->write(std::to_string(l.intValue()));
        this->write("u");
    } else if (type.matches(*fContext.fTypes.fBool)) {
        this->write(l.boolValue() ? "true" : "false");
    } else {
        SkDEBUGFAILF("unsupported literal type");
    }
}

void HLSLCodeGenerator::writeVariableReference(const VariableReference& ref) {
    this->write(ref.variable()->name());
}

void HLSLCodeGenerator::writeFunctionCall(const FunctionCall& c) {
    const FunctionDeclaration& decl = c.function();

    // Check if this is an intrinsic that needs special handling
    if (decl.intrinsicKind() != kNotIntrinsic) {
        // Special case for atan: use atan2 if there are 2 arguments
        if (decl.intrinsicKind() == k_atan_IntrinsicKind && c.arguments().size() == 2) {
            this->write("atan2");
        } else {
            std::string intrinsicName = this->getIntrinsicName(decl.intrinsicKind());
            this->write(intrinsicName);
        }
    } else {
        this->write(decl.name());
    }

    this->write("(");
    bool first = true;
    for (const auto& arg : c.arguments()) {
        if (!first) {
            this->write(", ");
        }
        first = false;
        this->writeExpression(*arg, Precedence::kSequence);
    }
    this->write(")");
}

void HLSLCodeGenerator::writeConstructorCompound(const ConstructorCompound& c,
                                                 Precedence parentPrecedence) {
    this->writeType(c.type());
    this->write("(");
    bool first = true;
    for (const auto& arg : c.arguments()) {
        if (!first) {
            this->write(", ");
        }
        first = false;
        this->writeExpression(*arg, Precedence::kSequence);
    }
    this->write(")");
}

void HLSLCodeGenerator::writeConstructorDiagonalMatrix(const ConstructorDiagonalMatrix& c,
                                                       Precedence parentPrecedence) {
    this->writeType(c.type());
    this->write("(");

    // HLSL requires all matrix elements, not just diagonal
    const Type& type = c.type();
    SkASSERT(type.isMatrix());

    for (int col = 0; col < type.columns(); col++) {
        for (int row = 0; row < type.rows(); row++) {
            if (col != 0 || row != 0) {
                this->write(", ");
            }
            if (col == row) {
                this->writeExpression(*c.argument(), Precedence::kSequence);
            } else {
                this->write("0");
            }
        }
    }

    this->write(")");
}

void HLSLCodeGenerator::writeConstructorArrayCast(const ConstructorArrayCast& c,
                                                  Precedence parentPrecedence) {
    this->writeType(c.type());
    this->write("(");
    this->writeExpression(*c.argument(), Precedence::kSequence);
    this->write(")");
}

void HLSLCodeGenerator::writeAnyConstructor(const AnyConstructor& c,
                                           Precedence parentPrecedence) {
    this->writeType(c.type());
    this->write("(");
    bool first = true;
    for (const auto& arg : c.argumentSpan()) {
        if (!first) {
            this->write(", ");
        }
        first = false;
        this->writeExpression(*arg, Precedence::kSequence);
    }
    this->write(")");
}

void HLSLCodeGenerator::writeGlobalVarDeclaration(const GlobalVarDeclaration& g) {
    this->writeVarDeclaration(g.varDeclaration());
    this->writeLine(";");
}

void HLSLCodeGenerator::writeInterfaceBlock(const InterfaceBlock& intf) {
    // HLSL doesn't have interface blocks in the same way - handle via cbuffer/uniform
}

void HLSLCodeGenerator::writeFunctionPrototype(const FunctionPrototype& f) {
    this->writeFunctionDeclaration(f.declaration());
    this->writeLine(";");
}

void HLSLCodeGenerator::writeFunctionDeclaration(const FunctionDeclaration& f) {
    this->writeType(f.returnType());
    this->write(" ");
    this->write(f.name());
    this->write("(");
    bool first = true;
    for (const Variable* param : f.parameters()) {
        if (!first) {
            this->write(", ");
        }
        first = false;
        this->writeType(param->type());
        this->write(" ");
        this->write(param->name());
    }
    this->write(")");
}

void HLSLCodeGenerator::writeModifiers(ModifierFlags flags) {
    // HLSL modifiers are handled differently - mostly through semantics
}

}  // namespace SkSL
