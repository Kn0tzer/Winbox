import { test } from 'node:test';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { execFileSync } from 'node:child_process';

const repo = resolve(import.meta.dirname, '../../../..');
test('stored ZIP seeks are bounded and preserve ZIP64 positions and sequential CRC checks', () => {
  const dir = mkdtempSync(join(tmpdir(), 'bw-stored-seek-'));
  try {
    execFileSync('python3', ['-c', String.raw`
import zipfile,struct,pathlib,sys
p=pathlib.Path(sys.argv[1]); data=bytes(range(256))*256
with zipfile.ZipFile(p/'small.zip','w') as z:
 z.writestr('stored',data,compress_type=zipfile.ZIP_STORED)
 z.writestr('deflated',data,compress_type=zipfile.ZIP_DEFLATED)
b=bytearray((p/'small.zip').read_bytes()); b[30+len('stored')+12]^=1;(p/'bad.zip').write_bytes(b)
name=b'large.bin'; size=(1<<32)+128; extra=struct.pack('<HHQQ',1,16,size,size)
local=struct.pack('<IHHHHHIIIHH',0x04034b50,45,0,0,0,0,0,0xffffffff,0xffffffff,len(name),len(extra))+name+extra
central=struct.pack('<IHHHHHHIIIHHHHHII',0x02014b50,45,45,0,0,0,0,0,0xffffffff,0xffffffff,len(name),len(extra),0,0,0,0,0)+name+extra
cd=len(local)+size
with (p/'large.zip').open('wb') as f:
 f.write(local);f.seek(len(local)+(1<<32)+17);f.write(b'ZIP64-MARKER')
 f.seek(cd);f.write(central)
 f.write(struct.pack('<IQHHIIQQQQ',0x06064b50,44,45,45,0,0,1,1,len(central),cd))
 f.write(struct.pack('<IIQI',0x07064b50,0,cd+len(central),1))
 f.write(struct.pack('<IHHHHIIH',0x06054b50,0,0,1,1,len(central),0xffffffff,0))
`, dir]);
    const source = String.raw`
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "unzip.h"
static unsigned long long read_bytes;
static uLong ZCALLBACK count_read(voidpf opaque,voidpf stream,void* buf,uLong size) {
 (void)opaque;read_bytes+=size;return fread(buf,1,size,(FILE*)stream);
}
static unzFile open_zip(const char* path,const char* member) {
 zlib_filefunc64_def funcs;fill_fopen64_filefunc(&funcs);funcs.zread_file=count_read;
 unzFile z=unzOpen2_64(path,&funcs);assert(z);assert(unzLocateFile(z,member,1)==UNZ_OK);
 assert(unzOpenCurrentFile(z)==UNZ_OK);return z;
}
static void all(unzFile z) { unsigned char buf[4096];int n;do {n=unzReadCurrentFile(z,buf,sizeof buf);assert(n>=0);}while(n); }
int main(int argc,char** argv) {
 assert(argc==4);unsigned char buf[80];unzFile z=open_zip(argv[1],"stored");
 read_bytes=0;assert(unzSeekStoredFile64(z,65500)==UNZ_OK);assert(unzReadCurrentFile(z,buf,36)==36);
 for(int i=0;i<36;i++)assert(buf[i]==(unsigned char)(65500+i));assert(read_bytes<=65536);
 assert(unzSeekStoredFile64(z,3)==UNZ_OK);assert(unzReadCurrentFile(z,buf,40)==40);
 for(int i=0;i<40;i++)assert(buf[i]==(unsigned char)(i+3));assert(unztell64(z)==43);
 assert(unzSeekStoredFile64(z,65537)==UNZ_PARAMERROR);assert(unztell64(z)==43);
 assert(unzSeekStoredFile64(z,65536)==UNZ_OK);assert(unzReadCurrentFile(z,buf,1)==0);
 assert(unzCloseCurrentFile(z)==UNZ_OK);
 assert(unzOpenCurrentFile(z)==UNZ_OK);assert(unzSeekStoredFile64(z,30)==UNZ_OK);
 assert(unzSeekStoredFile64(z,0)==UNZ_OK);all(z);assert(unzCloseCurrentFile(z)==UNZ_OK);unzClose(z);
 z=open_zip(argv[1],"deflated");assert(unzReadCurrentFile(z,buf,5)==5);
 assert(unzSeekStoredFile64(z,300)==UNZ_PARAMERROR);assert(unztell64(z)==5);
 assert(unzReadCurrentFile(z,buf,4)==4);for(int i=0;i<4;i++)assert(buf[i]==i+5);unzClose(z);
 z=open_zip(argv[2],"stored");all(z);assert(unzCloseCurrentFile(z)==UNZ_CRCERROR);
 assert(unzOpenCurrentFile(z)==UNZ_OK);assert(unzSeekStoredFile64(z,40)==UNZ_OK);
 assert(unzSeekStoredFile64(z,0)==UNZ_OK);all(z);assert(unzCloseCurrentFile(z)==UNZ_CRCERROR);unzClose(z);
 z=open_zip(argv[3],"large.bin");read_bytes=0;
 const ZPOS64_T target=((ZPOS64_T)1<<32)+17;assert(unzSeekStoredFile64(z,target)==UNZ_OK);
 assert(unzReadCurrentFile(z,buf,12)==12);assert(!memcmp(buf,"ZIP64-MARKER",12));assert(unztell64(z)==target+12);
 assert(read_bytes<=65536);assert(unzSeekStoredFile64(z,0)==UNZ_OK);assert(unztell64(z)==0);
 unzClose(z);puts("stored seek: random reads, CRC reset, deflate fallback and >4GB offsets pass");
}
`;
    const c = join(dir, 'seek.c'), binary = join(dir, 'seek');writeFileSync(c, source);
    const mini = join(repo, 'lib/zlib/contrib/minizip');
    execFileSync('cc', ['-D_FILE_OFFSET_BITS=64', '-I', mini, c, join(mini, 'unzip.c'), join(mini, 'ioapi.c'), '-lz', '-o', binary], { stdio: 'pipe' });
    execFileSync(binary, [join(dir, 'small.zip'), join(dir, 'bad.zip'), join(dir, 'large.zip')], { stdio: 'pipe' });
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
