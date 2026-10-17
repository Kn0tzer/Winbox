#version 460
#extension GL_KHR_shader_subgroup_vote : require

layout(set = 0, binding = 0, r32ui) uniform writeonly uimageBuffer _8;

layout(location = 0) flat in uint INDEX;
bool discard_state;

uint ByteAddressMask(uint index, uint stride)
{
    return index & (4294967295u / stride);
}

void discard_exit()
{
    if (discard_state)
    {
        discard;
    }
}

void main()
{
    discard_state = false;
    if (INDEX == 40u)
    {
        discard_state = true;
    }
    if (subgroupAllEqual(INDEX))
    {
        imageStore(_8, int(ByteAddressMask(INDEX, 4u)), uvec4(1u));
    }
    discard_exit();
}


#if 0
// SPIR-V disassembly
; SPIR-V
; Version: 1.3
; Generator: Unknown(30017); 21022
; Bound: 51
; Schema: 0
OpCapability Shader
OpCapability ImageBuffer
OpCapability GroupNonUniformVote
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %3 "main" %10
OpExecutionMode %3 OriginUpperLeft
OpName %3 "main"
OpName %10 "INDEX"
OpName %17 "discard_state"
OpName %26 "ByteAddressMask"
OpName %24 "index"
OpName %25 "stride"
OpName %43 "discard_exit"
OpDecorate %8 DescriptorSet 0
OpDecorate %8 Binding 0
OpDecorate %8 NonReadable
OpDecorate %10 Flat
OpDecorate %10 Location 0
%1 = OpTypeVoid
%2 = OpTypeFunction %1
%5 = OpTypeInt 32 0
%6 = OpTypeImage %5 Buffer 0 0 0 2 R32ui
%7 = OpTypePointer UniformConstant %6
%8 = OpVariable %7 UniformConstant
%9 = OpTypePointer Input %5
%10 = OpVariable %9 Input
%13 = OpTypeBool
%15 = OpConstant %5 40
%16 = OpTypePointer Private %13
%17 = OpVariable %16 Private
%18 = OpConstantFalse %13
%20 = OpConstant %5 3
%22 = OpConstant %5 2
%23 = OpTypeFunction %5 %5 %5
%29 = OpConstant %5 4294967295
%33 = OpConstant %5 4
%34 = OpConstant %5 1
%35 = OpTypeVector %5 4
%42 = OpConstantTrue %13
%3 = OpFunction %1 None %2
%4 = OpLabel
OpStore %17 %18
OpBranch %37
%37 = OpLabel
%11 = OpLoad %6 %8
%12 = OpLoad %5 %10
%14 = OpIEqual %13 %12 %15
OpSelectionMerge %39 None
OpBranchConditional %14 %38 %39
%38 = OpLabel
OpStore %17 %42
OpBranch %39
%39 = OpLabel
%19 = OpGroupNonUniformAllEqual %13 %20 %12
OpSelectionMerge %41 None
OpBranchConditional %19 %40 %41
%40 = OpLabel
%21 = OpShiftLeftLogical %5 %12 %22
%32 = OpFunctionCall %5 %26 %12 %33
%36 = OpCompositeConstruct %35 %34 %34 %34 %34
OpImageWrite %11 %32 %36
OpBranch %41
%41 = OpLabel
%49 = OpFunctionCall %1 %43
OpReturn
OpFunctionEnd
%26 = OpFunction %5 None %23
%24 = OpFunctionParameter %5
%25 = OpFunctionParameter %5
%27 = OpLabel
%28 = OpUDiv %5 %29 %25
%30 = OpBitwiseAnd %5 %24 %28
OpReturnValue %30
OpFunctionEnd
%43 = OpFunction %1 None %2
%44 = OpLabel
%47 = OpLoad %13 %17
OpSelectionMerge %46 None
OpBranchConditional %47 %45 %46
%45 = OpLabel
OpKill
%46 = OpLabel
OpReturn
OpFunctionEnd
#endif
