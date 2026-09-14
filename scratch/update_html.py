import re

with open('index.html', 'r', encoding='utf-8') as f:
    content = f.read()

# Define the new Dashboard Utama HTML
new_dashboard_utama = '''        <main class="dashboard-grid page-content active" id="page-dashboard-utama" style="grid-template-columns: repeat(3, minmax(0, 1fr)) minmax(220px, 0.9fr); grid-template-rows: auto 1fr;">
          <!-- RINGKASAN pH -->
          <section class="data-panel ph-panel" style="padding: 16px; min-height: 250px;">
            <div class="panel-heading" style="padding-bottom: 5px; border-bottom: 2px solid var(--border-dark); font-size: 22px;"><span>RINGKASAN pH</span></div>
            <div style="text-align: center; color: var(--mint); font-size: 18px; margin: 15px 0;">CURRENT pH</div>
            <div class="reading" style="min-height: auto; margin-bottom: 20px;">
              <img class="pixel-icon reading-icon" src="gambarpixel/parameter-ph-tetesan.svg" onerror="this.src='gambarpixel/ph-droplet.svg'" alt="pH droplet" style="width:70px; height:70px;" />
              <div><strong id="phNowSummary" style="font-size:45px;">6.85</strong><span style="font-size:16px;">pH LEVEL</span></div>
            </div>
            <div class="panel-footer" style="justify-content: space-between;"><span>STATUS:</span><b id="phStatusSummaryText" style="color:var(--neon); margin-left: auto;">LIVE <span class="signal" style="margin-left:5px;">?</span></b></div>
            <div class="panel-footer" style="border-top: none; padding-top: 5px; justify-content: space-between;"><span>PROBE VOLTAGE:</span><b id="voltNowSummary" style="color:var(--orange); font-weight:normal; margin-left: auto;">1385.55 mV</b></div>
          </section>

          <!-- RINGKASAN TSS -->
          <section class="data-panel voltage-panel" style="padding: 16px; min-height: 250px;">
            <div class="panel-heading" style="padding-bottom: 5px; border-bottom: 2px solid var(--border-dark); font-size: 22px;"><span>RINGKASAN TSS</span></div>
            <div style="text-align: center; color: var(--mint); font-size: 18px; margin: 15px 0;">CURRENT TSS</div>
            <div class="reading" style="min-height: auto; margin-bottom: 20px;">
              <img class="pixel-icon reading-icon" src="gambarpixel/parameter-tss-mgl.svg" onerror="this.src='gambarpixel/voltage-symbol.svg'" alt="TSS" style="width:70px; height:70px;" />
              <div><strong id="tssNowSummary" style="font-size:45px; color:var(--orange);">150</strong><span style="font-size:16px;">TSS mg/L</span></div>
            </div>
            <div class="panel-footer" style="justify-content: space-between;"><span>STATUS:</span><b id="tssStatusSummaryText" style="color:var(--neon); margin-left: auto;">LIVE <span class="signal" style="margin-left:5px;">?</span></b></div>
            <div class="panel-footer" style="border-top: none; padding-top: 5px; justify-content: space-between;"><span>TURBIDITY:</span><b id="ntuNowSummary" style="color:var(--orange); font-weight:normal; margin-left: auto;">50 NTU</b></div>
          </section>

          <!-- RINGKASAN WATER LEVEL -->
          <section class="data-panel ph-panel" style="padding: 16px; min-height: 250px;">
            <div class="panel-heading" style="padding-bottom: 5px; border-bottom: 2px solid var(--border-dark); font-size: 22px;"><span>RINGKASAN WATER LEVEL</span></div>
            <div style="text-align: center; color: var(--mint); font-size: 18px; margin: 15px 0;">CURRENT LEVEL</div>
            <div class="reading" style="min-height: auto; margin-bottom: 20px;">
              <img class="pixel-icon reading-icon" src="gambarpixel/parameter-water-level.svg" onerror="this.src='gambarpixel/ph-droplet.svg'" alt="Water Level" style="width:70px; height:70px;" />
              <div><strong id="wlNowSummary" style="font-size:45px; color:var(--orange);">78%</strong><span style="font-size:16px;">% LEVEL</span></div>
            </div>
            <div class="panel-footer" style="justify-content: space-between;"><span>STATUS:</span><b id="wlStatusSummaryText" style="color:var(--neon); margin-left: auto;">LIVE <span class="signal" style="margin-left:5px;">?</span></b></div>
            <div class="panel-footer" style="border-top: none; padding-top: 5px; justify-content: space-between;"><span>ECHO DISTANCE:</span><b id="distNowSummary" style="color:var(--orange); font-weight:normal; margin-left: auto;">92 cm</b></div>
          </section>

          <!-- SESSION OVERVIEW -->
          <section class="data-panel stats-panel" style="padding: 16px; min-height: 250px;">
            <div class="panel-heading" style="justify-content: space-between; padding-bottom: 5px; border-bottom: 2px solid var(--border-dark); font-size: 22px;"><span>SESSION OVERVIEW</span><img class="pixel-icon mini-icon" src="gambarpixel/log-book-2.svg" alt="" style="width:24px;height:24px;" /></div>
            <dl class="stats-list" style="grid-template-columns: 1fr 1fr; margin: 20px 0 15px; font-size: 16px; gap: 15px 5px;">
              <div><dt>AVG pH</dt><dd id="avgPhSummary" style="font-size: 22px;">6.73</dd></div>
              <div><dt>READINGS</dt><dd id="sessionReadingsSummary" style="font-size: 22px;">50</dd></div>
              <div><dt>AVG TSS</dt><dd id="avgTssSummary" style="font-size: 22px;">151 <span style="font-size:14px;color:var(--mint);">mg/L</span></dd></div>
              <div><dt>READINGS</dt><dd id="sessionReadingsTssSummary" style="font-size: 22px;">50</dd></div>
              <div><dt>AVG LEVEL</dt><dd id="avgWlSummary" style="font-size: 22px;">76%</dd></div>
              <div><dt>UPTIME</dt><dd class="good" style="font-size: 22px;">99.8%</dd></div>
            </dl>
            <p class="terminal-note" style="margin-top: 20px;">&gt; SESSION OVERVIEW</p>
          </section>

          <!-- CALIBRATION STATUS WIDE PANEL -->
          <section class="data-panel calibration-panel" id="calibration-summary" style="grid-column: 1 / -1; padding: 16px; margin-top: 5px;">
            <div class="panel-heading" style="justify-content: space-between; padding-bottom: 5px; border-bottom: 2px solid var(--border-dark);"><span>CALIBRATION STATUS</span><img class="pixel-icon mini-icon" src="gambarpixel/settings-gear.svg" alt="" style="width:24px;height:24px;" /></div>
            
            <div style="display: grid; grid-template-columns: repeat(3, 1fr); gap: 20px; margin-top: 15px; padding-bottom: 15px; border-bottom: 2px solid var(--border-dark);">
              <!-- pH Cal -->
              <div style="display: flex; gap: 15px; align-items: center;">
                 <img class="pixel-icon" src="gambarpixel/parameter-ph-tetesan.svg" onerror="this.src='gambarpixel/ph-droplet.svg'" style="width: 50px; height: 50px;" />
                 <div class="calibration-rows" style="border: none; flex-grow: 1; margin: 0;">
                   <div style="border: none; padding: 2px 0; font-size: 16px;"><span>pH: 4.01</span><b id="cal4Sum" style="color:var(--neon);">COMPLETE</b></div>
                   <div style="border: none; padding: 2px 0; font-size: 16px;"><span>6.86</span><b id="cal7Sum" style="color:var(--neon);">COMPLETE</b></div>
                   <div style="border: none; padding: 2px 0; font-size: 16px;"><span>9.18</span><em id="cal10Sum" style="color:var(--orange);">PENDING</em></div>
                 </div>
              </div>

              <!-- TSS Cal -->
              <div style="display: flex; gap: 15px; align-items: center; border-left: 2px solid var(--border-dark); padding-left: 15px;">
                 <img class="pixel-icon" src="gambarpixel/parameter-tss-mgl.svg" onerror="this.src='gambarpixel/history-icon.svg'" style="width: 50px; height: 50px;" />
                 <div class="calibration-rows" style="border: none; flex-grow: 1; margin: 0;">
                   <div style="border: none; padding: 2px 0; font-size: 16px;"><span>Std A</span><b id="calTssASum" style="color:var(--neon);">COMPLETE</b></div>
                   <div style="border: none; padding: 2px 0; font-size: 16px;"><span>Std B</span><em id="calTssBSum" style="color:var(--orange);">PENDING</em></div>
                   <div style="border: none; padding: 2px 0; font-size: 16px;"><span>Std C</span><em id="calTssCSum" style="color:var(--orange);">PENDING</em></div>
                 </div>
              </div>

              <!-- WL Cal -->
              <div style="display: flex; gap: 15px; align-items: center; border-left: 2px solid var(--border-dark); padding-left: 15px;">
                 <img class="pixel-icon" src="gambarpixel/parameter-water-level.svg" onerror="this.src='gambarpixel/settings-gear.svg'" style="width: 50px; height: 50px;" />
                 <div class="calibration-rows" style="border: none; flex-grow: 1; margin: 0;">
                   <div style="border: none; padding: 2px 0; font-size: 16px;"><span>High SP</span><em id="calWlHSum" style="color:var(--orange);">PENDING</em></div>
                   <div style="border: none; padding: 2px 0; font-size: 16px;"><span>Low SP</span><em id="calWlLSum" style="color:var(--orange);">PENDING</em></div>
                   <div style="border: none; padding: 2px 0; font-size: 16px;"><span>Tank Hgt</span><b id="calWlTSum" style="color:var(--neon);">COMPLETE</b></div>
                 </div>
              </div>
            </div>

            <div class="warning" style="margin-top: 15px;">! CALIBRATION DATA LOADED</div>
          </section>
        </main>'''

pattern = r'<!-- PAGE 1: DASHBOARD UTAMA -->(.*?)<!-- PAGE 2: pH SENSOR -->'
new_content = re.sub(pattern, "<!-- PAGE 1: DASHBOARD UTAMA -->\n" + new_dashboard_utama + "\n        \n        <!-- PAGE 2: pH SENSOR -->", content, flags=re.DOTALL)

# Add js updates
new_content = new_content.replace(
    'if (voltage !== null) { document.querySelector("#voltNow").textContent = voltage.toFixed(2); }',
    'if (voltage !== null) { document.querySelector("#voltNow").textContent = voltage.toFixed(2); document.querySelector("#voltNowSummary").textContent = voltage.toFixed(2) + " mV"; }'
)
new_content = new_content.replace(
    'document.querySelector("#ntuNow").textContent = ntu.toFixed(1);',
    'document.querySelector("#ntuNow").textContent = ntu.toFixed(1);\n        document.querySelector("#ntuNowSummary").textContent = ntu.toFixed(1) + " NTU";'
)
new_content = new_content.replace(
    'document.querySelector("#distNow").textContent = dist.toFixed(0);',
    'document.querySelector("#distNow").textContent = dist.toFixed(0);\n        document.querySelector("#distNowSummary").textContent = dist.toFixed(0) + " cm";'
)
new_content = new_content.replace(
    'document.querySelector("#phStatusSummary").textContent = st;',
    '// document.querySelector("#phStatusSummary").textContent = st;'
)
new_content = new_content.replace(
    'document.querySelector("#tssStatusSummary").textContent = tssSt;',
    '// document.querySelector("#tssStatusSummary").textContent = tssSt;'
)
new_content = new_content.replace(
    'document.querySelector("#wlStatusSummary").textContent = wlSt;',
    '// document.querySelector("#wlStatusSummary").textContent = wlSt;'
)
new_content = new_content.replace(
    'document.querySelector("#sessionReadings").textContent = entries.length;',
    'document.querySelector("#sessionReadings").textContent = entries.length;\n          document.querySelector("#sessionReadingsSummary").textContent = entries.length;\n          document.querySelector("#sessionReadingsTssSummary").textContent = entries.length;'
)

avgPhLine = 'document.querySelector("#avgPh").textContent = phEntries.length ? (phEntries.reduce((total, entry) => total + Number(entry.ph), 0) / phEntries.length).toFixed(2) : "--";'
new_content = new_content.replace(avgPhLine, avgPhLine + '\n          document.querySelector("#avgPhSummary").textContent = document.querySelector("#avgPh").textContent;')

avgTssLine = 'document.querySelector("#avgTss").textContent = tssEntries.length ? (tssEntries.reduce((total, entry) => total + Number(entry.tss), 0) / tssEntries.length).toFixed(1) : "0";'
new_content = new_content.replace(avgTssLine, avgTssLine + '\n          document.querySelector("#avgTssSummary").textContent = document.querySelector("#avgTss").textContent + " mg/L";')

avgWlLine = 'document.querySelector("#avgWl").textContent = wlEntries.length ? (wlEntries.reduce((total, entry) => total + Number(entry.wl), 0) / wlEntries.length).toFixed(0) + "%" : "0%";'
new_content = new_content.replace(avgWlLine, avgWlLine + '\n          document.querySelector("#avgWlSummary").textContent = document.querySelector("#avgWl").textContent;')

# Handle offline dashes
new_content = new_content.replace(
    'els.forEach(selector => { const el = document.querySelector(selector); if(el) el.textContent = "-"; });',
    'els.forEach(selector => { const el = document.querySelector(selector); if(el) el.textContent = "-"; }); document.querySelector("#phStatusSummaryText").innerHTML = "-"; document.querySelector("#tssStatusSummaryText").innerHTML = "-"; document.querySelector("#wlStatusSummaryText").innerHTML = "-"; document.querySelector("#voltNowSummary").textContent = "-"; document.querySelector("#ntuNowSummary").textContent = "-"; document.querySelector("#distNowSummary").textContent = "-";'
)

# Handle connected status LIVE dot
new_content = new_content.replace(
    'document.querySelector("#phNowSummary").textContent = txt; document.querySelector("#phStatusSummary").textContent = st;',
    'document.querySelector("#phNowSummary").textContent = txt; document.querySelector("#phStatusSummaryText").innerHTML = "LIVE <span class=\\"signal\\" style=\\"margin-left:5px;\\">?</span>";'
)
new_content = new_content.replace(
    'document.querySelector("#tssStatusSummary").textContent = tssSt;',
    'document.querySelector("#tssStatusSummaryText").innerHTML = "LIVE <span class=\\"signal\\" style=\\"margin-left:5px;\\">?</span>";'
)
new_content = new_content.replace(
    'document.querySelector("#wlStatusSummary").textContent = wlSt;',
    'document.querySelector("#wlStatusSummaryText").innerHTML = "LIVE <span class=\\"signal\\" style=\\"margin-left:5px;\\">?</span>";'
)

with open('index.html', 'w', encoding='utf-8') as f:
    f.write(new_content)

print("Done writing to index.html")
