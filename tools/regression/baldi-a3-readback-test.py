#!/usr/bin/env python3
import pathlib, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[2]
src = (root/'source/opengl/gl64bridge.cpp').read_text()
start = src.index('GLenum g_allocationError =')
end = src.index('GLuint boundTexture', start)
with tempfile.TemporaryDirectory(prefix='bw64-gl-regression-') as generated_dir:
    generated = pathlib.Path(generated_dir)
    cpp = generated/'baldi-a3-readback-test.cpp'
    cpp.write_text('''#include <cstddef>
    #include <cassert>
    #include <cstdio>
    using GLenum=unsigned; using GLsizei=int; using GLint=int;
    #define GL_NO_ERROR 0
    ''' + src[start:end] + '''
    int main() {
        assert(packedReadStride(3,0,4,4)==12);
        assert(packedReadStride(3,0,8,4)==16);
        assert(packedReadStride(3,5,8,4)==24);
        assert(packedReadStride(1,0,1,4)==4);
        recordBridgeError(0); assert(g_allocationError==0);
        recordBridgeError(0x500); recordBridgeError(0x502);
        assert(g_allocationError==0x500);
        g_allocationError=0; recordBridgeError(0x502);
        assert(g_allocationError==0x502);
        puts("PASS readback pack layout and first-error regression vectors (7)");
    }
    ''')
    exe=generated/'baldi-a3-readback-test'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
