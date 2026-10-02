interface Window {
  bwRuntime?: { fs(): any; ready(): boolean; call(name: string, types: string[], args: unknown[]): unknown };
  wineGpu?: any;
  wineLibrary?: any;
}
interface FileSystemFileEntry extends FileSystemEntry { file(success: (file: File) => void, error?: (error: DOMException) => void): void; }
interface FileSystemDirectoryEntry extends FileSystemEntry { createReader(): FileSystemDirectoryReader; }
