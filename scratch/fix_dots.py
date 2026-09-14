import re

with open('index.html', 'r', encoding='utf-8') as f:
    html = f.read()

# Replace any garbled signal spans with 3 clean dots
html = re.sub(r'<span class="signal">[^<]+</span>', '<span class="signal">&#9679; &#9679; &#9679;</span>', html)
html = re.sub(r'<span class="signal amber">[^<]+</span>', '<span class="signal amber">&#9679; &#9679; &#9679;</span>', html)

with open('index.html', 'w', encoding='utf-8') as f:
    f.write(html)
