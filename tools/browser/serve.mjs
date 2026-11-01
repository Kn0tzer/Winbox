import { createServer } from 'node:http';
import { createReadStream } from 'node:fs';
import { stat } from 'node:fs/promises';
import { resolve,extname,sep } from 'node:path';
export async function serve(root,port=0){
  root=resolve(root);
  // Fail loudly and immediately when the dist root is missing: without this
  // check the server starts happily and 404s every asset, so a probe against
  // an unbuilt or stripped dist burns its whole boot timeout and reports a
  // misleading guest failure instead of naming the real problem.
  const rootInfo=await stat(root).catch(()=>null);
  if(!rootInfo||!rootInfo.isDirectory())throw new Error(`BW64-FAIL serve: dist root does not exist or is not a directory: ${root} (build it first with tools/bw64-build.sh, or point WINE_DIST_DIR at a built dist)`);
  const types={'.svg':'image/svg+xml','.mjs':'text/javascript','.js':'text/javascript','.html':'text/html','.css':'text/css','.wasm':'application/wasm','.zip':'application/zip','.json':'application/json'};
  const server=createServer(async(req,res)=>{
    try{
      const url=new URL(req.url,'http://localhost');const path=resolve(root,'.'+decodeURIComponent(url.pathname));
      if(!path.startsWith(root+sep)&&path!==root)throw new Error('Invalid path');
      let file=path,info=await stat(file);if(info.isDirectory()){file=resolve(file,'index.html');info=await stat(file);}
      const headers={'Content-Type':types[extname(file)]||'application/octet-stream','Cross-Origin-Opener-Policy':'same-origin','Cross-Origin-Embedder-Policy':'require-corp','Cross-Origin-Resource-Policy':'same-origin','Cache-Control':'no-cache','Accept-Ranges':'bytes'};
      let start=0,end=info.size-1;const range=/^bytes=(\d+)-(\d*)$/.exec(req.headers.range||'');if(range){start=Number(range[1]);end=range[2]?Math.min(Number(range[2]),end):end;if(start>end){res.writeHead(416);res.end();return;}headers['Content-Range']=`bytes ${start}-${end}/${info.size}`;}
      headers['Content-Length']=end-start+1;res.writeHead(range?206:200,headers);if(req.method==='HEAD')res.end();else createReadStream(file,{start,end}).pipe(res);
    }catch{res.writeHead(404);res.end('Not found');}
  });
  await new Promise(ok=>server.listen(port,'127.0.0.1',ok));return server;
}
