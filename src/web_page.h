#pragma once

// The dashboard, as one document. Kept out of main.cpp because markup in the
// middle of the control code makes the control code harder to read.
//
// HTML, CSS and script together on purpose. Every separate file a browser has
// to fetch is another HTTP request, and this server answers one request per
// handleClient() call - so splitting them would slow the page down and give the
// loop more work.
//
// ⚠️ No web fonts, no CDN, no external anything. The phone is associated to the
// rover's own access point and has no route to the internet, so an external
// stylesheet does not fail slowly - it never arrives at all. System fonts only.
//
// A raw string literal rather than an escaped one: HTML is full of quotes, and
// a backslash before every attribute value would bury the markup.
//
// No PROGMEM. That macro belongs to AVR, where flash and RAM are separate
// address spaces. The ESP32 maps flash into the address space, so a const array
// already lives there and PROGMEM expands to nothing.
//
// The %PLACEHOLDER% tokens are filled in when the page is served, so each of
// those numbers exists once, as a named constant in main.cpp.
const char PAGE_HTML[] = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SafeRover</title>
<style>
/* Committed to one dark look rather than following the phone's theme. This is
   read on a floor, next to a moving vehicle, and a panel that is dark in every
   light is easier to find the important number on. */
:root{
  --bg:#0b0f14; --panel:#141b23; --panel2:#1b242e; --line:#26323e;
  --text:#dce6f0; --dim:#7d8fa3;
  --go:#35d0a5; --warn:#f0b429; --slow:#ff8f3f; --stop:#ff4d4d;
  --mono:ui-monospace,"SF Mono","Cascadia Mono",Menlo,Consolas,monospace;
}
*{box-sizing:border-box}
body{
  margin:0;background:var(--bg);color:var(--text);
  font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
  -webkit-text-size-adjust:100%;
}

/* --- header: name and mode, the two things read from a distance --- */
header{
  display:flex;align-items:center;justify-content:space-between;
  gap:.75rem;padding:.9rem 1rem;border-bottom:1px solid var(--line);
  background:var(--panel);position:sticky;top:0;z-index:2;
}
header h1{margin:0;font-size:1rem;letter-spacing:.14em;text-transform:uppercase;
          color:var(--dim);font-weight:600}
#mode{
  font-family:var(--mono);font-size:1rem;font-weight:700;letter-spacing:.1em;
  padding:.35rem .8rem;border-radius:2rem;border:1px solid currentColor;
  color:var(--dim);
}
body[data-mode="MANUAL"] #mode{color:var(--warn)}
body[data-mode="AUTO"] #mode{color:var(--go)}

/* --- the AEB banner: hidden until the layer actually acts --- */
#aeb{
  display:none;padding:.85rem 1rem;text-align:center;
  font-family:var(--mono);font-weight:700;letter-spacing:.12em;
  font-size:1.05rem;color:#0b0f14;
}
#aeb.on{display:block}
#aeb[data-s="WARN"]{background:var(--warn)}
#aeb[data-s="SLOW"]{background:var(--slow)}
#aeb[data-s="STOP"]{background:var(--stop);animation:flash .5s steps(2,end) infinite}
@keyframes flash{50%{opacity:.45}}
@media (prefers-reduced-motion:reduce){#aeb[data-s="STOP"]{animation:none}}

main{padding:1rem;display:flex;flex-direction:column;gap:1rem}

/* --- distance: the one number worth making large --- */
.hero{
  background:var(--panel);border:1px solid var(--line);border-radius:.75rem;
  padding:1rem;display:flex;align-items:baseline;justify-content:space-between;
}
.hero .k{font-size:.75rem;letter-spacing:.16em;text-transform:uppercase;color:var(--dim)}
.hero .v{font-family:var(--mono);font-size:2.6rem;font-weight:700;
         font-variant-numeric:tabular-nums;line-height:1}
.hero .u{font-size:1rem;color:var(--dim);margin-left:.25rem}

/* --- the pad: found by position, not by reading --- */
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:.6rem;
     max-width:22rem;width:100%;margin:0 auto}
.pad button{
  aspect-ratio:1;font-size:1.7rem;font-family:var(--mono);font-weight:700;
  color:var(--text);background:var(--panel2);border:1px solid var(--line);
  border-radius:.75rem;
  /* Without these a long press selects text or starts a scroll, and the system
     takes the gesture over - firing pointercancel instead of pointerup. */
  touch-action:none;user-select:none;-webkit-user-select:none;
}
.pad button:active{background:#2a3a49;border-color:var(--go)}
#fwd{grid-area:1/2}#left{grid-area:2/1}#stop{grid-area:2/2}
#right{grid-area:2/3}#back{grid-area:3/2}
#stop{font-size:.95rem;letter-spacing:.1em;color:var(--stop);
      border-color:var(--stop);background:transparent}
#note{margin:0;text-align:center;font-size:.8rem;color:var(--dim);min-height:1.1em}

/* --- panels --- */
.card{background:var(--panel);border:1px solid var(--line);
      border-radius:.75rem;padding:.9rem 1rem}
.card h2{margin:0 0 .7rem;font-size:.72rem;letter-spacing:.16em;
         text-transform:uppercase;color:var(--dim);font-weight:600}
.rows{display:grid;grid-template-columns:1fr auto;gap:.4rem .75rem;
      font-size:.95rem}
.rows b{font-family:var(--mono);font-weight:600;text-align:right;
        font-variant-numeric:tabular-nums}
.rows span{color:var(--dim)}

label.sl{display:block;font-size:.8rem;color:var(--dim);margin-bottom:.4rem}
input[type=range]{width:100%;height:2.2rem;accent-color:var(--go)}

.tune{display:flex;flex-wrap:wrap;align-items:flex-end;gap:.7rem}
.tune label{font-size:.75rem;color:var(--dim);display:flex;
            flex-direction:column;gap:.25rem}
.tune input{font-family:var(--mono);font-size:1.05rem;width:5rem;padding:.4rem;
            color:var(--text);background:var(--panel2);
            border:1px solid var(--line);border-radius:.4rem}
button.act{font-family:inherit;font-size:.95rem;padding:.55rem 1.1rem;
           border-radius:.4rem;border:1px solid var(--line);
           background:var(--panel2);color:var(--text)}
button.act:disabled{opacity:.35}
#ts,#cs{font-size:.8rem;color:var(--dim);align-self:center}

/* Dimmed rather than removed: the control still exists and still applies the
   moment the rover is back in a mode that uses it. */
body[data-mode="AUTO"] .manual-only{opacity:.35}

details summary{cursor:pointer;font-size:.72rem;letter-spacing:.16em;
                text-transform:uppercase;color:var(--dim)}
details[open] summary{margin-bottom:.7rem}
</style>
</head>
<body data-mode="STANDBY">

<header>
  <h1>SafeRover</h1>
  <div id="mode">STANDBY</div>
</header>

<div id="aeb"></div>

<main>

<div class="hero">
  <span class="k">distance</span>
  <span><span class="v" id="d">--</span><span class="u" id="du">cm</span></span>
</div>

<div class="pad">
  <button id="fwd">&#94;</button>
  <button id="left">&lt;</button>
  <button id="stop">STOP</button>
  <button id="right">&gt;</button>
  <button id="back">v</button>
</div>
<p id="note"></p>

<div class="card manual-only">
  <label class="sl">manual speed <b id="sv">%START_PERCENT%</b>%</label>
  <input type="range" id="speed"
         min="%MIN_PERCENT%" max="100" value="%START_PERCENT%">
</div>

<div class="card">
  <h2>status</h2>
  <div class="rows">
    <span>AEB stage</span><b id="ae">--</b>
    <span>lane keeping</span><b id="la">--</b>
    <span>calibration</span><b id="cp">--</b>
    <span id="crk" style="display:none">refused because</span>
    <b id="cr" style="display:none"></b>
    <span>zero point</span><b id="zp">--</b>
    <span>error</span><b id="er">--</b>
    <span>line L / R</span><b id="lr">--</b>
  </div>
</div>

<div class="card">
  <h2>lane keeping</h2>
  <div class="tune">
    <label>Kp<input type="number" id="kp" min="0" max="%KP_MAX%" step="1"></label>
    <label>base %<input type="number" id="base" min="0" max="100" step="1"></label>
    <button class="act" id="apply">apply</button>
    <span id="ts"></span>
  </div>
  <div class="tune" style="margin-top:.8rem">
    <button class="act" id="cal">calibrate</button>
    <span id="cs">standby only - measures without driving</span>
  </div>
</div>

<div class="card">
  <details>
    <summary>diagnostics</summary>
    <div class="rows">
      <span>err raw</span><b id="em">--</b>
      <span>gap avg</span><b id="ga">--</b>
      <span>gap max</span><b id="gm">--</b>
      <span>button</span><b id="b">--</b>
    </div>
  </details>
</div>

</main>

<script>
const $ = (id) => document.getElementById(id);
let tuningLoaded = false;

// What the pad does depends on the mode, and the operator has to know that
// before pressing rather than after.
const NOTES = {
  STANDBY: 'a press takes manual control',
  MANUAL:  '',
  AUTO:    'a press disengages lane keeping'
};

async function poll() {
  try {
    const s = await (await fetch('/data')).json();

    // Strict equality, so no reading and a reading of zero can never be taken
    // for one another. A number here would say an obstacle is touching the
    // sensor, which is the worst thing this page could get wrong.
    if (s.distance === null) {
      $('d').textContent = '--';
      $('du').textContent = 'no echo';
    } else {
      $('d').textContent = s.distance.toFixed(1);
      $('du').textContent = 'cm';
    }

    // Drives the CSS rather than being styled here, so the mode has one
    // representation on the page instead of one per element.
    document.body.dataset.mode = s.mode;
    $('mode').textContent = s.mode;
    $('note').textContent = NOTES[s.mode] || '';

    // aebPeak, not aeb. The page polls every %POLL_MS% ms and a braking event
    // lasts about fifty, so the live stage would miss most of them. The rover
    // remembers the worst since this poll last ran and clears it on read.
    const bar = $('aeb');
    if (s.aebPeak && s.aebPeak !== 'CLEAR') {
      bar.dataset.s = s.aebPeak;
      bar.textContent = s.aebPeak === 'STOP' ? 'EMERGENCY STOP'
                      : s.aebPeak === 'SLOW' ? 'BRAKING'
                      : 'OBSTACLE AHEAD';
      bar.classList.add('on');
    } else {
      bar.classList.remove('on');
    }

    // textContent, not innerHTML: values are written as text and never
    // interpreted as markup.
    $('ae').textContent = s.aeb;
    $('la').textContent = s.lkaActive ? 'steering' : 'off';
    $('cp').textContent = s.calPhase;

    // The firmware prints nothing, so this is the only place a refusal says
    // which of the two things went wrong - and they have opposite fixes. Both
    // cells are hidden when there is nothing to explain: the grid has no row
    // element to hide, so the label and the value are hidden as a pair.
    $('cr').textContent = s.calReason;
    const showReason = s.calReason ? '' : 'none';
    $('crk').style.display = showReason;
    $('cr').style.display = showReason;
    $('zp').textContent = s.zeroPoint;
    $('er').textContent = s.lkaError;
    $('lr').textContent = s.lineLeft + ' / ' + s.lineRight;
    $('b').textContent = s.button ? 'pressed' : 'released';

    // The error row above is one sample of a signal moving six times faster
    // than this poll, read after the deadband has flattened it. This is the raw
    // swing since the last poll - the number that says whether the signal
    // underneath is actually quiet.
    $('em').textContent =
      s.errMin === null ? 'idle' : s.errMin + ' .. ' + s.errMax;

    // How old the reading behind that error was. Read here rather than over
    // serial because a cable would drag behind the rover and change the timing
    // it was brought along to measure.
    $('ga').textContent = s.gapAvg + ' ms';
    $('gm').textContent = s.gapMax + ' ms';

    $('cal').disabled = (s.mode !== 'STANDBY');

    // Filled from the rover once, then left alone. Writing them on every poll
    // would overwrite whatever is being typed, twice a second.
    if (!tuningLoaded) {
      $('kp').value = s.kp;
      $('base').value = s.base;
      tuningLoaded = true;
    }
  } catch (e) {
    // One dropped poll is not worth blanking the screen for. The next one is
    // along shortly and will put the numbers back.
  }
}

setInterval(poll, %POLL_MS%);
poll();

// Sends whatever is in the two boxes. The rover refuses anything out of range
// rather than clamping it, so a rejection is shown as one instead of being
// quietly turned into a number nobody asked for.
$('apply').addEventListener('click', async () => {
  const body = 'kp=' + encodeURIComponent($('kp').value) +
               '&base=' + encodeURIComponent($('base').value);
  try {
    const res = await fetch('/tune', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: body
    });
    $('ts').textContent = res.ok ? 'set' : 'rejected';
  } catch (e) {
    $('ts').textContent = 'no reply';
  }
});

// Takes a zero point without driving on it. The rover refuses this outside
// STANDBY; the disabled button is a courtesy, not the check.
$('cal').addEventListener('click', async () => {
  try {
    const res = await fetch('/calibrate', { method: 'POST' });
    $('cs').textContent = res.ok ? 'measuring...' : 'standby only';
  } catch (e) {
    $('cs').textContent = 'no reply';
  }
});


// ---- driving ----

// The whole of the operator's intent, in one variable. Buttons write to it, the
// sender reads it, and the two never call each other. One variable rather than
// a flag per direction, because directions contradict: with four flags there is
// a state where forward and back are both set and something has to decide which
// wins. Here that state cannot be expressed.
let currentDir = 'stop';

const speedEl = $('speed');

// 'input' fires on every movement of the handle. 'change' would only fire when
// it is let go, and the number beside it has to follow the thumb.
speedEl.addEventListener('input', () => { $('sv').textContent = speedEl.value; });

// Reports the current intent. Not "do this once" but "this is what I want now",
// which is why repeats and out-of-order arrivals are harmless.
//
// ⚠️ The manual flag separates the two kinds of message this sends. A press is
// the operator asking for something and carries it; the heartbeat below does
// not. The rover switches to MANUAL on the mark alone and never on the content,
// which is what lets STOP stop an autonomous rover while eight unmarked
// heartbeats a second leave it alone.
async function sendCommand(manual) {
  const params = { dir: currentDir, speed: speedEl.value };
  if (manual) { params.manual = '1'; }

  try {
    await fetch('/drive', { method: 'POST', body: new URLSearchParams(params) });
  } catch (e) {
    // Deliberately empty. A dropped command is not worth reacting to: the next
    // one is along shortly. And if they all stop arriving, it is the rover's
    // own watchdog that stops it - nothing in this page is responsible.
  }
}

// Wires one direction button. Written once rather than four times: four copies
// of the same handler are four chances to leave 'left' where 'right' belongs.
function hold(id, dir) {
  const el = $(id);

  el.addEventListener('pointerdown', () => {
    currentDir = dir;
    sendCommand(true);
  });

  // Three ways a press can end, and only the first is the ordinary one.
  // pointercancel fires when the system takes the gesture over - a scroll, an
  // edge swipe, an incoming call - and pointerleave when the finger slides off
  // the button without lifting. Sending one stop too many costs nothing;
  // missing one leaves the rover driving.
  ['pointerup', 'pointercancel', 'pointerleave'].forEach((ev) => {
    el.addEventListener(ev, () => {
      currentDir = 'stop';
      sendCommand(true);
    });
  });
}

hold('fwd', 'fwd');
hold('back', 'back');
hold('left', 'left');
hold('right', 'right');

// Stop is not a held control. It is pressed, and it means stop.
$('stop').addEventListener('pointerdown', () => {
  currentDir = 'stop';
  sendCommand(true);
});

// The heartbeat, and only that. The presses above are what make the controls
// feel immediate; this exists to keep proving the channel is alive, which is
// what the rover's watchdog is really measuring. It runs whether or not a
// button is down, so a watchdog message means the page genuinely went away
// rather than that somebody let go of a button.
setInterval(() => sendCommand(false), %SEND_MS%);
</script>
</body>
</html>
)rawliteral";
