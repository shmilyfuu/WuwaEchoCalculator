import { RULES } from './rules.js';
import { selectOcrRows, ocrDetails } from './ocr-parse.js';

const STORAGE_KEY = 'wuwaEchoWebView2StateV3';
const LEGACY_STORAGE_KEYS = [
  'wuwaEchoWebView2StateV2',
  'wuwaEchoWebView2StateV1',
  'wuwa:wuwaEchoSlots'
];

const dom = Object.fromEntries([
  'topmostToggle', 'recognitionState', 'dropZone', 'fileInput', 'previewWrap', 'previewImage',
  'stopRecognition', 'recognizeAgain', 'recognizeTop', 'imageMeta', 'currentSubtotal',
  'rowsContainer', 'rawText', 'slotSelect', 'confirmRecord', 'recordHint', 'recordCount',
  'slotsList', 'rawTotal', 'finalAverage', 'recordedSummary', 'clearAll', 'exportRecords',
  'exportHint', 'statusDot', 'appStatus', 'runtimeInfo', 'confirmOverlay', 'confirmTitle',
  'confirmMessage', 'confirmAccept', 'confirmCancel', 'exportOverlay', 'exportTitleInput',
  'exportTitleHint', 'exportAccept', 'exportCancel', 'ocrFrame'
].map(id => [id, document.getElementById(id)]));

const state = {
  rows: blankRows(),
  slots: {},
  selectedSlot: 1,
  imageFile: null,
  imageUrl: '',
  imageName: '',
  imageWidth: 0,
  imageHeight: 0,
  jobSequence: 0,
  currentJobId: 0,
  sandboxReady: false,
  modelReady: false,
  recognizing: false,
  pendingRun: false,
  stoppedByUser: false,
  initializingOcr: true
};

let activeConfirmation = null;
let confirmationFocus = null;
let activeExportDialog = null;
let exportDialogFocus = null;

function blankRows() {
  return Array.from({ length: 5 }, () => ({
    attribute: '',
    value: '',
    confidence: 'manual',
    confidenceScore: 0,
    sourceText: ''
  }));
}

function option(value, label = value) {
  const el = document.createElement('option');
  el.value = value;
  el.textContent = label;
  return el;
}

function scoreOf(row) {
  const rule = RULES[row.attribute];
  if (!rule) return 0;
  const index = rule.values.indexOf(row.value);
  return index >= 0 ? rule.scores[index] : 0;
}

function subtotal() {
  return state.rows.reduce((sum, row) => sum + scoreOf(row), 0);
}

function formatScore(value) {
  return `${value > 0 ? '+' : ''}${value}`;
}

function shortAttribute(name) {
  return ({
    生命百分比: '生命',
    攻击百分比: '攻击',
    防御百分比: '防御'
  })[name] || name;
}

function setStatus(message, kind = 'info') {
  dom.appStatus.textContent = message;
  dom.statusDot.className = `status-dot ${kind || 'info'}`;
}

function setRecordHint(message, kind = '') {
  dom.recordHint.textContent = message;
  setStatus(message, kind === 'error' ? 'error' : kind === 'success' ? 'success' : 'info');
}

function setRecognitionState(text, kind = 'idle') {
  dom.recognitionState.textContent = text;
  setStatus(text, kind);
}

function setModelStatus(text, kind = 'loading') {
  setStatus(text, kind);
}

function confidenceKind(row) {
  if (!row.attribute && !row.value) return 'idle';
  if (row.confidence === 'high') return 'high';
  if (row.confidence === 'medium') return 'medium';
  return 'manual';
}

function fillValueSelect(select, row) {
  select.replaceChildren();
  if (!row.attribute || !RULES[row.attribute]) {
    select.append(option('', '请选择档位'));
    select.disabled = true;
    row.value = '';
    return;
  }
  select.disabled = false;
  select.append(option('', '请选择档位'));
  for (const value of RULES[row.attribute].values) select.append(option(value));
  if (RULES[row.attribute].values.includes(row.value)) select.value = row.value;
}

function renderRows() {
  dom.rowsContainer.replaceChildren();
  state.rows.forEach((row, index) => {
    const line = document.createElement('div');
    line.className = 'attribute-row';

    const idx = document.createElement('span');
    idx.className = 'row-index';
    idx.textContent = String(index + 1);

    const attr = document.createElement('select');
    attr.append(option('', '请选择属性'));
    Object.keys(RULES).forEach(name => attr.append(option(name)));
    attr.value = row.attribute;

    const value = document.createElement('select');
    fillValueSelect(value, row);

    const score = document.createElement('span');
    const updateScore = () => {
      line.classList.remove('positive-row', 'negative-row');
      if (!row.attribute || !row.value) {
        score.textContent = '—';
        score.className = 'score-pill';
        return;
      }
      const points = scoreOf(row);
      score.textContent = formatScore(points);
      score.className = `score-pill${points > 0 ? ' positive' : points < 0 ? ' negative' : ''}`;
      if (points > 0) line.classList.add('positive-row');
      if (points < 0) line.classList.add('negative-row');
    };

    const confidence = document.createElement('span');
    const updateConfidence = () => {
      const kind = confidenceKind(row);
      confidence.className = `confidence ${kind}`;
      confidence.title = kind === 'high' ? '可信度高' : kind === 'medium' ? '建议核对' : kind === 'manual' ? '手动选择' : '尚未识别';
      confidence.replaceChildren(document.createElement('i'));
    };

    attr.addEventListener('change', () => {
      row.attribute = attr.value;
      row.value = '';
      row.confidence = 'manual';
      row.confidenceScore = 0;
      fillValueSelect(value, row);
      updateScore();
      updateConfidence();
      updateCurrentScore();
      validateRows(false);
    });

    value.addEventListener('change', () => {
      row.value = value.value;
      row.confidence = 'manual';
      row.confidenceScore = 0;
      updateScore();
      updateConfidence();
      updateCurrentScore();
      validateRows(false);
    });

    updateScore();
    updateConfidence();
    line.append(idx, attr, value, score, confidence);
    dom.rowsContainer.append(line);
  });
  updateCurrentScore();
  validateRows(false);
}

function updateCurrentScore() {
  dom.currentSubtotal.textContent = String(subtotal());
}

function validateRows(showMessage = false) {
  const attrs = state.rows.map(row => row.attribute).filter(Boolean);
  const duplicated = new Set(attrs.filter((name, index) => attrs.indexOf(name) !== index));
  const touched = showMessage || state.rows.some(row => row.attribute || row.value);
  let valid = true;

  [...dom.rowsContainer.children].forEach((line, index) => {
    const row = state.rows[index];
    const invalid = !row.attribute || !row.value || duplicated.has(row.attribute);
    line.classList.toggle('invalid', touched && invalid);
    if (invalid) valid = false;
  });

  if (showMessage) {
    if (duplicated.size) setRecordHint('同一件声骸出现了重复属性，请重新选择。', 'error');
    else if (!valid) setRecordHint('请补齐五条属性及对应档位。', 'error');
  }
  return valid;
}

function saveState() {
  localStorage.setItem(STORAGE_KEY, JSON.stringify({ slots: state.slots }));
}

function normalizeStoredSlots(value) {
  if (!value || typeof value !== 'object') return {};
  const output = {};
  for (let slot = 1; slot <= 5; slot += 1) {
    const record = value[slot] || value[String(slot)];
    if (!record || !Array.isArray(record.rows)) continue;
    const rows = record.rows.slice(0, 5).map(row => ({
      attribute: String(row.attribute || ''),
      value: String(row.value || ''),
      score: Number.isFinite(Number(row.score)) ? Number(row.score) : scoreOf(row)
    }));
    if (rows.length === 5) {
      output[slot] = {
        rows,
        subtotal: rows.reduce((sum, row) => sum + row.score, 0),
        updatedAt: record.updatedAt || Date.now()
      };
    }
  }
  return output;
}

function loadState() {
  try {
    const primary = localStorage.getItem(STORAGE_KEY);
    if (primary) {
      state.slots = normalizeStoredSlots(JSON.parse(primary).slots);
      return;
    }
    for (const key of LEGACY_STORAGE_KEYS) {
      const raw = localStorage.getItem(key);
      if (!raw) continue;
      const parsed = JSON.parse(raw);
      const slots = key === 'wuwa:wuwaEchoSlots' ? parsed : parsed.slots;
      state.slots = normalizeStoredSlots(slots);
      saveState();
      return;
    }
  } catch {
    state.slots = {};
  }
}

function renderSlotSelect() {
  dom.slotSelect.replaceChildren();
  for (let slot = 1; slot <= 5; slot += 1) {
    const suffix = state.slots[slot] ? '（已记录）' : '';
    dom.slotSelect.append(option(String(slot), `声骸 ${slot}${suffix}`));
  }
  dom.slotSelect.value = String(state.selectedSlot);
}

function selectSlot(slot) {
  state.selectedSlot = slot;
  renderSlotSelect();
  renderSlots();
}

function renderSlots() {
  dom.slotsList.replaceChildren();
  let count = 0;
  let total = 0;

  for (let slot = 1; slot <= 5; slot += 1) {
    const record = state.slots[slot];
    if (!record) {
      const card = document.createElement('button');
      card.type = 'button';
      card.className = `slot-card empty${state.selectedSlot === slot ? ' selected' : ''}`;
      card.title = `选择声骸 ${slot}`;
      const symbol = document.createElement('span');
      symbol.className = 'empty-symbol';
      const title = document.createElement('strong');
      title.textContent = `声骸 ${slot}`;
      const hint = document.createElement('span');
      hint.textContent = state.selectedSlot === slot ? '当前记录位置' : '尚未记录';
      card.append(symbol, title, hint);
      card.addEventListener('click', () => selectSlot(slot));
      dom.slotsList.append(card);
      continue;
    }

    count += 1;
    total += Number(record.subtotal) || 0;
    const card = document.createElement('article');
    card.className = `slot-card recorded${state.selectedSlot === slot ? ' selected' : ''}`;

    const head = document.createElement('div');
    head.className = 'slot-card-head';
    const title = document.createElement('strong');
    title.textContent = `声骸 ${slot}`;
    const subtotalEl = document.createElement('span');
    subtotalEl.className = 'slot-subtotal';
    const subtotalLabel = document.createElement('span');
    subtotalLabel.textContent = '小计';
    const subtotalScore = document.createElement('span');
    subtotalScore.className = `slot-subtotal-score${record.subtotal > 0 ? ' positive' : record.subtotal < 0 ? ' negative' : ' zero'}`;
    subtotalScore.textContent = formatScore(record.subtotal);
    subtotalEl.append(subtotalLabel, subtotalScore);
    head.append(title, subtotalEl);

    const table = document.createElement('div');
    table.className = 'slot-props';
    for (const row of record.rows) {
      const line = document.createElement('div');
      line.className = 'slot-prop-row';
      const attr = document.createElement('span');
      attr.className = 'prop-name';
      attr.textContent = shortAttribute(row.attribute);
      attr.title = row.attribute;
      const value = document.createElement('span');
      value.className = 'prop-value';
      value.textContent = row.value;
      const score = document.createElement('strong');
      score.className = `prop-score${row.score > 0 ? ' positive' : row.score < 0 ? ' negative' : ' zero'}`;
      score.textContent = formatScore(row.score);
      line.append(attr, value, score);
      table.append(line);
    }

    const actions = document.createElement('div');
    actions.className = 'slot-actions';
    const edit = document.createElement('button');
    edit.type = 'button';
    edit.textContent = '编辑';
    edit.addEventListener('click', () => loadSlot(slot));
    const remove = document.createElement('button');
    remove.type = 'button';
    remove.className = 'danger';
    remove.textContent = '删除';
    remove.addEventListener('click', () => deleteSlot(slot));
    actions.append(edit, remove);
    card.append(head, table, actions);
    dom.slotsList.append(card);
  }

  dom.recordCount.textContent = `${count} / 5`;
  dom.recordedSummary.textContent = `${count} / 5`;
  dom.rawTotal.textContent = String(total);
  dom.finalAverage.textContent = (total / 5).toFixed(1);
  dom.exportRecords.disabled = count !== 5;
  dom.exportHint.textContent = count === 5 ? '可导出一张包含五件声骸的 PNG 图片。' : `还需记录 ${5 - count} 件声骸。`;
}

function loadSlot(slot) {
  const record = state.slots[slot];
  if (!record) return;
  state.selectedSlot = slot;
  state.rows = record.rows.map(row => ({ ...row, confidence: 'manual', confidenceScore: 0, sourceText: '' }));
  renderRows();
  renderSlotSelect();
  renderSlots();
  setRecordHint(`已载入声骸 ${slot}，修改后可覆盖。`);
}

function closeConfirmDialog(accepted) {
  if (!activeConfirmation) return;
  const resolve = activeConfirmation;
  activeConfirmation = null;
  dom.confirmOverlay.hidden = true;
  resolve(accepted);
  if (confirmationFocus instanceof HTMLElement) confirmationFocus.focus();
  confirmationFocus = null;
}

function showConfirmDialog(title, message, confirmText = '确定') {
  if (activeConfirmation) closeConfirmDialog(false);
  confirmationFocus = document.activeElement;
  dom.confirmTitle.textContent = title;
  dom.confirmMessage.textContent = message;
  dom.confirmAccept.textContent = confirmText;
  dom.confirmOverlay.hidden = false;
  window.setTimeout(() => dom.confirmAccept.focus(), 0);
  return new Promise(resolve => { activeConfirmation = resolve; });
}

function setupConfirmDialog() {
  dom.confirmAccept.addEventListener('click', () => closeConfirmDialog(true));
  dom.confirmCancel.addEventListener('click', () => closeConfirmDialog(false));
  dom.confirmOverlay.addEventListener('click', event => {
    if (event.target === dom.confirmOverlay) closeConfirmDialog(false);
  });
  document.addEventListener('keydown', event => {
    if (!activeConfirmation) return;
    if (event.key === 'Escape') closeConfirmDialog(false);
    if (event.key === 'Enter' && document.activeElement !== dom.confirmCancel) closeConfirmDialog(true);
  });
}

function exportTitleLength(value) {
  return Array.from(value).reduce((length, character) => (
    length + (/\p{Script=Han}/u.test(character) ? 1 : .5)
  ), 0);
}

function updateExportTitleValidation() {
  const invalid = exportTitleLength(dom.exportTitleInput.value.trim()) > 12;
  dom.exportTitleInput.classList.toggle('invalid', invalid);
  dom.exportTitleHint.classList.toggle('invalid', invalid);
  dom.exportTitleHint.hidden = !invalid;
  return !invalid;
}

function closeExportDialog(title = null) {
  if (!activeExportDialog) return;
  const resolve = activeExportDialog;
  activeExportDialog = null;
  dom.exportOverlay.hidden = true;
  resolve(title);
  if (exportDialogFocus instanceof HTMLElement) exportDialogFocus.focus();
  exportDialogFocus = null;
}

function submitExportDialog() {
  if (!updateExportTitleValidation()) {
    dom.exportTitleInput.focus();
    return;
  }
  closeExportDialog(dom.exportTitleInput.value.trim());
}

function showExportTitleDialog() {
  if (activeExportDialog) closeExportDialog(null);
  exportDialogFocus = document.activeElement;
  dom.exportTitleInput.value = '';
  updateExportTitleValidation();
  dom.exportOverlay.hidden = false;
  window.setTimeout(() => dom.exportTitleInput.focus(), 0);
  return new Promise(resolve => { activeExportDialog = resolve; });
}

function setupExportDialog() {
  dom.exportTitleInput.addEventListener('input', updateExportTitleValidation);
  dom.exportAccept.addEventListener('click', submitExportDialog);
  dom.exportCancel.addEventListener('click', () => closeExportDialog(null));
  dom.exportOverlay.addEventListener('click', event => {
    if (event.target === dom.exportOverlay) closeExportDialog(null);
  });
  document.addEventListener('keydown', event => {
    if (!activeExportDialog) return;
    if (event.key === 'Escape') closeExportDialog(null);
    if (event.key === 'Enter' && document.activeElement !== dom.exportCancel) {
      event.preventDefault();
      submitExportDialog();
    }
  });
}

async function requestExportRecords() {
  if ([1, 2, 3, 4, 5].some(slot => !state.slots[slot])) return;
  const title = await showExportTitleDialog();
  if (title === null) return;
  try {
    await exportRecordsImage(title);
    setStatus('记录图片已导出', 'success');
  } catch (error) {
    setStatus(error instanceof Error ? error.message : String(error), 'error');
  }
}

async function deleteSlot(slot) {
  if (!await showConfirmDialog('删除记录', `确认删除声骸 ${slot} 的记录？`, '删除')) return;
  delete state.slots[slot];
  saveState();
  renderSlotSelect();
  renderSlots();
  setRecordHint(`声骸 ${slot} 的记录已删除。`);
}

async function confirmRecord() {
  if (!validateRows(true)) return;
  const slot = state.selectedSlot;
  if (state.slots[slot] && !await showConfirmDialog('覆盖记录', `声骸 ${slot} 已有记录，确认覆盖？`, '覆盖')) return;
  const rows = state.rows.map(row => ({
    attribute: row.attribute,
    value: row.value,
    score: scoreOf(row)
  }));
  const points = rows.reduce((sum, row) => sum + row.score, 0);
  state.slots[slot] = { rows, subtotal: points, updatedAt: Date.now() };
  saveState();
  const next = [1, 2, 3, 4, 5].find(number => !state.slots[number]);
  if (next) state.selectedSlot = next;
  renderSlotSelect();
  renderSlots();
  setRecordHint(`声骸 ${slot} 已记录，小计 ${points} 分。`, 'success');
}

async function clearAll() {
  if (!Object.keys(state.slots).length) return;
  if (!await showConfirmDialog('清空记录', '确认清空5个声骸的全部记录？')) return;
  state.slots = {};
  state.selectedSlot = 1;
  saveState();
  renderSlotSelect();
  renderSlots();
  setRecordHint('全部记录已清空。');
}
