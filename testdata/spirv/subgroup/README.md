# Subgroup capability fixtures

Hand-written GLSL fragment shaders (kept beside the .spv) compiled with
glslangValidator -V --target-env vulkan1.1, one per GroupNonUniform family:

- ballot.frag.spv      -- GroupNonUniformBallot (64), OpGroupNonUniformBallot
- shuffle.frag.spv     -- GroupNonUniformShuffle (65), OpGroupNonUniformShuffle
- shuffledown.frag.spv -- GroupNonUniformShuffleRelative (66), OpGroupNonUniformShuffleDown
- arith.frag.spv       -- GroupNonUniformArithmetic (63), OpGroupNonUniformFAdd
- vote.frag.spv        -- GroupNonUniformVote (62), OpGroupNonUniformAny
- quad.frag.spv        -- GroupNonUniformQuad (68), OpGroupNonUniformQuadBroadcast, constant index (spirvfix R17 lowers to a subgroup shuffle)
- quadswap.frag.spv    -- GroupNonUniformQuad (68), OpGroupNonUniformQuadSwap, constant direction (R17 lowers to subgroupShuffleXor)
- clustered.frag.spv   -- GroupNonUniformClustered (67), OpGroupNonUniformIAdd clustered (naga rejects)

They pin the audit disposition of the subgroup capability family: 61-66 are
translated by the vendored naga to WGSL subgroup builtins (and advertised by
the bridge only when the page device carries the WebGPU subgroups feature);
67 rejects by name at the audit because naga rejects the capability
outright; 68 is admitted only for the constant quad forms R17 lowers
(mirror classifier in the bridge audit) and rejects by name otherwise.
