HEAD = '''<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <script src="./support.js"></script>
</head>
<body>
<x-dc>
<helmet>
  <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Inter:wght@500;700&display=swap">
  <style>
    body { margin: 0; font-family: Inter, "Helvetica Neue", Arial, sans-serif; background: #e8e6e1; }
    a { color: #000; } a:hover { color: #333; }
    .screen { width: 480px; height: 800px; background: #fff; color: #000; position: relative; overflow: hidden; }
    .hdr { position: absolute; top: 5px; left: 0; width: 480px; height: 45px; display: flex; align-items: center; justify-content: center; font-size: 16px; font-weight: 700; }
    .batt { position: absolute; right: 12px; top: 10px; display: flex; align-items: center; gap: 4px; font-size: 13px; font-weight: 500; }
    .status { position: absolute; top: 60px; left: 20px; width: 440px; display: flex; justify-content: space-between; font-size: 14px; font-weight: 700; }
    .rule { position: absolute; left: 20px; width: 440px; height: 1px; background: #000; }
    .body { position: absolute; left: 20px; width: 440px; display: flex; flex-direction: column; gap: 10px; font-size: 15px; font-weight: 500; line-height: 1.45; text-wrap: pretty; }
    .log { position: absolute; left: 20px; width: 440px; border: 1px solid #000; box-sizing: border-box; padding: 8px 10px; display: flex; flex-direction: column; gap: 3px; font-size: 13px; font-weight: 500; }
    .list { position: absolute; left: 0; width: 480px; display: flex; flex-direction: column; }
    .row { height: 30px; display: flex; align-items: center; padding: 0 20px; font-size: 16px; font-weight: 500; }
    .row.sel { background: #000; color: #fff; }
    .hint { position: absolute; bottom: 0; height: 40px; width: 106px; border: 1px solid #000; box-sizing: border-box; display: flex; align-items: center; justify-content: center; font-size: 13px; font-weight: 500; background: #fff; }
    .tag { position: absolute; top: 22px; left: 20px; font-size: 12px; font-weight: 500; }
  </style>
</helmet>
<div style="padding: 24px; width: 528px; height: 852px; box-sizing: border-box; overflow: hidden;">
  <div class="screen" style="border: 2px solid #111">
'''
TAIL = '''  </div>
</div>
</x-dc>
</body>
</html>
'''
BATT = '<div class="batt"><span>82%</span><svg width="15" height="12" viewBox="0 0 15 12"><rect x="0.5" y="0.5" width="12" height="11" fill="none" stroke="#000"></rect><rect x="13" y="3.5" width="2" height="5" fill="#000"></rect><rect x="2" y="2" width="8" height="8" fill="#000"></rect></svg></div>'

def hdr(title, tag=None):
    s = '    <div class="hdr"><span>%s</span>\n' % title
    if tag:
        s += '      <div class="tag">%s</div>\n' % tag
    return s + '      ' + BATT + '\n    </div>\n'

def status(hp, mhp, gold, depth, turn):
    return ('    <div class="status"><span>HP %d/%d</span><span>Gold %d</span><span>Depth %d</span><span>Turn %d</span></div>\n'
            '    <div class="rule" style="top: 84px"></div>\n') % (hp, mhp, gold, depth, turn)

def body(top, paras):
    return '    <div class="body" style="top: %dpx">\n' % top + ''.join('      <div>%s</div>\n' % p for p in paras) + '    </div>\n'

def log(top, lines):
    return '    <div class="log" style="top: %dpx">\n' % top + ''.join('      <div>%s</div>\n' % l for l in lines) + '    </div>\n'

def lst(top, rows, sel):
    s = '    <div class="list" style="top: %dpx">\n' % top
    for i, r in enumerate(rows):
        s += '      <div class="row%s">%s</div>\n' % (' sel' if i == sel else '', r)
    return s + '    </div>\n'

def hints(a, b, c, d):
    return ''.join('    <div class="hint" style="left: %dpx">%s</div>\n' % (x, t) for x, t in zip((25, 130, 245, 350), (a, b, c, d)))

def rule(top):
    return '    <div class="rule" style="top: %dpx"></div>\n' % top

B = lambda t: '<span style="font-weight: 700;">%s</span>' % t
SEED = 'Seed 3F2A91C0'
screens = {}

screens['Main'] = hdr('Rogue') + '''    <div style="position: absolute; top: 120px; left: 20px; width: 440px; display: flex; flex-direction: column; gap: 14px; text-align: center;">
      <div style="font-size: 28px; font-weight: 700; letter-spacing: 0.04em;">THE HOLLOW STAIR</div>
      <div style="font-size: 15px; font-weight: 500; line-height: 1.45;">Every run is a new dungeon. Death is final; what you learn is not.</div>
    </div>
''' + lst(300, ['New run', 'Continue (depth 3, turn 18)', 'Stats'], 0) + '''    <div style="position: absolute; left: 20px; top: 420px; width: 440px; display: flex; flex-direction: column; gap: 6px; font-size: 13px;">
      <div style="height: 1px; background: #000"></div>
      <div style="display: flex; justify-content: space-between"><span>Best depth</span><span>7</span></div>
      <div style="display: flex; justify-content: space-between"><span>Runs</span><span>12 &#183; 1 escaped</span></div>
      <div style="display: flex; justify-content: space-between"><span>Unlocked</span><span>Starting potion, +2 max HP</span></div>
    </div>
''' + hints('Back', 'Select', 'Up', 'Down')

screens['Round1Room'] = hdr('Rogue', SEED) + status(20, 20, 0, 1, 1) + body(100, [
    'The stair ends in a low chamber that smells of wet iron. Torchlight from a bracket on the far wall shows three ways on.',
    'To the left, a rusted grate stands ajar over a shaft; a cold draught comes up it. Ahead, a corridor of cut stone runs straight and dry. To the right, you hear the slow drip of water and, under it, something breathing.',
]) + rule(560) + lst(576, ['Climb down the grate', 'Take the dry corridor', 'Go toward the breathing', 'Rest here a moment'], 2) + hints('Menu', 'Choose', 'Up', 'Down')

monster = 'A cave rat the size of a dog turns from the pool. Its eyes hold the torchlight.'
screens['Round1Combat'] = hdr('Rogue', SEED) + status(20, 20, 0, 1, 2) + body(100, [
    monster, B('Cave Rat') + ' &#8212; HP 6 &#183; ATK 2 &#183; DEF 0',
]) + log(300, ['You draw your knife.', '&nbsp;', '&nbsp;']) + rule(560) + lst(576, ['Attack', 'Defend', 'Item (Potion)', 'Flee'], 0) + hints('Menu', 'Choose', 'Up', 'Down')

screens['Round1Resolve'] = hdr('Rogue', SEED) + status(17, 20, 0, 1, 4) + body(100, [
    monster, B('Cave Rat') + ' &#8212; HP 0 &#183; ATK 2 &#183; DEF 0',
]) + log(300, ['You hit the rat for 4. (6 &#8594; 2)', 'The rat bites you for 3. (20 &#8594; 17)', 'You hit the rat for 2. It dies.']) + body(400, [
    'You wipe the blade on its fur. In the shallows behind it, a leather purse: ' + B('+8 gold') + '.',
]) + rule(560) + lst(576, ['Continue'], 0) + hints('Menu', 'Choose', '', '')

screens['Round2Room'] = hdr('Rogue', SEED) + status(17, 20, 8, 1, 5) + body(100, [
    'Past the pool the passage narrows to a crawl, then opens on a shrine: a squat stone figure, its face worn smooth, a bowl of black water at its feet.',
    'Someone has left three copper coins in the bowl. The air here is very still.',
]) + rule(560) + lst(576, ['Drink from the bowl', 'Take the coins', 'Leave an offering (5 gold)', 'Descend the stair behind the shrine'], 1) + hints('Menu', 'Choose', 'Up', 'Down')

r3 = [
    'The coins burn cold in your hand and the water in the bowl goes still as glass. You feel thinner. ' + B('&#8722;3 HP.'),
    'You take the stair down. ' + B('Depth 2.'),
]
shop = ['Short sword (10 gold)', 'Green vial (6 gold)', 'Ask what he is doing down here', 'Walk on']
screens['Round3Room'] = hdr('Rogue', SEED) + status(14, 20, 11, 2, 7) + body(100, r3 + [
    'A long hall of pillars, half of them fallen. Between two of them a merchant has set a cloth, and on it, a lamp, a short sword, and a vial that glows faintly green. He does not look up.',
]) + rule(560) + lst(576, shop, 0) + hints('Menu', 'Choose', 'Up', 'Down')

screens['Pause'] = hdr('Rogue', SEED) + status(14, 20, 11, 2, 7) + body(100, r3) + '''    <div style="position: absolute; top: 300px; left: 80px; width: 320px; background: #fff; border: 2px solid #000; display: flex; flex-direction: column; padding: 14px 0;">
      <div style="font-size: 16px; font-weight: 700; text-align: center; padding: 0 0 10px;">Paused</div>
      <div class="row sel" style="height: 34px;">Resume</div>
      <div class="row" style="height: 34px;">Abandon run</div>
    </div>
''' + rule(560) + lst(576, shop, -1) + hints('Menu', 'Choose', 'Up', 'Down')

for k, v in screens.items():
    open('%s.dc.html' % k, 'w', encoding='utf-8').write(HEAD + v + TAIL)
print(sorted(screens))
