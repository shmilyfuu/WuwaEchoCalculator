import * as ort from 'onnxruntime-web';
import { PaddleOCR } from '@paddleocr/paddleocr-js';

ort.env.logLevel = 'error';
const originalWarn = console.warn.bind(console);
console.warn = (...args) => {
  const message = args.map(String).join(' ');
  if (message.includes('CleanUnusedInitializersAndNodeArgs') || message.includes('Removing initializer') || message.includes('not used by any node')) return;
  originalWarn(...args);
};

let ocr = null;
let lastConfig = null;
function send(message) { window.parent.postMessage(message, '*'); }

async function ensureOcr(config) {
  if (ocr) return ocr;
  lastConfig = config;
  send({ type: 'ocr-progress', stage: 'model', progress: .08, text: '正在读取内置检测模型' });
  ocr = await PaddleOCR.create({
    textDetectionModelName: 'PP-OCRv5_mobile_det',
    textDetectionModelAsset: { url: config.detModelUrl },
    textRecognitionModelName: 'PP-OCRv5_mobile_rec',
    textRecognitionModelAsset: { url: config.recModelUrl },
    textDetectionBatchSize: 1,
    textRecognitionBatchSize: 8,
    worker: true,
    ortOptions: { backend: 'wasm', wasmPaths: config.wasmBaseUrl, numThreads: 1, simd: true }
  });
  send({ type: 'ocr-progress', stage: 'model', progress: 1, text: '本地模型已就绪' });
  send({ type: 'ocr-initialized', summary: ocr.getInitializationSummary?.() || null });
  return ocr;
}

async function runOcr(message) {
  const { jobId, buffer, mimeType, params, config } = message;
  try {
    const engine = await ensureOcr(config || lastConfig);
    send({ type: 'ocr-progress', jobId, stage: 'detect', progress: .2, text: '正在定位图片文字' });
    const blob = new Blob([buffer], { type: mimeType || 'image/png' });
    const [result] = await engine.predict(blob, {
      textDetLimitSideLen: params?.textDetLimitSideLen || 1280,
      textDetLimitType: 'max',
      textDetMaxSideLimit: 2400,
      textDetThresh: .25,
      textDetBoxThresh: .32,
      textDetUnclipRatio: 1.7,
      textRecScoreThresh: .20
    });
    send({ type: 'ocr-progress', jobId, stage: 'recognize', progress: .92, text: '正在整理识别结果' });
    send({ type: 'ocr-result', jobId, items: result?.items || [], image: result?.image || null, metrics: result?.metrics || null, runtime: result?.runtime || null });
  } catch (error) {
    ocr = null;
    send({ type: 'ocr-error', jobId, message: error instanceof Error ? error.message : String(error), stack: error instanceof Error ? error.stack : '' });
  }
}

window.addEventListener('message', event => {
  const data = event.data;
  if (!data || typeof data !== 'object') return;
  if (data.type === 'ocr-init') ensureOcr(data.config).catch(error => {
    ocr = null;
    send({ type: 'ocr-error', jobId: 0, message: error instanceof Error ? error.message : String(error) });
  });
  if (data.type === 'ocr-run') runOcr(data);
});

send({ type: 'ocr-sandbox-ready' });
