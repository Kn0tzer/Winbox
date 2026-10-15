import { execFile } from 'node:child_process';

export async function portalMenuText(imagePath, { timeoutMs = 3000, executable = 'tesseract', prefixArgs = [] } = {}) {
  return new Promise((resolve, reject) => {
    const child = execFile(executable, [...prefixArgs, imagePath, 'stdout', '--psm', '11'],
      { encoding: 'utf8', timeout: timeoutMs, killSignal: 'SIGKILL', maxBuffer: 1024 * 1024 }, (error, stdout) => {
        if (!error) resolve({ text: stdout, timedOut: false });
        else if (error.killed && error.signal === 'SIGKILL') resolve({ text: '', timedOut: true, childPid: child.pid });
        else reject(error);
      });
  });
}
