import { replacePackage } from './storage.mjs';
import { writeStored, deleteStored } from './session-storage.mjs';

// A fresh save database is private until the library's atomic pointer switch.
// Failed staging/publication leaves both the original package and saves intact.
export async function replaceInstallation(previous, changes, saves) {
  const next = { ...previous, ...changes, id: crypto.randomUUID(), patched: new Date().toISOString() };
  try {
    await writeStored(next.id, saves, true);
    await replacePackage(previous, next);
  } catch (error) {
    await deleteStored(next.id).catch(() => {});
    throw error;
  }
  await deleteStored(previous.id).catch(() => {});
  return next;
}
