const $ = id => document.getElementById(id);
const source = {
  recognition: $('recognitionState'),
  image: $('imageMeta'),
  record: $('recordHint'),
  modelDot: $('modelDot'),
  model: $('modelStatus'),
  runtime: $('runtimeInfo')
};
const target = {
  dot: $('statusDot'),
  message: $('appStatus'),
  detail: $('statusDetail'),
  modelDot: $('topModelDot')
};

const initialRecordHint = '识别后请核对五条属性，再写入对应位置。';
const initialImageHint = '尚未导入图片';

function classHas(element, name) {
  return element?.classList.contains(name) || false;
}

function compactError(text) {
  const value = String(text || '');
  if (value.includes('no available backend') || value.includes('Failed to fetch dynamically imported module')) {
    return '本地识别运行文件加载失败，请查看 OCR 明细。';
  }
  if (value.includes('模型') || value.includes('model')) return '本地识别模型加载失败，请查看 OCR 明细。';
  return '识别模块运行失败，请查看 OCR 明细。';
}

function renderStatus() {
  const recognitionText = source.recognition?.textContent?.trim() || '';
  const modelText = source.model?.textContent?.trim() || '';
  const recordText = source.record?.textContent?.trim() || '';
  const imageText = source.image?.textContent?.trim() || initialImageHint;
  const runtimeText = source.runtime?.textContent?.trim() || '本地识别 · 图片不会上传';

  let kind = 'idle';
  let message = '尚未导入图片。';
  let detail = runtimeText;

  if (classHas(source.modelDot, 'error') || classHas(source.recognition, 'error')) {
    kind = 'error';
    message = compactError(`${modelText} ${recognitionText}`);
    detail = '技术信息已保存在 OCR 明细中';
  } else if (classHas(source.recognition, 'running')) {
    kind = 'loading';
    message = recognitionText || modelText || '正在识别声骸截图。';
    detail = imageText !== initialImageHint ? imageText : runtimeText;
  } else if (source.record?.classList.contains('error')) {
    kind = 'error';
    message = recordText;
  } else if (source.record?.classList.contains('success')) {
    kind = 'ready';
    message = recordText;
  } else if (classHas(source.recognition, 'success')) {
    kind = 'ready';
    message = recordText && recordText !== initialRecordHint ? recordText : (recognitionText || '识别完成。');
  } else if (recordText && recordText !== initialRecordHint) {
    kind = 'info';
    message = recordText;
  } else if (classHas(source.modelDot, 'ready')) {
    kind = 'ready';
    message = imageText === initialImageHint ? '本地识别模型已就绪，尚未导入图片。' : '本地识别模型已就绪。';
    detail = imageText === initialImageHint ? runtimeText : imageText;
  } else if (modelText) {
    kind = 'loading';
    message = modelText;
  } else if (imageText !== initialImageHint) {
    kind = 'info';
    message = '图片已导入。';
    detail = imageText;
  }

  target.dot.className = `status-dot ${kind}`;
  target.message.textContent = message;
  target.detail.textContent = detail;
  const modelKind = classHas(source.modelDot, 'error') ? 'error' : classHas(source.modelDot, 'ready') ? 'ready' : 'loading';
  target.modelDot.className = `model-dot ${modelKind}`;
}

const observer = new MutationObserver(renderStatus);
Object.values(source).forEach(element => {
  if (element) observer.observe(element, { childList: true, characterData: true, subtree: true, attributes: true, attributeFilter: ['class'] });
});

$('exportRecords')?.addEventListener('click', event => {
  if (event.currentTarget.disabled) return;
  window.setTimeout(() => {
    target.dot.className = 'status-dot ready';
    target.message.textContent = '五件声骸记录图已导出。';
    target.detail.textContent = 'PNG 图片已保存到浏览器默认下载位置';
  }, 0);
});

renderStatus();
