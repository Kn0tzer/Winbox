interface Window {
  showDirectoryPicker?: (options: {mode: 'read', id: string}) => Promise<any>;
  bwReadLocalArchive?: (url: string, start: number, end: number) => Promise<Uint8Array>;
  bwSetMouseCapture?: (enabled: boolean) => void;
  bwRuntime?: { fs(): any; ready(): boolean; call(name: string, types: string[], args: unknown[]): unknown };
  wineGpu?: any;
  wineLibrary?: any;
  wineGames?: any;
  // vk64 page tier (web/vkwebgpu.mjs, installed from web/runtime.html's
  // window.bwVkFrame hop or from web/app.mjs's 'vk' message branch).
  bwVkFrame?: (json: string) => void;
  bwVkTier?: any;
  bwVkTierReady?: Promise<any>;
  bwVkStats?: () => any;
  bwVkRenderer?: any;
  __bwLogConfig?: Record<string, string | number>;
  __bwStatsEvery?: number;
  __bwDbgRender?: boolean;
}
interface FileSystemFileEntry extends FileSystemEntry { file(success: (file: File) => void, error?: (error: DOMException) => void): void; }
interface FileSystemDirectoryEntry extends FileSystemEntry { createReader(): FileSystemDirectoryReader; }
