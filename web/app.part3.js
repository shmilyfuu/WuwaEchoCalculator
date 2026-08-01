async function runRecognition() {
  if (!state.imageFile || state.recognizing) return;
  state.stoppedByUser = false;

  if (state.initializingOcr || !state.sandboxReady || !state.modelReady) {
    state.pendingRun = true;
    state.initializingOcr = true;
    setRecognitionState('等待识别模块就绪', 'running');
    updateRecognitionButtons();
    return;
  }

  state.pendingRun = false;
  state.recognizing = true;
  const jobId = ++state.jobSequence;
  state.currentJobId = jobId;
  updateRecognitionButtons();
  setRecognitionState('正在识别', 'running');
  setModelStatus('正在处理截图', 'loading');

  try {
    const blob = await preprocessImage(state.imageFile);
    if (!state.recognizing || state.currentJobId !== jobId) return;
    const buffer = await blob.arrayBuffer();
    if (!state.recognizing || state.currentJobId !== jobId) return;
    postOcr({
      type: 'ocr-run',
      jobId,
      buffer,
      mimeType: 'image/png',
      config: ocrConfig(),
      params: { textDetLimitSideLen: 1600 }
    }, [buffer]);
  } catch (error) {
    if (state.currentJobId !== jobId) return;
    state.recognizing = false;
    updateRecognitionButtons();
    setModelStatus(error instanceof Error ? error.message : String(error), 'error');
  }
}

function resetOcrFrame() {
  state.sandboxReady = false;
  state.modelReady = false;
  state.initializingOcr = true;
  dom.ocrFrame.src = 'about:blank';
  window.setTimeout(() => {
    dom.ocrFrame.src = `./ocr-sandbox.html?reset=${Date.now()}`;
  }, 0);
}

function stopRecognition() {
  const busy = state.initializingOcr || state.pendingRun || state.recognizing;
  if (!busy) return;

  state.currentJobId = ++state.jobSequence;
  state.recognizing = false;
  state.pendingRun = false;
  state.stoppedByUser = true;
  resetOcrFrame();
  updateRecognitionButtons();
  setModelStatus('识别已停止，正在重启模块', 'loading');
  dom.runtimeInfo.textContent = '当前识别已终止';
}

function applyOcrResult(items, metrics) {
  const parsed = selectOcrRows(items);
  state.rows = Array.from({ length: 5 }, (_, index) => {
    const row = parsed.selected[index];
    return row ? {
      attribute: row.attribute,
      value: row.value,
      confidence: row.confidence,
      confidenceScore: row.confidenceScore,
      sourceText: row.sourceText
    } : {
      attribute: '',
      value: '',
      confidence: 'manual',
      confidenceScore: 0,
      sourceText: ''
    };
  });
  dom.rawText.textContent = ocrDetails(items, parsed, metrics);
  renderRows();
  const medium = state.rows.filter(row => row.confidence === 'medium').length;
  if (parsed.selected.length === 5 && medium === 0) setRecordHint('已识别五条，请核对后写入。', 'success');
  else if (parsed.selected.length === 5) setRecordHint(`已识别五条，其中 ${medium} 条建议重点核对。`);
  else setRecordHint(`当前识别到 ${parsed.selected.length} 条，请通过下拉框补齐。`);
}

function setupImageInput() {
  const appShell = document.querySelector('.app-shell');
  let dragDepth = 0;
  const isFileDrag = event => [...(event.dataTransfer?.types || [])].includes('Files');
  const setDragging = active => appShell?.classList.toggle('dragging-file', active);

  dom.dropZone.addEventListener('click', () => dom.fileInput.click());
  dom.fileInput.addEventListener('change', () => {
    const file = dom.fileInput.files?.[0];
    if (file) acceptImage(file);
    dom.fileInput.value = '';
  });

  document.addEventListener('dragenter', event => {
    if (!isFileDrag(event)) return;
    event.preventDefault();
    dragDepth += 1;
    setDragging(true);
  });

  document.addEventListener('dragover', event => {
    if (!isFileDrag(event)) return;
    event.preventDefault();
    if (event.dataTransfer) event.dataTransfer.dropEffect = 'copy';
    setDragging(true);
  });

  document.addEventListener('dragleave', event => {
    if (!isFileDrag(event)) return;
    event.preventDefault();
    dragDepth = Math.max(0, dragDepth - 1);
    if (dragDepth === 0) setDragging(false);
  });

  document.addEventListener('drop', event => {
    event.preventDefault();
    dragDepth = 0;
    setDragging(false);
    const file = [...(event.dataTransfer?.files || [])].find(item => item.type.startsWith('image/'));
    if (file) acceptImage(file);
    else setModelStatus('请拖入图片文件', 'error');
  });

  window.addEventListener('blur', () => {
    dragDepth = 0;
    setDragging(false);
  });

  document.addEventListener('paste', event => {
    if (event.target instanceof HTMLInputElement ||
        event.target instanceof HTMLTextAreaElement ||
        event.target?.isContentEditable) return;
    const file = [...(event.clipboardData?.items || [])]
      .find(item => item.type.startsWith('image/'))
      ?.getAsFile();
    if (file) {
      event.preventDefault();
      acceptImage(file, '剪贴板图片');
    }
  });
}

function setupOcrMessages() {
  window.addEventListener('message', event => {
    const data = event.data;
    if (!data || typeof data !== 'object') return;

    if (data.type === 'ocr-sandbox-ready') {
      state.sandboxReady = true;
      state.initializingOcr = true;
      state.stoppedByUser = false;
      setModelStatus('正在读取本地 PaddleOCR 模型', 'loading');
      updateRecognitionButtons();
      postOcr({ type: 'ocr-init', config: ocrConfig() });
      return;
    }

    if (data.type === 'ocr-progress') {
      if (!data.jobId || data.jobId === state.currentJobId) {
        setModelStatus(data.text || '正在处理', 'loading');
        updateRecognitionButtons();
      }
      return;
    }

    if (data.type === 'ocr-initialized') {
      state.modelReady = true;
      state.initializingOcr = false;
      setModelStatus('PaddleOCR 已就绪 · 本地模型', 'ready');
      updateRecognitionButtons();
      if (state.pendingRun) {
        state.pendingRun = false;
        runRecognition();
      }
      return;
    }

    if (data.type === 'ocr-result' && data.jobId === state.currentJobId) {
      state.recognizing = false;
      state.modelReady = true;
      state.initializingOcr = false;
      updateRecognitionButtons();
      applyOcrResult(Array.isArray(data.items) ? data.items : [], data.metrics || null);
      setModelStatus('PaddleOCR 已就绪 · 本地模型', 'ready');
      if (data.metrics) dom.runtimeInfo.textContent = `本次 ${Math.round(data.metrics.totalMs || 0)} ms · 本地识别`;
      return;
    }

    if (data.type === 'ocr-error' && (!data.jobId || data.jobId === state.currentJobId)) {
      if (state.stoppedByUser) return;
      state.recognizing = false;
      state.pendingRun = false;
      state.initializingOcr = false;
      updateRecognitionButtons();
      dom.rawText.textContent = `${data.message || 'OCR 处理失败'}${data.stack ? `\n\n${data.stack}` : ''}`;
      setModelStatus(data.message || 'OCR 处理失败', 'error');
    }
  });
}

function setupHostActions() {
  if (window.chrome?.webview) {
    window.chrome.webview.addEventListener('message', event => {
      if (event.data === 'export:completed') {
        setStatus('记录图片已导出', 'success');
      } else if (event.data === 'export:canceled') {
        setStatus('已取消导出', 'info');
      } else if (event.data === 'export:interrupted') {
        setStatus('记录图片保存中断', 'error');
      } else if (event.data === 'export:error') {
        setStatus('无法打开保存窗口或写入所选位置', 'error');
      }
    });
  }

  dom.topmostToggle.addEventListener('change', () => {
    if (window.chrome?.webview) {
      window.chrome.webview.postMessage(`topmost:${dom.topmostToggle.checked ? '1' : '0'}`);
    }
  });
}

function initialize() {
  loadState();
  renderRows();
  renderSlotSelect();
  renderSlots();
  setupImageInput();
  setupOcrMessages();
  setupHostActions();
  setupConfirmDialog();
  setupExportDialog();
  dom.slotSelect.addEventListener('change', () => selectSlot(Number(dom.slotSelect.value)));
  dom.recognizeAgain.addEventListener('click', runRecognition);
  dom.recognizeTop.addEventListener('click', runRecognition);
  dom.stopRecognition.addEventListener('click', stopRecognition);
  dom.confirmRecord.addEventListener('click', confirmRecord);
  dom.clearAll.addEventListener('click', clearAll);
  dom.exportRecords.addEventListener('click', requestExportRecords);
  setModelStatus('正在准备本地 PaddleOCR 模型', 'loading');
  updateRecognitionButtons();
}

initialize();
