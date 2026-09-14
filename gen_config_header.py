import json, sys

with open('./config/config.json', 'r', encoding='utf-8') as f:
    text = f.read()

with open('./config_json.h', 'w', encoding='utf-8') as f:
    f.write('#pragma once\n')
    f.write('static const char CONFIG_JSON[] =\n')
    # 转义处理
    escaped = text.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n"\n"')
    f.write('"' + escaped + '";\n')
