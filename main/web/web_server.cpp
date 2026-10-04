#include "web/web_server.hpp"

#include "config/motion_config.hpp"
#include "measurement/measurement_controller.hpp"
#include "wifi/wifi_manager.hpp"
#include "esp_http_server.h"
#include "esp_log.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr char TAG[] = "web";

const char PAGE[] = R"HTML(
<!doctype html>
<html lang="ru">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>NeedleMeter — панель оператора</title>
  <style>
    :root{--bg:#f3f5f8;--card:#fff;--text:#1b2735;--muted:#667484;--line:#dde3ea;--blue:#175ea8;--green:#18864b;--yellow:#c98a12;--red:#bd2d35;--gray:#a8b1bb}
    *{box-sizing:border-box} body{margin:0;background:var(--bg);color:var(--text);font:16px/1.45 Inter,Segoe UI,Arial,sans-serif}
    .app{max-width:1220px;margin:0 auto;padding:30px 24px 44px}.topbar{display:flex;align-items:center;justify-content:space-between;margin-bottom:18px}
    h1{font-size:24px;letter-spacing:-.02em;margin:0} h2{font-size:17px;margin:0 0 18px}.subtitle,.caption{color:var(--muted);margin:3px 0 0}.connection{font-size:13px;color:var(--muted)}
    .card{background:var(--card);border:1px solid var(--line);border-radius:16px;box-shadow:0 5px 18px rgba(31,49,70,.06);padding:24px}
    .hero{display:grid;grid-template-columns:minmax(0,1fr) 34px minmax(270px,330px);gap:16px;align-items:stretch}.result-card{min-height:292px;display:flex;flex-direction:column}
    .eyebrow{font-size:13px;font-weight:700;letter-spacing:.06em;text-transform:uppercase;color:var(--muted)}.measurement{font-size:clamp(52px,6.1vw,82px);font-weight:750;letter-spacing:-.06em;line-height:1.04;margin:22px 0 18px}.measurement small{font-size:.43em;letter-spacing:0}
    .metrics{display:flex;gap:20px;flex-wrap:wrap;color:var(--muted)}.metrics b{color:var(--text);font-weight:650}.result-state{font-size:18px;font-weight:750;margin-top:auto;padding-top:22px}.state-green{color:var(--green)}.state-yellow{color:var(--yellow)}.state-red{color:var(--red)}.state-gray{color:var(--gray)}
    .indicator-card{padding:10px;display:flex;align-items:center;justify-content:center}.indicator{width:100%;height:100%;min-height:270px;border-radius:9px;background:var(--gray);box-shadow:inset 0 0 0 1px rgba(0,0,0,.08);transition:background .2s}.indicator.green{background:var(--green)}.indicator.yellow{background:#e1a11f}.indicator.red{background:var(--red)}.indicator.gray{background:var(--gray)}
    .controls{display:flex;flex-direction:column}.operation{min-height:22px;color:var(--muted);font-size:14px;margin:-9px 0 14px}.action-list{display:grid;gap:10px;margin-top:auto}.btn{width:100%;border:0;border-radius:10px;padding:14px 16px;font:inherit;font-weight:700;cursor:pointer;transition:filter .15s,transform .15s}.btn:hover:not(:disabled){filter:brightness(.96);transform:translateY(-1px)}.btn:disabled{cursor:not-allowed;opacity:.45}.btn-primary{background:var(--blue);color:#fff}.btn-secondary{background:#e8eef5;color:#1e4c7b}.btn-auto-on{background:var(--green);color:#fff}.btn-stop{background:var(--red);color:#fff}.btn-warn{background:#f6e8c9;color:#7b5300}.btn-neutral{background:#e9edf2;color:#3b4653}
    .message{min-height:0;margin:14px 0 0;padding:0;border-radius:10px;color:var(--red);font-weight:650}.message:not(:empty){padding:10px 13px;background:#fdf0f1}.message.info{color:var(--green);background:#eff9f2}
    .middle{display:grid;grid-template-columns:230px minmax(0,1fr);gap:16px;margin-top:16px}.emergency{display:grid;align-content:start;gap:10px}.emergency .btn{padding:15px}.system-grid{display:grid;grid-template-columns:max-content minmax(0,1fr);gap:10px 24px;margin:0}.system-grid dt{color:var(--muted)}.system-grid dd{margin:0;font-weight:650;overflow-wrap:anywhere}.value-alert{color:var(--red)}.value-ok{color:var(--green)}
    .settings{margin-top:16px}.settings-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:15px 20px}.field{display:grid;grid-template-columns:minmax(0,1fr) 146px;align-items:center;gap:12px}.field label{font-weight:600}.input-wrap{display:flex;align-items:center;border:1px solid #cbd4df;border-radius:9px;background:#fff;overflow:hidden}.input-wrap input{min-width:0;width:100%;border:0;outline:0;padding:10px 10px;font:inherit;text-align:right}.unit{padding-right:10px;color:var(--muted);font-size:14px}.check-field{display:flex;align-items:center;gap:10px;padding:10px 0}.check-field input{width:18px;height:18px;accent-color:var(--blue)}.settings-actions{margin-top:21px;display:flex;justify-content:flex-end}.settings-actions .btn{width:auto;min-width:210px}
    .service{margin-top:16px}.service-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px 28px}.service-values{display:grid;grid-template-columns:max-content minmax(0,1fr);gap:8px 18px;margin:0}.service-values dt{color:var(--muted)}.service-values dd{margin:0;font-weight:650;overflow-wrap:anywhere}.service-actions{display:flex;gap:10px;align-items:end}.service-actions .btn{width:auto;min-width:156px}.service-result{margin-top:20px;padding-top:18px;border-top:1px solid var(--line)}.service-result-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px 28px}.service-result-grid span{color:var(--muted)}.service-result-grid b{display:block;font-size:18px;color:var(--text)}.service-note{margin:14px 0 0;color:var(--muted);font-size:14px}
    @media(max-width:760px){.app{padding:20px 14px 32px}.topbar{align-items:flex-start;gap:12px;flex-direction:column}.hero,.middle,.service-grid{grid-template-columns:1fr}.indicator-card{min-height:28px;padding:0}.indicator{min-height:28px;height:28px;width:100%;border-radius:8px}.result-card{min-height:0}.settings-grid{grid-template-columns:1fr}.field{grid-template-columns:minmax(0,1fr) 140px}.service-actions{align-items:stretch;flex-direction:column}.service-actions .btn{width:100%}.service-result-grid{grid-template-columns:1fr}}
  </style>
</head>
<body>
  <main class="app">
    <header class="topbar">
      <div><h1>NeedleMeter</h1><p class="subtitle">Панель оператора</p></div>
      <div id="connection" class="connection">Получение состояния…</div>
    </header>

    <section class="hero" aria-label="Результат и управление">
      <article class="card result-card">
        <div class="eyebrow">Последнее измерение</div>
        <div id="length" class="measurement">—</div>
        <div class="metrics">
          <span>Отклонение: <b id="deviation">—</b></span>
          <span>Номинал: <b id="nominal">—</b></span>
          <span>Допуск: <b id="tolerance">—</b></span>
        </div>
        <div id="result-state" class="result-state state-gray">Нет измерения</div>
      </article>
      <aside class="card indicator-card" aria-label="Индикатор допуска"><div id="indicator" class="indicator gray"></div></aside>
      <section class="card controls">
        <h2>Управление</h2>
        <div id="operation" class="operation">Ожидание команды</div>
        <div class="action-list">
          <button id="measure" class="btn btn-primary" type="button">Измерить</button>
          <button id="calibrate" class="btn btn-secondary" type="button">Калибровать</button>
          <button id="auto" class="btn btn-secondary" type="button">Авто: ВЫКЛ</button>
        </div>
      </section>
    </section>

    <p id="message" class="message" role="status"></p>

    <section class="middle">
      <aside class="card emergency" aria-label="Аварийное управление">
        <h2>Аварийное управление</h2>
        <button id="stop-command" class="btn btn-stop" type="button">STOP</button>
        <button id="reset-stop" class="btn btn-neutral" type="button">Сброс STOP</button>
        <button id="reset-error" class="btn btn-warn" type="button">Сброс ошибки</button>
      </aside>
      <section class="card">
        <h2>Состояние системы</h2>
        <dl class="system-grid">
          <dt>Игла</dt><dd id="needle">—</dd>
          <dt>CONTACT</dt><dd id="contact">—</dd>
          <dt>STOP</dt><dd id="stop">—</dd>
          <dt>Ошибка</dt><dd id="error">—</dd>
          <dt>Wi‑Fi</dt><dd id="wifi">—</dd>
          <dt>IP</dt><dd id="ip">—</dd>
          <dt>Текущая фаза</dt><dd id="phase">—</dd>
          <dt>Автоматический режим</dt><dd id="auto-state">—</dd>
          <dt>Автофаза</dt><dd id="auto-phase">—</dd>
          <dt>Калибровка</dt><dd id="calibration-source">—</dd>
          <dt>Опорная длина</dt><dd id="calibration-reference">—</dd>
          <dt>Измерений после калибровки</dt><dd id="measurements-since-calibration">—</dd>
        </dl>
      </section>
    </section>

    <section class="card settings">
      <h2>Настройки оператора</h2>
      <form id="config-form">
        <div class="settings-grid">
          <div class="field"><label for="nominal_length_mm">Номинальная длина</label><div class="input-wrap"><input id="nominal_length_mm" type="number" step="0.001" required><span class="unit">мм</span></div></div>
          <div class="field"><label for="tolerance_mm">Допуск</label><div class="input-wrap"><input id="tolerance_mm" type="number" step="0.001" min="0" required><span class="unit">мм</span></div></div>
          <div class="field"><label for="calibration_length_mm">Длина ручного калибра</label><div class="input-wrap"><input id="calibration_length_mm" type="number" step="0.001" required><span class="unit">мм</span></div></div>
          <div class="field"><label for="auto_calibration_length_mm">Длина автокалибра</label><div class="input-wrap"><input id="auto_calibration_length_mm" type="number" step="0.001" required><span class="unit">мм</span></div></div>
          <div class="field"><label for="retract_mm">Расстояние отъезда</label><div class="input-wrap"><input id="retract_mm" type="number" step="0.001" min="0" required><span class="unit">мм</span></div></div>
          <label class="check-field" for="measure_dir_inverted"><input id="measure_dir_inverted" type="checkbox">Инвертировать DIR</label>
        </div>
        <div class="settings-actions"><button class="btn btn-primary" type="submit">Сохранить настройки</button></div>
      </form>
    </section>

    <section class="card service" aria-label="Калибровка перемещения">
      <h2>Калибровка перемещения</h2>
      <p class="caption">Сервисный раздел. Перед движением убедитесь, что каретке доступен свободный ход.</p>
      <div class="service-grid">
        <dl class="service-values">
          <dt>Текущий коэффициент</dt><dd id="service-current-mm-per-step">—</dd>
          <dt>Шагов на мм</dt><dd id="service-current-steps-per-mm">—</dd>
          <dt>Последняя команда</dt><dd id="service-last-command">—</dd>
        </dl>
        <form id="service-move-form">
          <div class="field"><label for="service-distance-mm">Заданное перемещение</label><div class="input-wrap"><input id="service-distance-mm" type="number" step="0.001" required value="-100.000"><span class="unit">мм</span></div></div>
          <div class="settings-actions"><button id="service-move" class="btn btn-secondary" type="submit">Переместить</button></div>
        </form>
      </div>
      <div class="service-result">
        <div class="service-grid">
          <div class="field"><label for="service-actual-distance-mm">Фактическое перемещение</label><div class="input-wrap"><input id="service-actual-distance-mm" type="number" step="0.001" min="0" required placeholder="99.720"><span class="unit">мм</span></div></div>
          <div class="service-actions"><button id="service-calculate" class="btn btn-secondary" type="button">Рассчитать</button><button id="service-apply" class="btn btn-primary" type="button" disabled>Применить коэффициент</button></div>
        </div>
        <div class="service-result-grid">
          <div><span>Ожидалось</span><b id="service-expected">—</b></div>
          <div><span>Фактически</span><b id="service-actual">—</b></div>
          <div><span>Ошибка</span><b id="service-error-mm">—</b></div>
          <div><span>Ошибка, %</span><b id="service-error-percent">—</b></div>
          <div><span>Рекомендуемый коэффициент</span><b id="service-recommended-mm-per-step">—</b></div>
          <div><span>Рекомендуемые шаги на мм</span><b id="service-recommended-steps-per-mm">—</b></div>
        </div>
        <p class="service-note">Отрицательное значение — влево, положительное — вправо. Фактическое перемещение вводится всегда положительным числом.</p>
      </div>
    </section>
  </main>
  <script>
    const phaseNames={IDLE:'Ожидание',MEASURE_FAST:'Быстрый проход',MEASURE_RETRACT:'Отъезд',MEASURE_FINE:'Точное измерение',MEASURE_FINAL_RETRACT:'Финальный отъезд',CALIBRATE_FAST:'Калибровка: быстрый проход',CALIBRATE_RETRACT:'Калибровка: отъезд',CALIBRATE_FINE:'Калибровка: точное касание',CALIBRATE_FINAL_RETRACT:'Калибровка: финальный отъезд',AUTO_WAIT_SENSOR_CLEAR:'Ожидание освобождения датчика',AUTO_WAIT_NEEDLE:'Ожидание иглы',AUTO_WAIT_REMOVE:'Ожидание снятия иглы',AUTO_MOVE_TO_LOAD:'Возврат в загрузочную позицию',MANUAL_LEFT:'Ручное перемещение',SERVICE_MOVE:'Сервисное перемещение',FINISHED:'Операция завершена',STOPPED:'STOP',ERROR:'Ошибка'};
    const errorNames={NONE:'Нет',NEEDLE_NOT_FOUND:'Игла не обнаружена',CONTACT_ALREADY_ACTIVE:'CONTACT уже активен',CONTACT_RELEASE_TIMEOUT:'CONTACT не освободился вовремя',MEASUREMENT_TIMEOUT:'Тайм-аут измерения',CALIBRATION_TIMEOUT:'Тайм-аут калибровки',MAX_STEPS_REACHED:'Достигнут лимит шагов',NOT_CALIBRATED:'Нет калибровки',STOP_ACTIVE:'Активен STOP',INVALID_CONFIG:'Некорректные настройки',INTERNAL_ERROR:'Внутренняя ошибка'};
    let currentStatus=null;
    let lastServiceCommandMm=0;
    let recommendedMmPerStep=null;
    const byId=id=>document.getElementById(id);
    const number=(value,digits=3)=>Number(value).toFixed(digits);
    const mm=(value,digits=3)=>`${number(value,digits)} мм`;
    const signedMm=value=>`${Number(value)>=0?'+':''}${mm(value)}`;
    const setText=(id,value)=>{byId(id).textContent=value;};
    function showMessage(text,isError=false){const box=byId('message');box.textContent=text||'';box.className=`message${text?(isError?'':' info'):''}`;}
    function resultView(status){
      const hasMeasurement=status.result&&status.result!=='NONE';
      if(!hasMeasurement)return {color:'gray',label:'Нет измерения'};
      const deviation=Math.abs(Number(status.last_length_mm)-Number(status.nominal_length_mm));
      const tolerance=Math.max(0,Number(status.tolerance_mm));
      if(deviation>tolerance)return {color:'red',label:'Вне допуска'};
      if(tolerance>0&&deviation>=tolerance*.8)return {color:'yellow',label:'Близко к границе'};
      return {color:'green',label:'В допуске'};
    }
    function isBusy(state){return !['IDLE','FINISHED','ERROR','STOPPED'].includes(state);}
    function setServiceSummary(status){
      const coefficient=Number(status.mm_per_step);
      setText('service-current-mm-per-step',Number.isFinite(coefficient)&&coefficient>0?`${number(coefficient,9)} мм/шаг`:'—');
      setText('service-current-steps-per-mm',Number.isFinite(coefficient)&&coefficient>0?`${number(1/coefficient,3)} шаг/мм`:'—');
      const reportedCommand=Number(status.service_move_requested_mm);
      if(Number.isFinite(reportedCommand)&&reportedCommand!==0)lastServiceCommandMm=reportedCommand;
      setText('service-last-command',lastServiceCommandMm?signedMm(lastServiceCommandMm):'—');
    }
    function clearServiceCalculation(){
      recommendedMmPerStep=null;
      for(const id of ['service-expected','service-actual','service-error-mm','service-error-percent','service-recommended-mm-per-step','service-recommended-steps-per-mm'])setText(id,'—');
      byId('service-apply').disabled=true;
    }
    function calculateServiceCoefficient(){
      const actual=Number(byId('service-actual-distance-mm').value);
      const expected=Math.abs(lastServiceCommandMm);
      const coefficient=currentStatus&&Number(currentStatus.mm_per_step);
      if(!Number.isFinite(expected)||expected<=0){showMessage('Сначала выполните сервисное перемещение.',true);return;}
      if(!Number.isFinite(actual)||actual<=0){showMessage('Введите положительное фактическое перемещение.',true);return;}
      if(!Number.isFinite(coefficient)||coefficient<=0){showMessage('Текущий коэффициент недоступен.',true);return;}
      const error=actual-expected;
      recommendedMmPerStep=coefficient*actual/expected;
      setText('service-expected',mm(expected));setText('service-actual',mm(actual));setText('service-error-mm',signedMm(error));setText('service-error-percent',`${error>=0?'+':''}${number(error/expected*100,3)} %`);
      setText('service-recommended-mm-per-step',`${number(recommendedMmPerStep,9)} мм/шаг`);
      setText('service-recommended-steps-per-mm',`${number(1/recommendedMmPerStep,3)} шаг/мм`);
      byId('service-apply').disabled=false;
    }
    function put(status){
      currentStatus=status;
      const hasMeasurement=status.result&&status.result!=='NONE';
      const view=resultView(status);
      setText('length',hasMeasurement?mm(status.last_length_mm):'—');
      setText('deviation',hasMeasurement?signedMm(status.deviation_mm):'—');
      setText('nominal',mm(status.nominal_length_mm)); setText('tolerance',`±${mm(status.tolerance_mm)}`);
      const result=byId('result-state');result.textContent=view.label;result.className=`result-state state-${view.color}`;
      byId('indicator').className=`indicator ${view.color}`;
      const error=status.error||'NONE'; const stopActive=status.stop_active||status.state==='STOPPED';
      setText('needle',status.needle_present?'Обнаружена':'Отсутствует'); setText('contact',status.contact?'Активен':'Свободен');
      setText('stop',stopActive?'АКТИВЕН':'Нормально');byId('stop').className=stopActive?'value-alert':'value-ok';
      setText('error',errorNames[error]||error);byId('error').className=error==='NONE'?'value-ok':'value-alert';
      setText('wifi',status.wifi_connected?`${status.wifi_mode}: подключён`:`${status.wifi_mode}: отключён`);setText('ip',status.ip_address||'—');
      setText('phase',phaseNames[status.state]||status.state);setText('auto-state',status.auto_mode_enabled?'ВКЛ':'ВЫКЛ');setText('auto-phase',status.auto_phase||'—');
      setText('calibration-source',status.calibration_source==='AUTO'?'Автоматическая':status.calibration_source==='MANUAL'?'Ручная':'Нет калибровки');
      setText('calibration-reference',status.calibration_valid?mm(status.calibration_reference_length_mm):'Нет калибровки');
      setText('measurements-since-calibration',String(status.measurements_since_calibration??0));
      setServiceSummary(status);
      const busy=isBusy(status.state);byId('measure').disabled=busy||status.auto_mode_enabled;byId('calibrate').disabled=busy||status.auto_mode_enabled;
      const auto=byId('auto');auto.textContent=`Авто: ${status.auto_mode_enabled?'ВКЛ':'ВЫКЛ'}`;auto.className=`btn ${status.auto_mode_enabled?'btn-auto-on':'btn-secondary'}`;auto.disabled=busy;
      byId('reset-stop').disabled=status.state!=='STOPPED';byId('reset-error').disabled=status.state!=='ERROR';
      byId('service-move').disabled=busy||status.auto_mode_enabled||stopActive||error!=='NONE';
      if(recommendedMmPerStep===null)byId('service-apply').disabled=true;
      setText('operation',busy?(phaseNames[status.state]||status.state):status.auto_mode_enabled?(status.auto_phase||'Автоматический режим'):'Ожидание команды');
      setText('connection','Устройство на связи');
      if(stopActive)showMessage('STOP активен. После устранения причины нажмите «Сброс STOP».',true);else if(error!=='NONE')showMessage(errorNames[error]||error,true);
    }
    async function request(path){const response=await fetch(`/api/${path}`,{method:'POST'});if(!response.ok)throw new Error(await response.text()||'Команда отклонена');showMessage('Команда принята');await refreshStatus();}
    async function refreshStatus(){try{const response=await fetch('/api/status');if(!response.ok)throw new Error();put(await response.json());}catch(_){setText('connection','Нет связи с устройством');showMessage('Не удалось получить состояние устройства.',true);}}
    async function loadConfig(){try{const response=await fetch('/api/config');if(!response.ok)throw new Error();const config=await response.json();for(const key of ['nominal_length_mm','tolerance_mm','calibration_length_mm','auto_calibration_length_mm','retract_mm'])byId(key).value=config[key];byId('measure_dir_inverted').checked=Boolean(config.measure_dir_inverted);}catch(_){showMessage('Не удалось загрузить настройки.',true);}}
    async function saveConfig(event){event.preventDefault();const config={};for(const key of ['nominal_length_mm','tolerance_mm','calibration_length_mm','auto_calibration_length_mm','retract_mm'])config[key]=Number(byId(key).value);config.measure_dir_inverted=byId('measure_dir_inverted').checked;try{const response=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(config)});if(!response.ok)throw new Error(await response.text()||'Настройки отклонены');showMessage('Настройки сохранены');await refreshStatus();}catch(error){showMessage(error.message||'Не удалось сохранить настройки.',true);}}
    async function serviceMove(event){
      event.preventDefault();
      const distanceMm=Number(byId('service-distance-mm').value);
      if(!Number.isFinite(distanceMm)||distanceMm===0||Math.abs(distanceMm)>150){showMessage('Введите ненулевое перемещение не более 150 мм.',true);return;}
      try{
        const response=await fetch('/api/service-move',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({distance_mm:distanceMm})});
        if(!response.ok)throw new Error(await response.text()||'Сервисное перемещение отклонено');
        lastServiceCommandMm=distanceMm;clearServiceCalculation();showMessage('Сервисное перемещение принято');await refreshStatus();
      }catch(error){showMessage(error.message||'Не удалось запустить сервисное перемещение.',true);}
    }
    async function applyServiceCoefficient(){
      if(!currentStatus||!Number.isFinite(recommendedMmPerStep)||recommendedMmPerStep<=0){showMessage('Сначала рассчитайте коэффициент.',true);return;}
      const current=Number(currentStatus.mm_per_step);
      if(!window.confirm(`Изменить MM_PER_STEP с\n${number(current,9)}\nна\n${number(recommendedMmPerStep,9)}?\n\nПосле изменения необходимо выполнить новую калибровку.`))return;
      try{
        const response=await fetch('/api/service/mm-per-step',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mm_per_step:recommendedMmPerStep})});
        if(!response.ok)throw new Error(await response.text()||'Коэффициент отклонён');
        recommendedMmPerStep=null;showMessage('Коэффициент сохранён. Выполните калибровку устройства.');await refreshStatus();
      }catch(error){showMessage(error.message||'Не удалось сохранить коэффициент.',true);}
    }
    byId('measure').addEventListener('click',()=>request('measure').catch(error=>showMessage(error.message,true)));
    byId('calibrate').addEventListener('click',()=>request('calibrate').catch(error=>showMessage(error.message,true)));
    byId('auto').addEventListener('click',()=>request(currentStatus&&currentStatus.auto_mode_enabled?'auto/stop':'auto/start').catch(error=>showMessage(error.message,true)));
    byId('stop-command').addEventListener('click',()=>request('stop').catch(error=>showMessage(error.message,true)));
    byId('reset-stop').addEventListener('click',()=>request('reset-stop').catch(error=>showMessage(error.message,true)));
    byId('reset-error').addEventListener('click',()=>request('reset-error').catch(error=>showMessage(error.message,true)));
    byId('config-form').addEventListener('submit',saveConfig);
    byId('service-move-form').addEventListener('submit',serviceMove);
    byId('service-calculate').addEventListener('click',calculateServiceCoefficient);
    byId('service-apply').addEventListener('click',applyServiceCoefficient);
    loadConfig();refreshStatus();setInterval(refreshStatus,700);
  </script>
</body>
</html>
)HTML";

WebContext *context(httpd_req_t *request) { return static_cast<WebContext *>(request->user_ctx); }

esp_err_t json(httpd_req_t *request, const char *body) {
    httpd_resp_set_type(request, HTTPD_TYPE_JSON);
    return httpd_resp_sendstr(request, body);
}

esp_err_t conflict(httpd_req_t *request, const char *message) {
    httpd_resp_set_status(request, "409 Conflict");
    httpd_resp_set_type(request, HTTPD_TYPE_TEXT);
    return httpd_resp_sendstr(request, message);
}

esp_err_t root(httpd_req_t *request) {
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, PAGE, HTTPD_RESP_USE_STRLEN);
}

const char *calibration_source_name(CalibrationSource source) {
    return source == CalibrationSource::Auto ? "AUTO" : source == CalibrationSource::Manual ? "MANUAL" : "NONE";
}

bool snapshot(httpd_req_t *request, StatusSnapshot &measurement, WifiStatus &wifi) {
    auto *web = context(request);
    return web && web->controller && web->wifi && web->controller->snapshot(measurement) && ((wifi = web->wifi->snapshot()), true);
}

esp_err_t status(httpd_req_t *request) {
    StatusSnapshot measurement{};
    WifiStatus wifi{};
    if (!snapshot(request, measurement, wifi)) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Controller unavailable");
    char body[2200];
    const int length = std::snprintf(body, sizeof(body),
        "{\"state\":\"%s\",\"error\":\"%s\",\"needle_present\":%s,\"contact\":%s,\"stop_active\":%s,\"motor_enabled\":%s,\"auto_mode_enabled\":%s,\"auto_phase\":\"%s\",\"current_speed_steps_s\":%lu,\"position_steps\":%lld,\"last_length_mm\":%.6f,\"nominal_length_mm\":%.6f,\"deviation_mm\":%.6f,\"tolerance_mm\":%.6f,\"result\":\"%s\",\"calibration_valid\":%s,\"calibration_source\":\"%s\",\"calibration_reference_length_mm\":%.6f,\"measurements_since_calibration\":%lu,\"mm_per_step\":%.9f,\"service_move_requested_mm\":%.6f,\"service_move_target_steps\":%lu,\"wifi_mode\":\"%s\",\"wifi_connected\":%s,\"ip_address\":\"%s\"}",
        state_name(measurement.state), error_name(measurement.error), measurement.needle_present ? "true" : "false", measurement.contact ? "true" : "false", measurement.stop_active ? "true" : "false", measurement.motor_enabled ? "true" : "false", measurement.auto_mode_enabled ? "true" : "false", measurement.auto_phase, static_cast<unsigned long>(measurement.current_speed_steps_s), static_cast<long long>(measurement.position_steps), measurement.stats.last_measured_length, measurement.config.nominal_length_mm, measurement.stats.last_deviation, measurement.config.tolerance_mm, measurement.stats.last_result, measurement.stats.calibration_valid ? "true" : "false", calibration_source_name(measurement.stats.calibration_source), measurement.stats.calibration_reference_length_mm, static_cast<unsigned long>(measurement.stats.measurements_since_calibration), measurement.config.mm_per_step, measurement.service_move_requested_mm, static_cast<unsigned long>(measurement.service_move_target_steps), wifi_mode_name(wifi.mode), wifi.connected ? "true" : "false", wifi.ip_address);
    return length < 0 || static_cast<size_t>(length) >= sizeof(body) ? ESP_ERR_NO_MEM : json(request, body);
}

esp_err_t get_config(httpd_req_t *request) {
    StatusSnapshot measurement{};
    WifiStatus wifi{};
    if (!snapshot(request, measurement, wifi)) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Controller unavailable");
    char body[512];
    const int length = std::snprintf(body, sizeof(body), "{\"nominal_length_mm\":%.6f,\"tolerance_mm\":%.6f,\"calibration_length_mm\":%.6f,\"auto_calibration_length_mm\":%.6f,\"retract_mm\":%.6f,\"measure_dir_inverted\":%s,\"mm_per_step\":%.9f}", measurement.config.nominal_length_mm, measurement.config.tolerance_mm, measurement.config.calibration_length_mm, measurement.config.auto_calibration_length_mm, measurement.config.retract_mm, measurement.config.measure_dir_inverted ? "true" : "false", measurement.config.mm_per_step);
    return length < 0 || static_cast<size_t>(length) >= sizeof(body) ? ESP_ERR_NO_MEM : json(request, body);
}

const char *value(const char *body, const char *key) {
    char needle[64];
    const int length = std::snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (length < 0 || static_cast<size_t>(length) >= sizeof(needle)) return nullptr;
    const char *position = std::strstr(body, needle);
    if (!position) return nullptr;
    position += length;
    while (std::isspace(static_cast<unsigned char>(*position))) ++position;
    if (*position++ != ':') return nullptr;
    while (std::isspace(static_cast<unsigned char>(*position))) ++position;
    return position;
}

bool number(const char *position, double &value) {
    if (!position) return false;
    char *end = nullptr;
    value = std::strtod(position, &end);
    if (end == position || !std::isfinite(value)) return false;
    while (std::isspace(static_cast<unsigned char>(*end))) ++end;
    return *end == ',' || *end == '}' || !*end;
}

bool optional_double(const char *body, const char *key, double &out) {
    const char *position = value(body, key);
    if (!position) return true;
    double parsed = 0;
    if (!number(position, parsed)) return false;
    out = parsed;
    return true;
}

bool optional_bool(const char *body, const char *key, bool &out) {
    const char *position = value(body, key);
    if (!position) return true;
    if (std::strncmp(position, "true", 4) == 0) { out = true; return true; }
    if (std::strncmp(position, "false", 5) == 0) { out = false; return true; }
    return false;
}

bool request_body(httpd_req_t *request, char *body, size_t size) {
    if (request->content_len <= 0 || static_cast<size_t>(request->content_len) >= size) return false;
    int received = 0;
    while (received < request->content_len) {
        const int chunk = httpd_req_recv(request, body + received, request->content_len - received);
        if (chunk <= 0) return false;
        received += chunk;
    }
    body[received] = 0;
    return true;
}

esp_err_t post_config(httpd_req_t *request) {
    char body[1024];
    if (!request_body(request, body, sizeof(body))) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid body");
    StatusSnapshot measurement{};
    WifiStatus wifi{};
    if (!snapshot(request, measurement, wifi)) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Controller unavailable");
    DeviceConfig config = measurement.config;
    const bool valid = value(body, "mm_per_step") == nullptr && optional_double(body, "nominal_length_mm", config.nominal_length_mm) && optional_double(body, "tolerance_mm", config.tolerance_mm) && optional_double(body, "calibration_length_mm", config.calibration_length_mm) && optional_double(body, "auto_calibration_length_mm", config.auto_calibration_length_mm) && optional_double(body, "retract_mm", config.retract_mm) && optional_bool(body, "measure_dir_inverted", config.measure_dir_inverted);
    const char *reason = nullptr;
    auto *web = context(request);
    if (!valid || !web || !web->controller || !web->controller->update_config(config, &reason)) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, reason ? reason : "Settings rejected");
    return json(request, "{\"ok\":true}");
}

esp_err_t post_service_move(httpd_req_t *request) {
    char body[256];
    double distance_mm = 0.0;
    if (!request_body(request, body, sizeof(body)) || !number(value(body, "distance_mm"), distance_mm) || distance_mm == 0.0 || std::fabs(distance_mm) > motion_config::MAX_SERVICE_MOVE_MM) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "distance_mm must be non-zero and no more than 150 mm");
    auto *web = context(request);
    if (!web || !web->controller) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Controller unavailable");
    const char *reason = nullptr;
    if (!web->controller->enqueue_service_move(distance_mm, &reason)) {
        if (reason && (std::strcmp(reason, "Controller busy") == 0 || std::strcmp(reason, "Command queue full") == 0)) {
            httpd_resp_set_status(request, "503 Service Unavailable");
            return json(request, "{\"error\":\"Controller busy\"}");
        }
        return conflict(request, reason ? reason : "Service move unavailable");
    }
    return json(request, "{\"accepted\":true}");
}

esp_err_t post_service_mm_per_step(httpd_req_t *request) {
    char body[256];
    double mm_per_step = 0.0;
    if (!request_body(request, body, sizeof(body)) || !number(value(body, "mm_per_step"), mm_per_step)) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid mm_per_step");
    StatusSnapshot measurement{};
    WifiStatus wifi{};
    if (!snapshot(request, measurement, wifi)) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Controller unavailable");
    DeviceConfig config = measurement.config;
    config.mm_per_step = mm_per_step;
    const char *reason = nullptr;
    auto *web = context(request);
    if (!web || !web->controller || !web->controller->update_config(config, &reason)) {
        if (reason && std::strcmp(reason, "Settings can be changed only in IDLE") == 0) return conflict(request, reason);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, reason ? reason : "mm_per_step rejected");
    }
    return json(request, "{\"ok\":true,\"calibration_invalidated\":true}");
}

esp_err_t command(httpd_req_t *request) {
    auto *web = context(request);
    if (!web || !web->controller) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Controller unavailable");
    const char *uri = request->uri;
    const CommandType type = std::strstr(uri, "auto/start") ? CommandType::AutoStart : std::strstr(uri, "auto/stop") ? CommandType::AutoStop : std::strstr(uri, "reset-stop") ? CommandType::ResetStop : std::strstr(uri, "reset-error") ? CommandType::ResetError : std::strstr(uri, "/api/stop") ? CommandType::Stop : std::strstr(uri, "measure") ? CommandType::Measure : std::strstr(uri, "calibrate") ? CommandType::Calibrate : CommandType::FactoryReset;
    if (!web->controller->enqueue(type)) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return json(request, "{\"error\":\"Command queue full\"}");
    }
    return json(request, "{\"accepted\":true}");
}
} // namespace

esp_err_t WebServer::start(MeasurementController *controller, WifiManager *wifi) {
    if (!controller || !wifi) return ESP_ERR_INVALID_ARG;
    if (server_) return ESP_ERR_INVALID_STATE;
    context_ = {controller, wifi};
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    httpd_handle_t server = nullptr;
    esp_err_t error = httpd_start(&server, &config);
    if (error != ESP_OK) return error;
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = root, .user_ctx = &context_},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status, .user_ctx = &context_},
        {.uri = "/api/config", .method = HTTP_GET, .handler = get_config, .user_ctx = &context_},
        {.uri = "/api/config", .method = HTTP_POST, .handler = post_config, .user_ctx = &context_},
        {.uri = "/api/service-move", .method = HTTP_POST, .handler = post_service_move, .user_ctx = &context_},
        {.uri = "/api/service/mm-per-step", .method = HTTP_POST, .handler = post_service_mm_per_step, .user_ctx = &context_},
        {.uri = "/api/measure", .method = HTTP_POST, .handler = command, .user_ctx = &context_},
        {.uri = "/api/calibrate", .method = HTTP_POST, .handler = command, .user_ctx = &context_},
        {.uri = "/api/auto/start", .method = HTTP_POST, .handler = command, .user_ctx = &context_},
        {.uri = "/api/auto/stop", .method = HTTP_POST, .handler = command, .user_ctx = &context_},
        {.uri = "/api/stop", .method = HTTP_POST, .handler = command, .user_ctx = &context_},
        {.uri = "/api/reset-stop", .method = HTTP_POST, .handler = command, .user_ctx = &context_},
        {.uri = "/api/reset-error", .method = HTTP_POST, .handler = command, .user_ctx = &context_},
        {.uri = "/api/factory-reset", .method = HTTP_POST, .handler = command, .user_ctx = &context_},
    };
    for (const auto &route : routes) {
        error = httpd_register_uri_handler(server, &route);
        if (error != ESP_OK) { httpd_stop(server); return error; }
    }
    server_ = server;
    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}
