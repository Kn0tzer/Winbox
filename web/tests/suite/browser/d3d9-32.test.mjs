import D3D9ComprehensiveTest from './d3d9-comprehensive.test.mjs';
// Run the same bounded boot and workload contracts through CPU32/Vulkan32.
export default class D3D9Cpu32Test extends D3D9ComprehensiveTest {
  constructor() { super({ arch: 'x86' }); }
}
