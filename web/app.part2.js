
function roundedRect(ctx,x,y,w,h,r,fill,stroke=null){ctx.beginPath();ctx.roundRect(x,y,w,h,r);if(fill){ctx.fillStyle=fill;ctx.fill()}if(stroke){ctx.strokeStyle=stroke;ctx.lineWidth=2;ctx.stroke()}}
function exportRecordsImage(){const records=[1,2,3,4,5].map(slot=>state.slots[slot]);if(records.some(record=>!record))return;const total=records.reduce((sum,record)=>sum+Number(record.subtotal||0),0),canvas=document.createElement('canvas');canvas.width=2000;canvas.height=920;const ctx=canvas.getContext('2d');ctx.fillStyle='#202020';ctx.fillRect(0,0,canvas.width,canvas.height);ctx.fillStyle='#4cc2ff';ctx.fillRect(0,0,2000,10);ctx.fillStyle='#f3f3f3';ctx.font='600 40px "Microsoft YaHei UI", "Segoe UI", sans-serif';ctx.fillText('鸣潮声骸记录',50,70);ctx.fillStyle='#969696';ctx.font='22px "Microsoft YaHei UI", "Segoe UI", sans-serif';const now=new Date();ctx.fillText(`导出时间  ${now.toLocaleString('zh-CN',{hour12:false})}`,50,110);roundedRect(ctx,1440,35,510,92,14,'#292929','rgba(255,255,255,.12)');ctx.fillStyle='#969696';ctx.font='18px "Microsoft YaHei UI", sans-serif';ctx.fillText('原始总分',1480,72);ctx.fillText('最终平均分',1690,72);ctx.fillStyle='#f3f3f3';ctx.font='700 32px "Segoe UI", sans-serif';ctx.fillText(String(total),1480,108);ctx.fillStyle='#6ccb5f';ctx.fillText((total/5).toFixed(1),1690,108);const cardW=364,cardH=690,gap=20,startX=50,cardY=160;records.forEach((record,index)=>{const x=startX+index*(cardW+gap);roundedRect(ctx,x,cardY,cardW,cardH,16,'#292929','rgba(255,255,255,.13)');ctx.fillStyle='#333333';ctx.fillRect(x+2,cardY+72,cardW-4,2);ctx.fillStyle='#f3f3f3';ctx.font='600 26px "Microsoft YaHei UI", sans-serif';ctx.fillText(`声骸 ${index+1}`,x+24,cardY+47);const subColor=record.subtotal>0?'#6ccb5f':record.subtotal<0?'#ff99a4':'#c9c9c9';ctx.fillStyle=subColor;ctx.font='700 24px "Segoe UI", "Microsoft YaHei UI", sans-serif';ctx.textAlign='right';ctx.fillText(`小计 ${formatScore(record.subtotal)}`,x+cardW-24,cardY+47);ctx.textAlign='left';record.rows.forEach((row,rowIndex)=>{const y=cardY+118+rowIndex*88;if(rowIndex>0){ctx.strokeStyle='rgba(255,255,255,.065)';ctx.beginPath();ctx.moveTo(x+24,y-29);ctx.lineTo(x+cardW-24,y-29);ctx.stroke()}ctx.fillStyle='#c9c9c9';ctx.font='22px "Microsoft YaHei UI", sans-serif';ctx.fillText(shortAttribute(row.attribute),x+24,y);ctx.fillStyle='#f3f3f3';ctx.font='23px "Segoe UI", "Microsoft YaHei UI", sans-serif';ctx.textAlign='right';ctx.fillText(row.value,x+cardW-82,y);ctx.fillStyle=row.score>0?'#6ccb5f':row.score<0?'#ff99a4':'#969696';ctx.font='700 23px "Segoe UI", sans-serif';ctx.fillText(formatScore(row.score),x+cardW-24,y);ctx.textAlign='left'})});ctx.fillStyle='#777';ctx.font='18px "Microsoft YaHei UI", sans-serif';ctx.fillText('鸣潮声骸计算器 v1.2.1 · 本地记录导出',50,890);canvas.toBlob(blob=>{if(!blob)return;const url=URL.createObjectURL(blob),link=document.createElement('a'),stamp=now.toISOString().replace(/[:T]/g,'-').slice(0,19);link.download=`鸣潮声骸记录_${stamp}.png`;link.href=url;link.click();setTimeout(()=>URL.revokeObjectURL(url),1500)},'image/png')}
async function preprocessImage(file){const bitmap=await createImageBitmap(file),width=bitmap.width,height=bitmap.height,shortest=Math.min(width,height),longest=Math.max(width,height);let scale=1;if(shortest<720)scale=Math.min(3.2,720/Math.max(1,shortest));if(longest*scale>1900)scale=1900/longest;const padding=Math.max(20,Math.round(24*scale)),canvas=document.createElement('canvas');canvas.width=Math.round(width*scale)+padding*2;canvas.height=Math.round(height*scale)+padding*2;const ctx=canvas.getContext('2d',{alpha:false});ctx.fillStyle='#101a25';ctx.fillRect(0,0,canvas.width,canvas.height);ctx.imageSmoothingEnabled=true;ctx.imageSmoothingQuality='high';ctx.drawImage(bitmap,padding,padding,Math.round(width*scale),Math.round(height*scale));bitmap.close();return new Promise((resolve,reject)=>canvas.toBlob(blob=>blob?resolve(blob):reject(new Error('图片转换失败')),'image/png',1))}
function ocrConfig(){return{detModelUrl:new URL('./models/PP-OCRv5_mobile_det_onnx_infer.tar',location.href).href,recModelUrl:new URL('./models/PP-OCRv5_mobile_rec_onnx_infer.tar',location.href).href,wasmBaseUrl:new URL('./assets/',location.href).href}}
function postOcr(message,transfers=[]){dom.ocrFrame.contentWindow?.postMessage(message,'*',transfers)}
function updateRecognitionButtons(){const busy=state.initializingOcr||state.pendingRun||state.recognizing;dom.stopRecognition.disabled=!busy;dom.recognizeAgain.disabled=!state.imageFile||busy;dom.recognizeTop.disabled=!state.imageFile||busy}
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
  dom.previewImage.src = state.imageUrl;
  dom.previewWrap.classList.remove('empty');

  try {
    const bitmap = await createImageBitmap(file);
    state.imageWidth = bitmap.width;
    state.imageHeight = bitmap.height;
    bitmap.close();
    dom.imageMeta.textContent = `${name} · ${state.imageWidth} × ${state.imageHeight}`;
  } catch {
    dom.imageMeta.textContent = name;
  }

  updateRecognitionButtons();
  await runRecognition();
}
