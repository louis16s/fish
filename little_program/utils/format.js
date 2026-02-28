function pad2(v) {
  const n = Number(v) || 0;
  return n < 10 ? `0${n}` : `${n}`;
}

function fmtDateTime(v) {
  if (!v) return '--';
  const d = new Date(v);
  if (!Number.isFinite(d.getTime())) return String(v);
  return `${d.getFullYear()}-${pad2(d.getMonth() + 1)}-${pad2(d.getDate())} ${pad2(d.getHours())}:${pad2(d.getMinutes())}:${pad2(d.getSeconds())}`;
}

function gateStateText(n) {
  if (Number(n) === 1) return '开闸';
  if (Number(n) === 2) return '关闸';
  return '待机';
}

function alarmText(alarm) {
  if (!alarm || !alarm.active) return '正常';
  const sev = Number.isFinite(alarm.severity) ? alarm.severity : 0;
  return `S${sev} ${alarm.text || 'alarm'}`;
}

function safeParseJSON(raw, fallback) {
  try {
    return JSON.parse(raw);
  } catch (e) {
    return fallback;
  }
}

module.exports = {
  pad2,
  fmtDateTime,
  gateStateText,
  alarmText,
  safeParseJSON
};
