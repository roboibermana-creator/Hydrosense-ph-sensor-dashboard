import re

with open('index.html', 'r', encoding='utf-8') as f:
    html = f.read()

# Header SVGs
html = html.replace('gambarpixel/logo_h.png', 'gambarpixel/logo-hydrosense.svg')
html = html.replace('gambarpixel/operator_avatar.png', 'gambarpixel/avatar-operator.svg')
html = html.replace('gambarpixel/calibrate_probe.png', 'gambarpixel/calibration-tools.svg')
html = html.replace('gambarpixel/tools_settings.png', 'gambarpixel/calibration-tools.svg')

# Main Readings
html = html.replace('gambarpixel/droplet_ph.png', 'gambarpixel/ph-drop-large.svg')
html = html.replace('gambarpixel/tss_particles.png', 'gambarpixel/tss-cluster-large.svg')
html = html.replace('gambarpixel/water_tank.png', 'gambarpixel/level-main-tank.svg')

# Sub-readings
html = html.replace('gambarpixel/probe_voltage_bolt.png', 'gambarpixel/parameter-voltage-petir.svg')
html = html.replace('gambarpixel/sensor_turbidity.png', 'gambarpixel/tss-unit-symbol.svg')
html = html.replace('gambarpixel/echo_distance.png', 'gambarpixel/level-wave-status.svg')

# Headings icons
html = html.replace('gambarpixel/book_session.png', 'gambarpixel/ikon-statistik-sesi.svg')
html = html.replace('gambarpixel/gear_settings.png', 'gambarpixel/ikon-pengaturan-kecil.svg')

# Calibration statuses
html = html.replace('gambarpixel/calib_droplet_small.png', 'gambarpixel/ph-drop-small.svg')
html = html.replace('gambarpixel/calib_particles_small.png', 'gambarpixel/tss-cluster-small.svg')
html = html.replace('gambarpixel/calib_tank_small.png', 'gambarpixel/level-calibration.svg')

# Wi-Fi
html = html.replace('gambarpixel/wifi-icon.svg', 'gambarpixel/header-wifi.svg')
html = html.replace('gambarpixel/wifi_disconnected.png', 'gambarpixel/ikon-status-wifi-terputus.svg')

# Replace the date-block and wifi-status contents to include the new header icons
date_block_orig = '<div class="date-block"><span id="dateNow">DATE // --.--.----</span><strong id="timeNow">--:--:-- WIB</strong></div>'
date_block_new = '<div class="date-block" style="display:flex;align-items:center;"><img src="gambarpixel/header-calendar.svg" class="pixel-icon" style="width:16px;height:16px;margin-right:5px;"><span id="dateNow">DATE // --.--.----</span><img src="gambarpixel/header-clock.svg" class="pixel-icon" style="width:16px;height:16px;margin:0 5px 0 15px;"><strong id="timeNow">--:--:-- WIB</strong></div>'
html = html.replace(date_block_orig, date_block_new)

wifi_block_orig = '<div class="wifi-status"><img class="pixel-icon wifi-icon is-connecting" id="wifiIcon" src="gambarpixel/header-wifi.svg" alt="Wi-Fi connection status" /><span id="connectionStatus">CONNECTING</span></div>'
wifi_block_new = '<div class="wifi-status" style="display:flex;align-items:center;"><img src="gambarpixel/header-signal.svg" class="pixel-icon" style="width:16px;height:16px;margin-right:5px;"><img class="pixel-icon wifi-icon is-connecting" id="wifiIcon" src="gambarpixel/header-wifi.svg" alt="Wi-Fi connection status" /><span id="connectionStatus">CONNECTING</span></div>'
html = html.replace(wifi_block_orig, wifi_block_new)

score_block_orig = '<div class="score-box"><span>READINGS</span><strong id="readingCount">000000</strong></div>'
score_block_new = '<div class="score-box" style="display:flex;align-items:center;"><img src="gambarpixel/header-readings.svg" class="pixel-icon" style="width:16px;height:16px;margin-right:5px;"><span>READINGS</span><strong id="readingCount" style="margin-left:5px;">000000</strong></div>'
html = html.replace(score_block_orig, score_block_new)

with open('index.html', 'w', encoding='utf-8') as f:
    f.write(html)
