"""Write docs/SETTINGS.md from the settings schema, so the web UI can stay short and link here.

Usage: python scripts/docs/settings_reference.py <schema.json> <out.md>
Get the schema from a running host: GET /api/v1/config/schema?locale=en (signed in).
"""
import json
import sys

schema = json.load(open(sys.argv[1], encoding='utf-8'))
cats = schema['categories']
sections = {s['id']: s for s in schema.get('sections', [])}
settings = [s for s in schema['settings']]


def value_text(s, v):
    titles = s.get('value_titles', {})
    key = str(v).lower() if isinstance(v, bool) else str(v)
    if key in titles:
        return titles[key].replace(' (default)', '')
    if s['type'] == 'boolean':
        return 'On' if v else 'Off'
    if v in ('', None, [], {}):
        return 'empty' if s['type'] in ('string', 'path') else 'none'
    if isinstance(v, (list, dict)):
        return 'none' if not v else 'see the web UI'
    unit = {'kbps': ' kbps', 'ms': ' ms', 'percent': '%', 'fps': ' fps', 'per_second': ' per second'}.get(s.get('unit'), '')
    return f'{v}{unit}'


def platforms(s):
    p = s.get('platforms', [])
    if len(p) >= 3:
        return ''
    return ', '.join(x.capitalize().replace('Macos', 'macOS') for x in p) + ' only'


out = ['# Settings',
       '',
       'Every setting in the web UI (**Settings**) and in Norilight\'s **Host Settings**, by page.',
       'In the web UI, hover a setting\'s name for its explanation; search covers everything.',
       '**Advanced** settings only show with *Show advanced settings* on. The name in',
       '`sunshine.conf` is given for people who edit the file directly.',
       '',
       'Generated from the host\'s settings schema by `scripts/docs/settings_reference.py`.',
       '']
groups_seen = set()
out.append('Pages: ' + ' · '.join(f'[{c["title"]}](#{c["id"]})' for c in cats))
out.append('')
for c in cats:
    rows = [s for s in settings if s['category'] == c['id']]
    if not rows:
        continue
    if c.get('group') and c['group'] not in groups_seen:
        groups_seen.add(c['group'])
        out += [f'## {c["group_title"]}', '',
                'Only the page for the encoder your PC uses is listed in the web UI; the rest are behind *Show the other encoders*.', '']
    level = '###' if c.get('group') else '##'
    out += [f'<a id="{c["id"]}"></a>', '', f'{level} {c["title"]}', '']
    if c.get('description'):
        out += [c['description'], '']
    # As in the web UI: settings without a section first, then each section in order.
    defs = [d for d in schema.get('sections', []) if d['category'] == c['id']]
    groups = [(None, [s for s in rows if s.get('section') not in sections])]
    groups += [(d, [s for s in rows if s.get('section') == d['id']]) for d in defs]
    ordered = []
    for d, members in groups:
        if members:
            ordered.append(('head', d))
            ordered += [('row', s) for s in members]
    for kind, s in ordered:
        if kind == 'head':
            if s:
                if out[-1] != '':
                    out.append('')
                note = s.get('note')
                out += [f'**{s["title"]}**' + (f' ({note[0].lower() + note[1:]})' if note else ''), '']
            continue
        meta = [f'`{s["key"]}`']
        if s['type'] not in ('prep_commands', 'server_commands', 'keybindings', 'mode_remapping'):
            meta.append('default: ' + value_text(s, s['default']))
        if s['tier'] == 'advanced':
            meta.append('advanced')
        elif s['tier'] == 'dangerous':
            meta.append('asks before saving')
        if platforms(s):
            meta.append(platforms(s))
        out.append(f'- **{s["title"]}**: {s.get("help") or ""}'.rstrip())
        out.append('  <br>' + ' · '.join(meta))
    out.append('')

open(sys.argv[2], 'w', encoding='utf-8', newline='\n').write('\n'.join(out).rstrip() + '\n')
print('wrote', sys.argv[2], len(settings), 'settings')
