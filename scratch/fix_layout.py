import re

with open('index.html', 'r', encoding='utf-8') as f:
    html = f.read()

# Fix the LIVE indicators in HTML
html = html.replace('>LIVE <span class="signal" style="margin-left:5px;">?</span><', '>LIVE <span class="signal" style="margin-left:5px;">&#9679;</span><')

# Fix the LIVE indicators in JS
html = html.replace(
    'innerHTML = "LIVE <span class=\\"signal\\" style=\\"margin-left:5px;\\">?</span>"',
    'innerHTML = "LIVE <span class=\\"signal\\" style=\\"margin-left:5px;\\">&#9679;</span>"'
)

# Fix dashboard columns to be 4 equal columns instead of 3 + 1 smaller
html = html.replace(
    '#page-dashboard-utama { grid-template-columns: repeat(3, minmax(0, 1fr)) minmax(220px, 0.9fr); }',
    '#page-dashboard-utama { grid-template-columns: repeat(4, minmax(0, 1fr)); }'
)

# Fix Headings for better fitting (pH SENSOR, TSS SENSOR, WATER LEVEL)
html = html.replace(
    '<div class="panel-heading" style="padding-bottom: 5px; border-bottom: 2px solid var(--border-dark); font-size: 22px;"><span>RINGKASAN pH</span></div>',
    '<div class="panel-heading" style="padding-bottom: 5px; border-bottom: 2px solid var(--border-dark); font-size: 18px; justify-content: center;"><span>pH SENSOR</span></div>'
)
html = html.replace(
    '<div class="panel-heading" style="padding-bottom: 5px; border-bottom: 2px solid var(--border-dark); font-size: 22px;"><span>RINGKASAN TSS</span></div>',
    '<div class="panel-heading" style="padding-bottom: 5px; border-bottom: 2px solid var(--border-dark); font-size: 18px; justify-content: center;"><span>TSS SENSOR</span></div>'
)
html = html.replace(
    '<div class="panel-heading" style="padding-bottom: 5px; border-bottom: 2px solid var(--border-dark); font-size: 22px;"><span>RINGKASAN WATER LEVEL</span></div>',
    '<div class="panel-heading" style="padding-bottom: 5px; border-bottom: 2px solid var(--border-dark); font-size: 18px; justify-content: center;"><span>WATER LEVEL</span></div>'
)
html = html.replace(
    '<div class="panel-heading" style="justify-content: space-between; padding-bottom: 5px; border-bottom: 2px solid var(--border-dark); font-size: 22px;"><span>SESSION OVERVIEW</span>',
    '<div class="panel-heading" style="justify-content: space-between; padding-bottom: 5px; border-bottom: 2px solid var(--border-dark); font-size: 18px;"><span>SESSION OVERVIEW</span>'
)

# Fix inner spacing (CURRENT pH, etc) to match
html = html.replace(
    '<div style="text-align: center; color: var(--mint); font-size: 18px; margin: 15px 0;">',
    '<div style="text-align: center; color: var(--mint); font-size: 15px; margin: 10px 0;">'
)

# Resize large reading numbers so they don't break flex on small screens
html = html.replace('font-size:45px;', 'font-size:38px;')

# Resize icons in the 4 top boxes from 70px to 55px
html = html.replace('style="width:70px; height:70px;"', 'style="width:55px; height:55px;"')

# Adjust Calibration layout spacing
html = html.replace(
    '<div style="display: grid; grid-template-columns: repeat(3, 1fr); gap: 20px; margin-top: 15px; padding-bottom: 15px; border-bottom: 2px solid var(--border-dark);">',
    '<div style="display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; margin-top: 15px; padding-bottom: 15px; border-bottom: 2px solid var(--border-dark);">'
)

with open('index.html', 'w', encoding='utf-8') as f:
    f.write(html)
