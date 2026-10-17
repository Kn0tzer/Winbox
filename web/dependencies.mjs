/** Validate build-owned metadata before passing a lazy mount to the guest. */
export function dependencySpec(manifest, base) {
  const { version, archive, bytes, sha256 } = manifest || {};
  if (version !== 1 || !/^[a-f0-9]{64}$/.test(sha256) || archive !== `support-${sha256}.zip`
    || !Number.isSafeInteger(bytes) || bytes < 22) throw Error('Runtime dependencies are unavailable. Reload the page to try again.');
  const url = new URL(archive, base).href;
  return `bw64url:${bytes};${url}|${bytes}`;
}

/** Fail before starting Wine if the shared library archive cannot be mounted. */
export async function loadDependencies(manifestUrl, fetcher = fetch) {
  const failure = 'Runtime dependencies could not be loaded. Reload the page to try again.';
  const metadata = await fetcher(manifestUrl, { signal: AbortSignal.timeout(15000) });
  if (!metadata.ok) throw Error(failure);
  const manifest = await metadata.json();
  const spec = dependencySpec(manifest, manifestUrl);
  const archive = await fetcher(new URL(manifest.archive, manifestUrl), { method: 'HEAD', signal: AbortSignal.timeout(15000) });
  if (!archive.ok || Number(archive.headers.get('content-length')) !== manifest.bytes
    || archive.headers.get('accept-ranges') !== 'bytes') throw Error(failure);
  return spec;
}
