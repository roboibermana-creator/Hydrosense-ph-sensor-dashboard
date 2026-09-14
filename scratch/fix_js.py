import re

with open('index.html', 'r', encoding='utf-8') as f:
    html = f.read()

html = html.replace(
    '// document.querySelector("#tssStatusSummaryText").innerHTML',
    'document.querySelector("#tssStatusSummaryText").innerHTML'
)
html = html.replace(
    '// document.querySelector("#wlStatusSummaryText").innerHTML',
    'document.querySelector("#wlStatusSummaryText").innerHTML'
)

with open('index.html', 'w', encoding='utf-8') as f:
    f.write(html)
