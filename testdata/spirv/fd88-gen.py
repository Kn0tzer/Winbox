variants = [
  ('g_rt_push',  'rt',  'push', ['Shader','RuntimeDescriptorArray','SampledImageArrayDynamicIndexing']),
  ('h_rt_data',  'rt',  'data', ['Shader','RuntimeDescriptorArray','SampledImageArrayDynamicIndexing']),
  ('i_fx_data',  'fx4', 'data', ['Shader','SampledImageArrayDynamicIndexing']),
  ('j_fx_const', 'fx4', 'const',['Shader']),
  ('k_rt_const', 'rt',  'const',['Shader','RuntimeDescriptorArray','SampledImageArrayDynamicIndexing']),
  ('l_rt_data_nocap29', 'rt', 'data', ['Shader','RuntimeDescriptorArray']),
  ('m_fx_push',  'fx4', 'push', ['Shader','SampledImageArrayDynamicIndexing']),
  ('n_rt_push_nocap29', 'rt', 'push', ['Shader','RuntimeDescriptorArray']),
]
for name, arr, idx, caps in variants:
    head = []
    for c in caps: head.append('OpCapability ' + c)
    head.append('OpMemoryModel Logical GLSL450')
    head.append('OpEntryPoint Fragment %main "main" %texs %smps %pc %ssbo %out')
    head.append('OpExecutionMode %main OriginUpperLeft')
    annot = []
    annot.append('OpDecorate %texs DescriptorSet 3')
    annot.append('OpDecorate %texs Binding 0')
    annot.append('OpDecorate %smps DescriptorSet 0')
    annot.append('OpDecorate %smps Binding 0')
    annot.append('OpDecorate %pcblk Block')
    annot.append('OpMemberDecorate %pcblk 0 Offset 0')
    annot.append('OpDecorate %ssboblk Block')
    annot.append('OpMemberDecorate %ssboblk 0 Offset 0')
    annot.append('OpDecorate %ssbo DescriptorSet 3')
    annot.append('OpDecorate %ssbo Binding 2')
    annot.append('OpDecorate %out Location 0')
    types = []
    types.append('%void = OpTypeVoid')
    types.append('%fn = OpTypeFunction %void')
    types.append('%float = OpTypeFloat 32')
    types.append('%v4 = OpTypeVector %float 4')
    types.append('%v3 = OpTypeVector %float 3')
    types.append('%uint = OpTypeInt 32 0')
    types.append('%u0 = OpConstant %uint 0')
    types.append('%u4 = OpConstant %uint 4')
    types.append('%u16 = OpConstant %uint 16')
    types.append('%uffff = OpConstant %uint 65535')
    types.append('%f025 = OpConstant %float 0.25')
    types.append('%coord = OpConstantComposite %v3 %f025 %f025 %f025')
    types.append('%img = OpTypeImage %float 3D 0 0 0 1 Unknown')
    if arr == 'rt': types.append('%arrt = OpTypeRuntimeArray %img')
    else: types.append('%arrt = OpTypeArray %img %u4')
    types.append('%parr = OpTypePointer UniformConstant %arrt')
    types.append('%pimg = OpTypePointer UniformConstant %img')
    types.append('%smp = OpTypeSampler')
    types.append('%sarr = OpTypeRuntimeArray %smp')
    types.append('%psarr = OpTypePointer UniformConstant %sarr')
    types.append('%psmp = OpTypePointer UniformConstant %smp')
    types.append('%simg = OpTypeSampledImage %img')
    types.append('%pout = OpTypePointer Output %v4')
    types.append('%pcblk = OpTypeStruct %uint')
    types.append('%ppc = OpTypePointer PushConstant %pcblk')
    types.append('%ppcu = OpTypePointer PushConstant %uint')
    types.append('%rtu = OpTypeRuntimeArray %uint')
    types.append('%ssboblk = OpTypeStruct %rtu')
    types.append('%pssbo = OpTypePointer StorageBuffer %ssboblk')
    types.append('%pssbou = OpTypePointer StorageBuffer %uint')
    vars_ = []
    vars_.append('%texs = OpVariable %parr UniformConstant')
    vars_.append('%smps = OpVariable %psarr UniformConstant')
    vars_.append('%pc = OpVariable %ppc PushConstant')
    vars_.append('%ssbo = OpVariable %pssbo StorageBuffer')
    vars_.append('%out = OpVariable %pout Output')
    fn = []
    fn.append('%main = OpFunction %void None %fn')
    fn.append('%lab = OpLabel')
    if idx == 'const':
        fn.append('%idx = OpCopyObject %uint %u0')
    elif idx == 'push':
        fn.append('%pp = OpAccessChain %ppcu %pc %u0')
        fn.append('%idx = OpLoad %uint %pp')
    else:
        fn.append('%sp0 = OpAccessChain %pssbou %ssbo %u0 %u0')
        fn.append('%raw = OpLoad %uint %sp0')
        fn.append('%shr = OpShiftRightLogical %uint %raw %u16')
        fn.append('%idx = OpBitwiseAnd %uint %shr %uffff')
    fn.append('%ip = OpAccessChain %pimg %texs %idx')
    fn.append('%im = OpLoad %img %ip')
    fn.append('%sp = OpAccessChain %psmp %smps %u0')
    fn.append('%sm = OpLoad %smp %sp')
    fn.append('%si = OpSampledImage %simg %im %sm')
    fn.append('%r = OpImageSampleImplicitLod %v4 %si %coord')
    fn.append('OpStore %out %r')
    fn.append('OpReturn')
    fn.append('OpFunctionEnd')
    open('/home/ubuntu/wt-fd88/test-results/fd88/asm/' + name + '.spvasm', 'w').write('\n'.join(head + annot + types + vars_ + fn) + '\n')
print('written')
