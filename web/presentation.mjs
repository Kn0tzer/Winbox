export function fitDisplay(width, height, sourceWidth, sourceHeight) {
  const scale = Math.min(width / Math.max(1, sourceWidth), height / Math.max(1, sourceHeight));
  const fittedWidth = sourceWidth * scale, fittedHeight = sourceHeight * scale;
  return { left: (width - fittedWidth) / 2, top: (height - fittedHeight) / 2, width: fittedWidth, height: fittedHeight };
}

export function installPresentation(canvas, report) {
  const layout = () => {
    if (!canvas.width || !canvas.height) return;
    const box = fitDisplay(innerWidth, innerHeight, canvas.width, canvas.height);
    for (const [name, value] of Object.entries(box)) canvas.style[name] = value + 'px';
    report({ width: canvas.width, height: canvas.height });
  };
  const observer = new MutationObserver(layout);
  observer.observe(canvas, { attributes: true, attributeFilter: ['width', 'height'] });
  addEventListener('resize', layout);
  layout();
  return () => { observer.disconnect(); removeEventListener('resize', layout); };
}
