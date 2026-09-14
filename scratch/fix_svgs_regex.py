import re

with open('index.html', 'r', encoding='utf-8') as f:
    html = f.read()

# 1. Header (already done in swap_svgs.py, but just verify Logo)
html = html.replace('<span class="brand-mark">H</span>', '<img class="brand-mark" src="gambarpixel/logo-hydrosense.svg" alt="H" style="width:35px;height:35px;object-fit:contain;"/>')

# 2. Sidebar
html = re.sub(r'<img[^>]+src="gambarpixel/user-avatar\.svg"[^>]*>', '<img class="pixel-icon avatar" src="gambarpixel/avatar-operator.svg" alt="User avatar" />', html)
html = re.sub(r'<img[^>]+src="gambarpixel/calibration-tool\.svg"[^>]*>', '<img class="pixel-icon" src="gambarpixel/calibration-tools.svg" alt="" />', html)
html = re.sub(r'<img[^>]+src="gambarpixel/ph-probe-gear\.svg"[^>]*>', '<img class="pixel-icon" src="gambarpixel/calibration-tools.svg" alt="" />', html)

# Navigation
html = html.replace('<img class="pixel-icon" src="gambarpixel/parameter-ph-tetesan.svg" alt="" />pH SENSOR', '<img class="pixel-icon" src="gambarpixel/ph-drop-small.svg" alt="" />pH SENSOR')
html = html.replace('<img class="pixel-icon" src="gambarpixel/tss-cluster-large.svg" alt="" />TSS SENSOR', '<img class="pixel-icon" src="gambarpixel/tss-cluster-small.svg" alt="" />TSS SENSOR')
html = html.replace('<img class="pixel-icon" src="gambarpixel/level-main-tank.svg" alt="" />WATER LEVEL', '<img class="pixel-icon" src="gambarpixel/level-wave-status.svg" alt="" />WATER LEVEL')

# 3. Dashboard Utama specific
# pH
html = html.replace('<img class="pixel-icon reading-icon" src="gambarpixel/parameter-ph-tetesan.svg" alt="pH droplet" style="width:55px; height:55px;" />', '<img class="pixel-icon reading-icon" src="gambarpixel/ph-drop-large.svg" alt="pH droplet" style="width:55px; height:55px;" />')
# TSS already large, WL already large tank

# 4. Calibration Wide Panel in Dashboard Utama
html = html.replace('<img class="pixel-icon" src="gambarpixel/parameter-ph-tetesan.svg" style="width: 50px; height: 50px;" />', '<img class="pixel-icon" src="gambarpixel/ph-drop-small.svg" style="width: 50px; height: 50px;" />')
html = html.replace('<img class="pixel-icon" src="gambarpixel/tss-cluster-large.svg" style="width: 50px; height: 50px;" />', '<img class="pixel-icon" src="gambarpixel/tss-cluster-small.svg" style="width: 50px; height: 50px;" />')
html = html.replace('<img class="pixel-icon" src="gambarpixel/level-main-tank.svg" style="width: 50px; height: 50px;" />', '<img class="pixel-icon" src="gambarpixel/level-calibration.svg" style="width: 50px; height: 50px;" />')

# 5. pH Sensor Page
html = html.replace('<div class="cal-status"><img class="pixel-icon" src="gambarpixel/ph-drop-large.svg" alt="" /><div><b id="calibrationTitle">PROBE CALIBRATED</b>', '<div class="cal-status"><img class="pixel-icon" src="gambarpixel/ph-drop-small.svg" alt="" /><div><b id="calibrationTitle">PROBE CALIBRATED</b>')

# 6. TSS Sensor Page
html = html.replace('<img class="pixel-icon reading-icon" src="gambarpixel/tss-cluster-large.svg" alt="NTU" />', '<img class="pixel-icon reading-icon" src="gambarpixel/tss-unit-symbol.svg" alt="NTU" />')
html = html.replace('<div class="cal-status"><img class="pixel-icon" src="gambarpixel/tss-cluster-large.svg" alt="" /><div><b>SENSOR CALIBRATED</b>', '<div class="cal-status"><img class="pixel-icon" src="gambarpixel/tss-cluster-small.svg" alt="" /><div><b>SENSOR CALIBRATED</b>')

# 7. Water Level Page
html = html.replace('<img class="pixel-icon reading-icon" src="gambarpixel/parameter-voltage-petir.svg" alt="Distance" />', '<img class="pixel-icon reading-icon" src="gambarpixel/level-wave-status.svg" alt="Distance" />')
html = html.replace('<div class="panel-heading"><span>SESSION STATS</span><img class="pixel-icon mini-icon" src="gambarpixel/ikon-statistik-sesi.svg" alt="" /></div>\n              <dl class="stats-list"><div><dt>LAST UPDATE</dt><dd class="lastUpdateGeneric">--:--:--</dd></div><div><dt>AVG. LEVEL</dt><dd id="avgWl">--', '<div class="panel-heading"><span>SESSION STATS</span><img class="pixel-icon mini-icon" src="gambarpixel/level-stats.svg" alt="" /></div>\n              <dl class="stats-list"><div><dt>LAST UPDATE</dt><dd class="lastUpdateGeneric">--:--:--</dd></div><div><dt>AVG. LEVEL</dt><dd id="avgWl">--')
html = html.replace('<div class="cal-status"><img class="pixel-icon" src="gambarpixel/level-main-tank.svg" alt="" /><div><b>SENSOR CALIBRATED</b>', '<div class="cal-status"><img class="pixel-icon" src="gambarpixel/level-calibration.svg" alt="" /><div><b>SENSOR CALIBRATED</b>')


with open('index.html', 'w', encoding='utf-8') as f:
    f.write(html)
