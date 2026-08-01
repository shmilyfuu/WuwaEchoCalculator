
function roundedRect(ctx,x,y,w,h,r,fill,stroke=null){ctx.beginPath();ctx.roundRect(x,y,w,h,r);if(fill){ctx.fillStyle=fill;ctx.fill()}if(stroke){ctx.strokeStyle=stroke;ctx.lineWidth=2;ctx.stroke()}}

function loadCanvasImage(url) {
  return new Promise((resolve, reject) => {
    const image = new Image();
    image.onload = () => resolve(image);
    image.onerror = () => reject(new Error(`无法读取导出资源：${url}`));
    image.src = url;
  });
}

function exportScoreColor(score) {
  if (score > 0) return '#6CCB5F';
  if (score < 0) return '#FF99A4';
  return 'rgba(255, 255, 255, .60)';
}

function defaultExportTitle() {
  const version = document.querySelector('.brand-title span')?.textContent.trim() || '';
  if (!version) return '鸣潮声骸计算器';
  return `鸣潮声骸计算器${version.startsWith('v') ? version : `v${version}`}`;
}

function drawExportCard(ctx, record, index, x, y, starIcon) {
  roundedRect(ctx, x, y, 244, 195, 3, 'rgba(255, 255, 255, .03)');

  ctx.textAlign = 'left';
  ctx.fillStyle = '#fff';
  ctx.font = '600 16px "Microsoft YaHei UI", "Segoe UI", sans-serif';
  ctx.fillText('声骸', x + 12, y + 12);
  ctx.fillText(String(index + 1), x + 49, y + 12);

  const subtotal = formatScore(record.subtotal);
  ctx.textAlign = 'left';
  ctx.fillStyle = '#fff';
  ctx.fillText('小计', x + 169, y + 12);
  ctx.textAlign = 'right';
  ctx.fillStyle = exportScoreColor(record.subtotal);
  ctx.fillText(subtotal, x + 232, y + 12);

  record.rows.forEach((row, rowIndex) => {
    const rowY = y + 42 + rowIndex * 29;
    roundedRect(ctx, x + 12, rowY, 220, 25, 1, 'rgba(255, 255, 255, .06)');
    ctx.drawImage(starIcon, x + 18, rowY + (rowIndex === 0 ? 7 : 8), 11, 11);

    ctx.textAlign = 'left';
    ctx.fillStyle = '#fff';
    ctx.font = '400 14px "Microsoft YaHei UI", "Segoe UI", sans-serif';
    ctx.fillText(shortAttribute(row.attribute), x + 33, rowY + 3);

    ctx.textAlign = 'right';
    ctx.fillText(row.value, x + 196, rowY + 3);
    ctx.fillStyle = exportScoreColor(row.score);
    ctx.fillText(formatScore(row.score), x + 226, rowY + 3);
  });
}

function drawExportMetric(ctx, label, value, caption, x, y) {
  ctx.textAlign = 'center';
  ctx.fillStyle = '#fff';
  ctx.font = '400 16px "Microsoft YaHei UI", "Segoe UI", sans-serif';
  ctx.fillText(label, x + 40, y);
  ctx.fillStyle = '#6CCB5F';
  ctx.font = '600 24px "Microsoft YaHei UI", "Segoe UI", sans-serif';
  ctx.fillText(value, x + 40, y + 30);
  ctx.fillStyle = 'rgba(255, 255, 255, .60)';
  ctx.font = '400 12px "Microsoft YaHei UI", "Segoe UI", sans-serif';
  ctx.fillText(caption, x + 40, y + 68);
}

async function exportRecordsImage(customTitle = '') {
  const records = [1, 2, 3, 4, 5].map(slot => state.slots[slot]);
  if (records.some(record => !record)) return;

  const now = new Date();
  const total = records.reduce((sum, record) => sum + Number(record.subtotal || 0), 0);
  const canvas = document.createElement('canvas');
  canvas.width = 576;
  canvas.height = 749;
  const ctx = canvas.getContext('2d');
  if (!ctx) throw new Error('无法创建导出画布');

  const starIcon = await loadCanvasImage(new URL('./assets/icon_Star@2x.png', location.href).href);
  ctx.textBaseline = 'top';
  ctx.fillStyle = '#202020';
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  ctx.textAlign = 'left';
  ctx.fillStyle = '#fff';
  ctx.font = '600 20px "Microsoft YaHei UI", "Segoe UI", sans-serif';
  ctx.fillText('声骸记录', 40, 40);
  ctx.fillStyle = 'rgba(255, 255, 255, .60)';
  ctx.font = '400 14px "Microsoft YaHei UI", "Segoe UI", sans-serif';
  ctx.fillText('导出时间', 41, 68);
  ctx.fillText(now.toLocaleString('zh-CN', { hour12: false }), 101, 68);

  const cardPositions = [[40, 100], [292, 100], [40, 307], [292, 307], [40, 514]];
  records.forEach((record, index) => {
    const [x, y] = cardPositions[index];
    drawExportCard(ctx, record, index, x, y, starIcon);
  });

  roundedRect(ctx, 292, 514, 244, 155, 3, 'rgba(255, 255, 255, .03)');
  drawExportMetric(ctx, '总分', String(total), '5件声骸总分', 320, 549);
  drawExportMetric(ctx, '平均分', (total / 5).toFixed(1), '5件声骸平均分', 428, 549);

  ctx.textAlign = 'right';
  ctx.fillStyle = '#fff';
  ctx.font = '400 16px "Microsoft YaHei UI", "Segoe UI", sans-serif';
  ctx.fillText(customTitle || defaultExportTitle(), 536, 687);

  const blob = await new Promise(resolve => canvas.toBlob(resolve, 'image/png'));
  if (!blob) throw new Error('导出图片生成失败');
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  const stamp = now.toISOString().replace(/[:T]/g, '-').slice(0, 19);
  link.download = `鸣潮声骸记录_${stamp}.png`;
  link.href = url;
  link.click();
  window.setTimeout(() => URL.revokeObjectURL(url), 1500);
}
async function preprocessImage(file){const bitmap=await createImageBitmap(file),width=bitmap.width,height=bitmap.height,shortest=Math.min(width,height),longest=Math.max(width,height);let scale=1;if(shortest<720)scale=Math.min(3.2,720/Math.max(1,shortest));if(longest*scale>1900)scale=1900/longest;const padding=Math.max(20,Math.round(24*scale)),canvas=document.createElement('canvas');canvas.width=Math.round(width*scale)+padding*2;canvas.height=Math.round(height*scale)+padding*2;const ctx=canvas.getContext('2d',{alpha:false});ctx.fillStyle='#101a25';ctx.fillRect(0,0,canvas.width,canvas.height);ctx.imageSmoothingEnabled=true;ctx.imageSmoothingQuality='high';ctx.drawImage(bitmap,padding,padding,Math.round(width*scale),Math.round(height*scale));bitmap.close();return new Promise((resolve,reject)=>canvas.toBlob(blob=>blob?resolve(blob):reject(new Error('图片转换失败')),'image/png',1))}
function ocrConfig(){return{detModelUrl:new URL('./models/PP-OCRv5_mobile_det_onnx_infer.tar',location.href).href,recModelUrl:new URL('./models/PP-OCRv5_mobile_rec_onnx_infer.tar',location.href).href,wasmBaseUrl:new URL('./assets/',location.href).href}}
function postOcr(message,transfers=[]){dom.ocrFrame.contentWindow?.postMessage(message,'*',transfers)}
function updateRecognitionButtons(){const busy=state.initializingOcr||state.pendingRun||state.recognizing;dom.stopRecognition.disabled=!busy;dom.recognizeAgain.disabled=!state.imageFile||busy;dom.recognizeTop.disabled=!state.imageFile||busy}

function fitPreviewImage(width, height) {
  if (!(width > 0) || !(height > 0)) return;
  const scale = Math.min(1, 320 / width, 220 / height);
  dom.previewImage.style.width = `${width * scale}px`;
  dom.previewImage.style.height = `${height * scale}px`;
}

async function acceptImage(file, name = file?.name || '剪贴板图片') {
  if (!file || !file.type.startsWith('image/')) {
    setRecognitionState('请选择图片', 'error');
    return;
  }

  if (state.recognizing) stopRecognition();
  if (state.imageUrl) URL.revokeObjectURL(state.imageUrl);
  state.imageFile = file;
  state.imageUrl = URL.createObjectURL(file);
  state.imageName = name;
  dom.previewImage.style.visibility = 'hidden';
  dom.previewImage.src = state.imageUrl;
  dom.previewWrap.classList.remove('empty');

  try {
    const bitmap = await createImageBitmap(file);
    state.imageWidth = bitmap.width;
    state.imageHeight = bitmap.height;
    bitmap.close();
    fitPreviewImage(state.imageWidth, state.imageHeight);
    dom.imageMeta.textContent = `${name} · ${state.imageWidth} × ${state.imageHeight}`;
  } catch {
    try {
      await dom.previewImage.decode();
      state.imageWidth = dom.previewImage.naturalWidth;
      state.imageHeight = dom.previewImage.naturalHeight;
      fitPreviewImage(state.imageWidth, state.imageHeight);
      dom.imageMeta.textContent = `${name} · ${state.imageWidth} × ${state.imageHeight}`;
    } catch {
      dom.imageMeta.textContent = name;
    }
  }
  dom.previewImage.style.visibility = 'visible';

  updateRecognitionButtons();
  await runRecognition();
}
