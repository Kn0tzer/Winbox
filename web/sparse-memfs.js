// Wine creates large, mostly empty mapping files. Preserve file semantics while
// storing only pages that have been written, rather than allocating every hole.
globalThis.bwInstallSparseTmpMappings = function (FS, MEMFS) {
  if (!FS || !MEMFS || MEMFS.__bwSparseTmpMappings) return;
  MEMFS.__bwSparseTmpMappings = true;
  const pageSize = 4096;
  const stateKey = Symbol('Wine sparse mapping');
  const stateFor = (node, size = node.usedBytes) => {
    if (node[stateKey]) return node[stateKey];
    // Keep small Wine synchronization files on MEMFS's ordinary path.
    if (size < 1024 * 1024) return null;
    if (!/\/\.wine-\d+\/server[^/]+\/tmpmap-[^/]+$/.test(FS.getPath(node))) return null;
    const state = { pages: new Map() };
    if (node.contents) {
      for (let at = 0; at < node.usedBytes; at += pageSize) {
        const data = node.contents.subarray(at, Math.min(at + pageSize, node.usedBytes));
        if (data.some(v => v !== 0)) {
          const page = new Uint8Array(pageSize);
          page.set(data); state.pages.set(at / pageSize, page);
        }
      }
    }
    node.contents = null;
    node[stateKey] = state;
    return state;
  };
  const resize = MEMFS.resizeFileStorage;
  MEMFS.resizeFileStorage = (node, size) => {
    const state = stateFor(node, size);
    if (!state) return resize(node, size);
    for (const index of state.pages.keys()) {
      if (index * pageSize >= size) state.pages.delete(index);
    }
    if (size % pageSize) state.pages.get(Math.floor(size / pageSize))?.fill(0, size % pageSize);
    node.usedBytes = size;
  };
  const read = MEMFS.stream_ops.read;
  const sparseRead = (stream, buffer, offset, length, position) => {
    const state = stateFor(stream.node);
    if (!state) return read(stream, buffer, offset, length, position);
    const count = Math.max(0, Math.min(length, stream.node.usedBytes - position));
    buffer.fill(0, offset, offset + count);
    for (let done = 0; done < count;) {
      const at = position + done, inPage = at % pageSize;
      const take = Math.min(count - done, pageSize - inPage);
      const page = state.pages.get(Math.floor(at / pageSize));
      if (page) buffer.set(page.subarray(inPage, inPage + take), offset + done);
      done += take;
    }
    return count;
  };
  const write = MEMFS.stream_ops.write;
  const sparseWrite = (stream, buffer, offset, length, position, canOwn) => {
    const node = stream.node, state = stateFor(node, position + length);
    if (!state) return write(stream, buffer, offset, length, position, canOwn);
    if (!length) return 0;
    for (let done = 0; done < length;) {
      const at = position + done, inPage = at % pageSize;
      const take = Math.min(length - done, pageSize - inPage);
      const index = Math.floor(at / pageSize);
      const data = buffer.subarray(offset + done, offset + done + take);
      let page = state.pages.get(index);
      if (!page && data.some(v => v !== 0)) {
        page = new Uint8Array(pageSize); state.pages.set(index, page);
      }
      if (page) page.set(data, inPage);
      done += take;
    }
    node.usedBytes = Math.max(node.usedBytes, position + length);
    node.mtime = node.ctime = Date.now();
    return length;
  };
  const mmap = MEMFS.stream_ops.mmap;
  const sparseMmap = (stream, length, position, prot, flags) => {
    if (!stateFor(stream.node)) return mmap(stream, length, position, prot, flags);
    // Native Emscripten mmap is uncommon here; Boxedwine's guest MMU reads
    // individual pages. Materialize only the requested prefix for native mmap.
    const contents = new Uint8Array(position + length);
    sparseRead(stream, contents, position, length, position);
    stream.node.contents = contents;
    try { return mmap(stream, length, position, prot, flags); }
    finally { stream.node.contents = null; }
  };
  for (const ops of [MEMFS.stream_ops, MEMFS.ops_table?.file?.stream].filter(Boolean)) {
    ops.read = sparseRead; ops.write = sparseWrite; ops.mmap = sparseMmap;
  }
};
