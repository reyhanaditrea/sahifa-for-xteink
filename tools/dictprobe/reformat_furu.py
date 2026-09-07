"""Re-render ar-en-al-furu' entries as one house format:

    كَتَبَ · كَتَّبَ
    I  كَتَبَ · يَكْتُبُ · كَتْبٌ / كِتَابٌ
    • collocation — gloss [n]
    II ...
    Nouns
    • ...
    Notes
    [1] S, K

Reads the StarDict files in place; writes nothing unless --out is given.
"""
import argparse, gzip, io, os, re, struct, sys, collections

AR = 'ء-يٰ-ۓ'
MARKS = 'ً-ْٰ'
ROMAN = {1: 'I', 2: 'II', 3: 'III', 4: 'IV', 5: 'V', 6: 'VI', 7: 'VII', 8: 'VIII', 9: 'IX', 10: 'X', 11: 'XI',
         12: 'XII'}
ARABIC_DIGITS = str.maketrans('0123456789', '٠١٢٣٤٥٦٧٨٩')
FATHA, DAMMA, KASRA, SUKUN, SHADDA = 'َ', 'ُ', 'ِ', 'ْ', 'ّ'
VOWEL_OF_MARK = {'ﹷ': FATHA, 'ﹹ': DAMMA, 'ﹻ': KASRA, 'َ': FATHA, 'ُ': DAMMA, 'ِ': KASRA}
WEAK = set('اوىيءأإآؤئ')
# [note] is the dictionary's own editorial line -- "You (m.pl.) were -- 2nd person
# past of كان" -- and is the whole article for the 4,415 entries that have no
# lexicon text of their own. It was not in this list, so every one of them parsed
# as a headword called "note" and lost its content.
SRC_TAGS = ('Lane', 'Sihah', 'Arramooz', 'ara-eng', 'note', 'notes')
PRONOUN_TAIL = r'(هُ|هَا|هُمْ|نِى|كَ|ه|ها)$'

SIGLA = set()  # filled from the dictionary's own [notes] blocks


def read_stardict(base):
    data = gzip.open(base + '.dict.dz').read()
    idx = open(base + '.idx', 'rb').read()
    ents, order, i = {}, [], 0
    while i < len(idx):
        j = idx.index(b'\0', i)
        key = idx[i:j].decode()
        off, sz = struct.unpack('>II', idx[j + 1:j + 9])
        ents[key] = (off, sz)
        order.append(key)
        i = j + 9
    return data, ents, order


def read_stardict_keys(base):
    """Just the key order of a dictionary, which is what its ordinals count in."""
    idx, keys, i = open(base + '.idx', 'rb').read(), [], 0
    while i < len(idx):
        j = idx.index(b'\0', i)
        keys.append(idx[i:j].decode())
        i = j + 9
    return keys


def read_stardict_plain(base):
    """Same as read_stardict for a dictionary whose .dict was never dictzipped."""
    data = open(base + '.dict', 'rb').read()
    idx = open(base + '.idx', 'rb').read()
    ents, order, i = {}, [], 0
    while i < len(idx):
        j = idx.index(b'\0', i)
        key = idx[i:j].decode()
        off, sz = struct.unpack('>II', idx[j + 1:j + 9])
        ents[key] = (off, sz)
        order.append(key)
        i = j + 9
    return data, ents, order


def collect_sigla(data, ents):
    for o, s in ents.values():
        b = data[o:o + s].decode('utf-8', 'replace')
        if '[notes]' not in b:
            continue
        for m in re.finditer(r'\n\s*\[\d+\]\s*(.+)', b[b.index('[notes]'):]):
            for t in re.split(r'[,;]', m.group(1)):
                t = t.strip(' .*')
                if t and re.match(r'^[A-Z][A-Za-z\-\']{0,7}$', t):
                    SIGLA.add(t)
    # Lane's own list of authorities (preface), for the ones the first pass never
    # numbered and so never reached [notes].
    SIGLA.update("""&c Gr Kull Ham Har Meyd A AA AAF AHát AHeyth AO AZ Az Bd BM CK EM Fr HM IAar IAmb IAth IB IDrd IDrst
        B IF IH IHsh IJ IKh IKoot IKt IKtt IM ISd ISh ISk Jel JK JM K Kf KL Kr Ksh KT Kz L Lb Lh Lth M MA Mbr MF Mgh
        Msb Mtr Mz Nh O PS R S Sb Sgh Sh Sr T TA Th TK TS Yz Zj Z Ibn-'Abbád Ibn-Abbád Er-Rághib Esh-Shiháb
        Abu-l-'Abbás from-a-trad""".split())


def skeleton(w, keep_shadda=False):
    w = re.sub('[ًٌٍَُِْٰـ]' if keep_shadda else '[' + MARKS + 'ـ]', '', w)
    return w.translate(str.maketrans('أإآٱ', 'اااا'))


def head_score(h):
    """More vowels is better; a vowel written before its shadda (قَاَّلَ) is index junk."""
    return len(h) - (10 if re.search('[َُِ]ّ|اَ', h) else 0)


def strip_tags(s):
    return re.sub(r'<[^>]+>', '', s)


# ---------------------------------------------------------------- notes ----
# Lane cites his authorities by initial, which is unreadable on a reader that
# carries no key to them. The notes spell them out; anything not in this table
# stays as Lane wrote it rather than being guessed at.
FULL_NAMES = {
    # Lexicons and books
    'S': 'Ṣiḥāḥ (al-Jawharī)',
    'K': 'Qāmūs (al-Fīrūzābādī)',
    'TA': 'Tāj al-ʿArūs (al-Zabīdī)',
    'M': 'Muḥkam (Ibn Sīdah)',
    'L': 'Lisān al-ʿArab (Ibn Manẓūr)',
    'A': 'Asās al-Balāghah (al-Zamakhsharī)',
    'O': 'ʿUbāb (al-Ṣaghānī)',
    'T': 'Tahdhīb al-Lughah (al-Azharī)',
    'Msb': 'Miṣbāḥ al-Munīr (al-Fayyūmī)',
    'Mgh': 'Mughrib (al-Muṭarrizī)',
    'Nh': 'Nihāyah (Ibn al-Athīr)',
    'B': 'Baṣāʾir Dhawī al-Tamyīz (al-Fīrūzābādī)',
    'Ksh': 'Kashshāf (al-Zamakhsharī)',
    'Kull': 'Kulliyyāt (al-Kafawī)',
    'Ham': 'Ḥamāsah',
    'Har': 'Maqāmāt (al-Ḥarīrī)',
    'MA': 'Muqaddimat al-Adab (al-Zamakhsharī)',
    'Meyd': 'Majmaʿ al-Amthāl (al-Maydānī)',
    'Bd': 'al-Bayḍāwī',
    'Jel': 'Tafsīr al-Jalālayn',
    'CK': 'Cairo edition of the Qāmūs',
    # People
    'IAar': 'Ibn al-Aʿrābī',
    'Az': 'al-Azharī',
    'AZ': 'Abū Zayd al-Anṣārī',
    'As': 'al-Aṣmaʿī',
    'AA': 'Abū ʿAmr ibn al-ʿAlāʾ',
    'AO': 'Abū ʿUbaydah',
    'AHn': 'Abū Ḥanīfah al-Dīnawarī',
    'AHeyth': 'Abū al-Haytham',
    'AHát': 'Abū Ḥātim',
    'Fr': 'al-Farrāʾ',
    'Ks': 'al-Kisāʾī',
    'Th': 'Thaʿlab',
    'Sb': 'Sībawayh',
    'Mbr': 'al-Mubarrad',
    'Zj': 'al-Zajjāj',
    'Sh': 'Shamir',
    'Lh': 'al-Liḥyānī',
    'Lth': 'al-Layth',
    'Kr': 'Kurāʿ',
    'Z': 'al-Zamakhsharī',
    'Sgh': 'al-Ṣaghānī',
    'ISk': 'Ibn al-Sikkīt',
    'ISd': 'Ibn Sīdah',
    'ISh': 'al-Naḍr ibn Shumayl',
    'IB': 'Ibn Barrī',
    'IJ': 'Ibn Jinnī',
    'IF': 'Ibn Fāris',
    'IDrd': 'Ibn Durayd',
    'IAth': 'Ibn al-Athīr',
    'IAmb': 'Ibn al-Anbārī',
    'IKtt': 'Ibn al-Qaṭṭāʿ',
    'IKt': 'Ibn Qutaybah',
    "Ibn-'Abbád": 'Ibn ʿAbbād',
    'Er-Rághib': 'al-Rāghib al-Iṣfahānī',
    'Gr': 'the grammarians',
    # Our own merge's sources, cited the same way rather than as inline tags
    'Sihah': 'Mukhtār al-Ṣiḥāḥ (al-Rāzī)',
    'Arramooz': 'Arramooz al-Wasīṭ',
    'ara-eng': 'FreeDict Arabic–English (Arabeyes)',
    'Lane': "Lane's Arabic-English Lexicon",
    # Wording Lane runs into the citation itself
    '&c': 'and others',
    'from-a-trad': 'from a tradition',
    'from a trad': 'from a tradition',
}


def expand_sigla(note):
    """Spells out the initials in one note, leaving unknown ones alone."""
    parts = []
    # "S. M, A" is Lane's "S, M, A" with a typo'd separator, and it reaches here
    # whole; split it so the first initial is spelled out too.
    for token in re.split(r'\s*,\s*|(?<=[A-Za-z])\.\s+(?=[A-Z])', note):
        bare = token.strip().rstrip(':;.').strip()
        # Lane's asterisk -- the source implies the meaning rather than stating it --
        # is written before the initial as often as after it.
        star = bare.startswith('*') or bare.endswith('*')
        bare = bare.strip('*').strip()
        full = FULL_NAMES.get(bare)
        if full is None:
            # "TA in art. بكر", "S ubi suprà", "A voce سبع": a siglum with the place
            # in it that Lane is pointing at. The locator stays, the initial does not.
            located = re.match(r'^(\S+)\s+((?:in art\.|in the|voce|ubi suprà|from a trad).*)$', bare)
            if located and located.group(1) in FULL_NAMES:
                full = FULL_NAMES[located.group(1)] + ', ' + located.group(2)
        parts.append((full or bare) + (' *' if star else ''))
    return ', '.join(p for p in parts if p)

def is_citation(inner):
    """(S, K, TA) / (Kull, p. 327.) / (TA in art. تلو.) / (Gr.) / (Bd in xix. 60.)"""
    s = inner.strip().rstrip('.')
    s = re.sub(r'\[See[^\]]*\]', '', s)            # (TA. [See also حطط.])
    s = re.sub(r'\bfrom a trad\.?', 'from-a-trad', s)  # (TA, from a trad.)
    s = re.sub(r'\b(?:in )?(?:art\.|[ivxlc]+\.)\s*[^,;]+', '', s)
    s = re.sub(r'\bp\. ?\d+', '', s)
    s = re.sub(r'\bubi suprà', '', s)
    s = re.sub(r'\bvoce [^,;]+', '', s)
    # "S. M, A" is Lane's "S, M, A" with a typo'd separator: split on ". " between capitals too.
    toks = [t.strip(' .;:*') for t in re.split(r'[,;]|(?<=[A-Za-z])\. (?=[A-Z])', s)]
    toks = [t for t in toks if t]
    return bool(toks) and all(t in SIGLA for t in toks)


def hoist_citations(text, notes):
    index = {n: i + 1 for i, n in enumerate(notes)}

    def repl(m):
        inner = m.group(1)
        if not is_citation(inner):
            return m.group(0)
        key = inner.strip().rstrip('.;:').strip()
        if key not in index:
            notes.append(key)
            index[key] = len(notes)
        return '[%d]' % index[key]

    return re.sub(r'\(([^()]{1,120})\)', repl, text)


# ------------------------------------------------------------- morphology ----
def radicals_for_measure(head, measure):
    sk = skeleton(re.sub(PRONOUN_TAIL, '', head))
    pats = {1: r'^(.)(.)(.)$', 2: r'^(.)(.)(.)$', 3: r'^(.)ا(.)(.)$', 4: r'^ا(.)(.)(.)$', 5: r'^ت(.)(.)(.)$',
            6: r'^ت(.)ا(.)(.)$', 7: r'^ان(.)(.)(.)$', 8: r'^ا(.)ت(.)(.)$', 10: r'^است(.)(.)(.)$'}
    p = pats.get(measure)
    m = re.match(p, sk) if p else None
    if not m:
        return None
    r = m.groups()
    # A weak middle radical is only a problem where the conjugation actually
    # contracts. II, III, V and VI keep it written throughout (قَوَّلَ · يُقَوِّلُ · تَقْوِيلٌ),
    # so those measures can be built from a hollow root like any other.
    weak = [i for i, x in enumerate(r) if x in WEAK]
    if weak and not (weak == [1] and measure in (2, 3, 5, 6)):
        return None
    if measure == 1 and r[1] == r[2]:
        return None
    return r


# The imperfect a template cannot build: قال is كال is خاف on the surface, and the
# root letter that decides يَقُولُ / يَبِيعُ / يَخَافُ is not in the past tense at all.
# Lane prints the form itself somewhere in the article, so take his rather than
# guess: a token that is this head's imperfect is the head's own first and last
# radical behind the right subject prefix.
IMPERFECT_PREFIX_VOWEL = {1: FATHA, 2: DAMMA, 3: DAMMA, 4: DAMMA, 5: FATHA, 6: FATHA,
                          7: FATHA, 8: FATHA, 9: FATHA, 10: FATHA}
AR_TOKEN = re.compile('[' + AR + MARKS + ']+')
# Lane writes the form out whenever the mark alone would not settle it.
IMPERFECT_WORD = re.compile(r'imperfect\s+([' + AR + MARKS + r']{3,})')


def weak_imperfect(head, mark):
    """Measure I of a weak root, from Lane's vowel mark: قَالَ + ﹹ is يَقُولُ.

    The past tense hides which letter the alif stands for, but the imperfect
    vowel names it -- damma means واو, kasra means ياء -- so the mark Lane does
    print is enough to write the form he does not."""
    v = VOWEL_OF_MARK.get(mark or '')
    if not v:
        return None
    shadda = skeleton(re.sub(PRONOUN_TAIL, '', head), keep_shadda=True)
    bare = shadda.replace(SHADDA, '')
    F, D, K, S = FATHA, DAMMA, KASRA, SUKUN
    if len(shadda) == 3 and shadda[2] == SHADDA:          # مَدَّ: doubled
        return 'يَ' + bare[0] + v + bare[1] + SHADDA + D
    if len(bare) != 3:
        return None
    if bare[1] == 'ا':                                    # قَالَ: hollow
        middle = {D: 'و', K: 'ي', F: 'ا'}[v]
        return 'يَ' + bare[0] + v + middle + bare[2] + D
    if bare[2] in 'اىي':                                  # دَعَا: defective
        return 'يَ' + bare[0] + S + bare[1] + v + {D: 'و', K: 'ي', F: 'ى'}[v]
    if bare[0] == 'و' and v == K:                         # وَعَدَ: assimilated
        return 'يَ' + bare[1] + v + bare[2] + D
    return None


def harvest_imperfect(head, measure, text):
    """(what Lane spells out after "imperfect", what merely looks like the form).

    The first is his own statement and outranks anything a template can build; the
    second is a guess from the article's prose and is the last resort before the
    bare vowel mark."""
    want = IMPERFECT_PREFIX_VOWEL.get(measure)
    if not want:
        return None, None
    hs = skeleton(re.sub(PRONOUN_TAIL, '', head))
    if len(hs) < 3:
        return None, None
    stated = None
    for written in IMPERFECT_WORD.findall(text):
        if written.startswith('ي') and skeleton(written)[-1] == hs[-1]:
            stated = written
            break
    best = None
    for token in AR_TOKEN.findall(text):
        # The vowel on the ي separates the active from the passive (يَقُولُ / يُقَالُ)
        # and measure I from measures II-IV; an unvowelled token settles nothing.
        if len(token) < 2 or token[0] != 'ي' or token[1] != want:
            continue
        ts = skeleton(re.sub(PRONOUN_TAIL, '', token))[1:]
        if not ts or ts[-1] != hs[-1] or abs(len(ts) - len(hs)) > 1:
            continue
        if measure == 1 and ts[0] != hs[0]:
            continue
        if best is None or len(token) > len(best):   # the most fully vowelled writing
            best = token
    return stated, best


def unjussive(form):
    """يَكُنْ -> يَكُونُ: the jussive of a hollow verb, written out again.

    Lane quotes whichever form the phrase he is citing used, so the imperfect he
    prints is sometimes مجزوم -- and a hollow verb drops its middle radical there.
    The vowel left on the first radical names the letter that dropped, exactly as
    it does in the perfect."""
    m = re.fullmatch('ي' + FATHA + '?(.)([' + DAMMA + KASRA + FATHA + '])(.)' + SUKUN, form or '')
    if not m:
        return form
    r1, v, r3 = m.group(1), m.group(2), m.group(3)
    return 'ي' + FATHA + r1 + v + {DAMMA: 'و', KASRA: 'ي', FATHA: 'ا'}[v] + r3 + DAMMA


def tasrif(head, measure, imperfect_mark, masdars, stated=None, harvested=None):
    """past · imperfect · masdar: built for a sound root, taken from Lane for the rest."""
    r = radicals_for_measure(head, measure)
    past = re.sub(PRONOUN_TAIL, '', head) if measure == 1 else head
    imp = None
    if r:
        r1, r2, r3 = r
        F, D, K, S, SH = FATHA, DAMMA, KASRA, SUKUN, SHADDA
        forms = {
            2: (r1 + F + r2 + SH + F + r3 + F, 'يُ' + r1 + F + r2 + SH + K + r3 + D, 'تَ' + r1 + S + r2 + K + 'ي' + r3 + 'ٌ'),
            3: (r1 + F + 'ا' + r2 + F + r3 + F, 'يُ' + r1 + F + 'ا' + r2 + K + r3 + D, 'مُ' + r1 + F + 'ا' + r2 + F + r3 + F + 'ةٌ'),
            4: ('أَ' + r1 + S + r2 + F + r3 + F, 'يُ' + r1 + S + r2 + K + r3 + D, 'إِ' + r1 + S + r2 + F + 'ا' + r3 + 'ٌ'),
            5: ('تَ' + r1 + F + r2 + SH + F + r3 + F, 'يَتَ' + r1 + F + r2 + SH + F + r3 + D, 'تَ' + r1 + F + r2 + SH + D + r3 + 'ٌ'),
            6: ('تَ' + r1 + F + 'ا' + r2 + F + r3 + F, 'يَتَ' + r1 + F + 'ا' + r2 + F + r3 + D, 'تَ' + r1 + F + 'ا' + r2 + D + r3 + 'ٌ'),
            7: ('اِنْ' + r1 + F + r2 + F + r3 + F, 'يَنْ' + r1 + F + r2 + K + r3 + D, 'اِنْ' + r1 + K + r2 + F + 'ا' + r3 + 'ٌ'),
            8: ('اِ' + r1 + S + 'تَ' + r2 + F + r3 + F, 'يَ' + r1 + S + 'تَ' + r2 + K + r3 + D, 'اِ' + r1 + S + 'تِ' + r2 + F + 'ا' + r3 + 'ٌ'),
            10: ('اِسْتَ' + r1 + S + r2 + F + r3 + F, 'يَسْتَ' + r1 + S + r2 + K + r3 + D, 'اِسْتِ' + r1 + S + r2 + F + 'ا' + r3 + 'ٌ'),
        }
        if measure == 1:
            v = VOWEL_OF_MARK.get(imperfect_mark or '')
            if v:
                imp = 'يَ' + r1 + S + r2 + v + r3 + D
        else:
            past, imp, dm = forms[measure]
            if not masdars:
                masdars = [dm]
    if imp is None:
        imp = unjussive(stated)
    if imp is None and measure == 1:
        imp = weak_imperfect(head, imperfect_mark)
    if imp is None:
        imp = unjussive(harvested)
    if imp is None and measure == 1 and imperfect_mark in VOWEL_OF_MARK:
        imp = 'ـ' + VOWEL_OF_MARK[imperfect_mark]
    parts = [past] + ([imp] if imp else []) + ([' / '.join(masdars)] if masdars else [])
    return ' · '.join(parts)


# Lane names his authorities in the running text too, not only in the citations the
# notes now carry: "according to the K", "but Sb makes a distinction". Those are the
# same sigla, and they are just as unreadable on a device that carries no key to
# them, so the prose gets the same treatment as the footnotes.
#
# A book is only expanded behind "the", which is how Lane always writes it and what
# keeps the one-letter sigla from eating ordinary words. A person is expanded on
# sight, with the exception of "As" (al-Aṣmaʿī), which is an English word.
INLINE_BOOKS = {siglum: FULL_NAMES[siglum].split(' (')[0] for siglum in (
    'S', 'K', 'TA', 'M', 'L', 'A', 'O', 'T', 'Msb', 'Mgh', 'Nh', 'Ksh', 'Kull', 'Ham', 'CK')}
INLINE_PEOPLE = {siglum: FULL_NAMES[siglum] for siglum in (
    'Sb', 'IB', 'ISd', 'ISk', 'ISh', 'IAar', 'IAth', 'IJ', 'IF', 'IDrd', 'IAmb', 'IKt', 'IKtt',
    'Az', 'AZ', 'AA', 'AO', 'AHn', 'AHeyth', 'AHát', 'Fr', 'Ks', 'Th', 'Mbr', 'Zj', 'Sh', 'Lh',
    'Lth', 'Kr', 'Sgh', 'Er-Rághib', 'Bd', 'Jel')}
INLINE_BOOK_RE = re.compile(r'\bthe (' + '|'.join(sorted(INLINE_BOOKS, key=len, reverse=True)) + r')\b')
INLINE_PERSON_RE = re.compile(r'(?<![\w-])(' + '|'.join(sorted(INLINE_PEOPLE, key=len, reverse=True)) + r')(?![\w-])')


# Lane joins a run of near-synonyms with "and" -- "العِلْمُ and المَعْرِفَةُ and
# الشُّعُورُ". Spelled out that way the run is three Arabic words with an English
# conjunction wedged between them, so the line breaks twice and the words end up
# scattered. They are a list, so set them as one: a middle dot, which the wrap
# treats as neutral and keeps inside the Arabic run.
AND_RUN = re.compile('(?<=[' + AR + MARKS + '])[ ]+and[ ]+(?=[' + AR + MARKS + '])')


def prose(text):
    """A sentence of Lane's, with its tags off and its sigla written out."""
    text = strip_tags(text)
    text = INLINE_BOOK_RE.sub(lambda m: 'the ' + INLINE_BOOKS[m.group(1)], text)
    text = INLINE_PERSON_RE.sub(lambda m: INLINE_PEOPLE[m.group(1)], text)
    # Lane's "&c." is an et cetera, not an authority.
    text = re.sub(r'&c\.?', 'etc.', text)
    text = re.sub(r'\b(see(?:\s+also)?\s+)(\d+)',
                  lambda m: m.group(1) + m.group(2).translate(ARABIC_DIGITS), text)
    return AND_RUN.sub(' · ', text)


# ---------------------------------------------------------------- parsing ----
# Lane numbers the measure after the head, and the head may carry its object:
# "رَمَى الشَّىْءَ 1". Only the first word is the verb; the rest is the sense's own.
MEASURE_START = re.compile(r'^([' + AR + MARKS + r']+)((?:\s+[' + AR + MARKS + r']+){0,2})\s+'
                           r'(\d+|R\. ?Q\. ?\d+|Q\. ?Q\. ?\d+)\b[ ,.:;]*(.*)$', re.S)
IMPERFECT = re.compile(r'imperfect\s+([ﹷﹹﹻَُِ])')
MASDAR = re.compile(r'masdar\s+((?:[' + AR + MARKS + r']+)(?:\s+(?:and|or)\s+[' + AR + MARKS + r']+)*)')
CROSSREF = re.compile(r'(?i)^(see|same as)\b')
# The principal parts, as Lane states them at the head of a sense. Once they stand
# on the measure's own line there is nothing left for them to say here, so the
# opening clause comes off -- but only the opening one: "imperfect" further into a
# sense is talking about that sense, not about the verb.
LEADING_TASRIF = re.compile(
    r'^(?:\[\d+\]\s*)?'
    r'(?:\[?imperfect\s+(?:[\ufe70-\ufe7f' + MARKS + r']+|[' + AR + MARKS + r']+)\]?\s*[,;]?\s*(?:\[\d+\]\s*)?)?'
    r'(?:masdar\s+[' + AR + MARKS + r']+'
    r'(?:\s*(?:\[\d+\]\s*)?(?:and|or)\s+[' + AR + MARKS + r']+)*'
    r'\s*[,;]?\s*(?:\[\d+\]\s*)?)?')


# Lane's === is a sense break, but the source fires it mid-sentence too, which
# leaves a bullet hanging: "مُسْتَقَرٌّ: see قَرَارٌ; the former in several places:" and
# then, on its own, "and for the latter, see 10." A sense of Lane's opens with a
# capital "And"; a lowercase one is the tail of the chunk before it.
CONTINUATION = re.compile(r'^(?:<[^>]+>)*\s*(?:and|or|but)\b')


def split_chunks(par):
    chunks = []
    for c in re.split(r'\s*(?:===|___)\s*', par):
        c = c.strip()
        if not c:
            continue
        if chunks and CONTINUATION.match(c):
            chunks[-1] += ' ' + c
        else:
            chunks.append(c)
    return chunks


# Lane writes an article as a chain of sourced statements -- a claim, then the
# authority for it -- and the source hands the whole chain over as one paragraph:
# the first sense of علم runs for 4,600 characters, and 1,375 entries have a
# bullet like it. The note number hoisted out of the text marks exactly where one
# claim ends, so cutting there turns the wall into the list it already was.
# The lookbehind is the other half of the rule: Lane ends a finished claim with a
# full stop or a colon and links an unfinished one with a semicolon, so
# "كَتَبَ ... masdar كَتْبٌ; and كَتَبَ عَلَيْهَا; [3] He closed the camel's vulva" is one
# claim -- cutting at its citation would leave a head with no meaning under it.
CLAIM_END = re.compile(r'(?<=[:.])[ ]*(\[\d+\])[ ]+(?=\S)')
# What follows a citation stands on its own when it opens with a head -- an Arabic
# word, or a capital -- or when it is long enough to be saying something. A short
# lowercase tail ("and the guest. [47]") is the end of the claim before it, which
# is the same reading CONTINUATION gives a chunk that opens lowercase.
CLAIM_MIN = 150
CLAIM_HEAD = re.compile(r'[' + AR + r'A-Z]')


# A claim Lane never sourced can still run for a page: the whole of his comparison
# of العلم with المعرفة is one unbroken stretch. Inside it a colon introduces the
# next independent statement -- but only a colon at the top level, since the ones
# inside the square brackets his editorial insertions are made of are punctuating a
# parenthesis, and cutting there would leave the bracket hanging open.
CLAIM_MAX = 700


def colon_cuts(text):
    out, start, depth = [], 0, 0
    for i, ch in enumerate(text):
        if ch in '[(':
            depth += 1
            continue
        if ch in '])':
            depth = max(0, depth - 1)
        # After a closing bracket too: "[... but not without controversy:] or ..."
        # ends its statement inside the insertion and resumes outside it.
        colon = ch == ':' or (ch in '])' and i and text[i - 1] == ':')
        if not colon or depth or not text[i + 1:i + 2] == ' ':
            continue
        if i + 1 - start >= CLAIM_MIN and len(text) - (i + 1) >= CLAIM_MIN:
            out.append(text[start:i + 1])
            start = i + 1
    out.append(text[start:])
    return [c.strip() for c in out if c.strip()]


def split_claims(bullet):
    parts = CLAIM_END.split(bullet)
    out = [parts[0]]
    for i in range(1, len(parts), 2):
        out[-1] += ' ' + parts[i]
        tail = parts[i + 1]
        if len(tail) >= CLAIM_MIN or CLAIM_HEAD.match(tail):
            out.append(tail)
        else:
            out[-1] += ' ' + tail
    claims = []
    for claim in out:
        claim = claim.strip()
        if not claim:
            continue
        claims += colon_cuts(claim) if len(claim) >= CLAIM_MAX else [claim]
    return claims


# The same pair of forms, headed together by Lane and split apart by the source,
# comes back as two bullets with one body between them -- so "the former" and "the
# latter" in that body point at nothing. Rejoin the heads onto the one body.
HEADED = re.compile(r'^([' + AR + MARKS + r']+)\s*:\s*(.+)$', re.S)


def merge_shared_body(lines):
    out = []
    for line in lines:
        prev = HEADED.match(out[-1]) if out else None
        cur = HEADED.match(line)
        if prev and cur and prev.group(2) == cur.group(2):
            out[-1] = prev.group(1) + ' · ' + cur.group(1) + ': ' + cur.group(2)
        else:
            out.append(line)
    return out


def parse_entry(text):
    """-> heads[], lane[], sihah[], arramooz[], araeng[], curated[], notes[] (deduplicated across sub-blocks)"""
    heads, lane, sihah, arramooz, araeng, curated, notes = [], [], [], [], [], [], []
    seen = set()
    mode = None
    for raw in text.split('\n'):
        s = raw.strip()
        if not s:
            continue
        m = re.match(r'\[([^\]]+)\]\s*(.*)$', s, re.S)
        if m and m.group(1) in SRC_TAGS:
            mode = m.group(1)
            s = m.group(2).strip()
            if mode == 'notes' or not s:
                continue
        elif m and mode != 'notes':
            if m.group(1) not in heads:
                heads.append(m.group(1))
            mode = None
            continue
        if mode == 'notes':
            nm = re.match(r'\[(\d+)\]\s*(.*)$', s)
            if nm:
                notes.append(nm.group(2).strip().rstrip(';'))
            continue
        if mode is None:
            continue
        if s in seen:
            continue
        seen.add(s)
        {'Lane': lane, 'Sihah': sihah, 'Arramooz': arramooz, 'ara-eng': araeng, 'note': curated}[mode].append(s)
    return heads, lane, sihah, arramooz, araeng, curated, notes


def measure_key(m):
    return (0, m) if isinstance(m, int) else (1, m)


# ------------------------------------------------------- cross-references ----
# A derived form often has no entry of its own beyond a pointer: اِسْتَكْبَرَ 10 is
# "see 4: === see also 5, in two places", and measures 4 and 5 live in the root's
# entry (كبر). On the device that pointer is unfollowable -- the reader is looking
# at اِسْتَكْبَرَ, not at كبر -- so the entry says nothing at all. Follow it here and
# bring the sense back with it.
SEE_MEASURE = re.compile(r'(?i)^see(\s+also)?\s+([٠-٩\d]+)\s*(.*)$', re.S)
WESTERN = str.maketrans('٠١٢٣٤٥٦٧٨٩', '0123456789')
# The stem a measure builds on top of the root. Stripping one off the entry's own
# key is how the root entry is found, and the candidate is only accepted when it
# turns out to carry the very measure that was asked for.
ROOT_PREFIXES = ('است', 'ان', 'ات', 'ا', 'ت', 'م', 'ي', 'ن')
# A gloss pulled out of another entry is there to answer "what does this word
# mean", not to move the whole article; one sentence of it is the answer.
GLOSS_MAX = 300


def root_candidates(key):
    for pre in ROOT_PREFIXES:
        if key.startswith(pre) and len(key) - len(pre) >= 3:
            yield key[len(pre):]
    # Measure VIII writes its ت inside the word: اكتسب is كسب, not كتسب.
    if len(key) >= 5 and key[0] == 'ا' and key[2] == 'ت':
        yield key[1] + key[3:]


def measure_forms(root, n):
    """The keys a measure of this root would be filed under: 4 of كبر is اكبر."""
    if len(root) != 3:
        return ()
    a, b, c = root
    return {1: (root,), 2: (root,), 3: (a + 'ا' + b + c,), 4: ('ا' + root,),
            5: ('ت' + root,), 6: ('ت' + a + 'ا' + b + c,), 7: ('ان' + root,),
            8: ('ا' + a + 'ت' + b + c,), 10: ('است' + root,)}.get(n, ())


def entry_senses(text):
    """{measure: (head, [glosses])} for an entry we are only reading through."""
    lane = parse_entry(text)[1]
    out = {}
    for par in lane:
        par = hoist_citations(par, [])
        m = MEASURE_START.match(par)
        if not m:
            continue
        w, num, rest = m.group(1), m.group(3), m.group(4)
        mnum = int(num) if num.isdigit() else num
        glosses = out.setdefault(mnum, (w, []))[1]
        for c in split_chunks(rest):
            c = re.sub(r'\s*\[\d+\]', '', prose(c)).strip()
            c = LEADING_TASRIF.sub('', c, count=1).lstrip(' ,;')
            if c and not CROSSREF.match(c):
                glosses.append(c)
    return out


def shorten(gloss):
    if len(gloss) <= GLOSS_MAX:
        return gloss
    cut = gloss.rfind(': ', 0, GLOSS_MAX)
    if cut < GLOSS_MAX // 3:
        cut = gloss.rfind(' ', 0, GLOSS_MAX)
    return gloss[:cut].rstrip(' ,;:') + ' …'


def expand_crossref(bullet, key, resolve):
    """"see 4" -> "see ٤ أَكْبَرَ, in كبر — He deemed him great …"."""
    m = SEE_MEASURE.match(bullet)
    if not m or resolve is None:
        return bullet
    hit = resolve(key, int(m.group(2).translate(WESTERN)))
    if not hit:
        return bullet
    head, gloss, src = hit
    out = 'see%s %s %s' % (m.group(1) or '', m.group(2).translate(ARABIC_DIGITS), head)
    if src:
        out += ', in ' + src
    tail = m.group(3).strip(' :;,')
    if gloss:
        out += ' — ' + shorten(gloss)
    if tail:
        out += ' (' + tail.rstrip('.') + ')'
    return out


# --------------------------------------------------- imported from Lane ----
# ar-en-al-furu' is built out of Lane, but its four-letter roots did not survive
# the build: صلصل, زلزل, دحرج, بلبل, وسوس and five thousand more are simply not in
# it. So صَلۡصَٰلٖ missed, and a miss is not where it ended -- with no headword to
# answer, the stemmer went on to read a root letter as an affix and وسوس came back
# as و + سوس, بعثر as ب + عثر.
#
# Lane's own dictionary still has every one of them, in his raw markup rather than
# the house format, so they are re-rendered here by the same rules as everything
# else: the parenthesised authorities hoisted into numbered notes, the senses cut
# into bullets, and the long ones cut again at their claim boundaries.
LANE_TAGS = re.compile(r'</?[ibem]>|</?em>|</?strong>')
LANE_SENSE = re.compile(r'^\s*\d+\.\s*')
# &amp; goes last, or it makes entities out of the ones already decoded.
LANE_ENTITIES = (('&darr;', '\u2193'), ('&uarr;', '\u2191'), ('&nbsp;', ' '),
                 ('&lt;', '<'), ('&gt;', '>'), ('&quot;', '"'), ('&amp;', '&'))


def lane_plain(raw):
    text = raw.replace('<br>', '\n').replace('<br/>', '\n').replace('<br />', '\n')
    text = LANE_TAGS.sub('', text)
    for entity, ch in LANE_ENTITIES:
        text = text.replace(entity, ch)
    return re.sub(r'[ \t]+', ' ', text)


def lane_head(key, first_bullet):
    """The vowelled headword Lane opens the article with, or the key itself."""
    m = re.match(r'^\s*([' + AR + MARKS + r']+)', first_bullet)
    return m.group(1) if m else key


def render_lane_entry(key, raw):
    notes, bullets = [], []
    for block in lane_plain(raw).split('\n'):
        # Lane numbers his senses; the number is his ordering of the article, not a
        # measure, and a quadriliteral has no measures to name.
        block = LANE_SENSE.sub('', block.strip())
        # ___ is his own separator between the senses inside one of them.
        for piece in re.split(r'\s*___\s*', block):
            piece = piece.strip()
            if not piece:
                continue
            bullets += split_claims(hoist_citations(piece, notes))
    if not bullets:
        return ''
    out = [lane_head(key, bullets[0]), '']
    out += ['• ' + b for b in bullets]
    if notes:
        out.append('')
        out.append('Notes')
        out += ['[%d] %s' % (i, expand_sigla(n)) for i, n in enumerate(notes, 1)]
    return '\n'.join(out)


def render_entry(key, text, resolve=None):
    heads, lane, sihah, arramooz, araeng, curated, notes = parse_entry(text)
    measures = {}
    nouns = []
    for par in lane:
        par = hoist_citations(par, notes)
        m = MEASURE_START.match(par)
        if m:
            w, obj, num, rest = m.group(1), m.group(2).strip(), m.group(3), m.group(4)
            mnum = int(num) if num.isdigit() else num
            sec = measures.setdefault(mnum, {'head': w, 'imp': None, 'masdar': [], 'bullets': []})
            chunks = split_chunks(rest) if rest.strip() else []
            for chunk in chunks:
                im = IMPERFECT.search(chunk)
                if im and not sec['imp']:
                    sec['imp'] = im.group(1)
                md = MASDAR.search(chunk)
                if md and not sec['masdar']:
                    sec['masdar'] = re.split(r'\s+(?:and|or)\s+', md.group(1))
            sec['text'] = sec.get('text', '') + ' ' + rest
            for i, c in enumerate(chunks):
                c = prose(c).strip()
                if i == 0 and not CROSSREF.match(c):
                    if sec['imp'] or sec['masdar']:
                        c = LEADING_TASRIF.sub('', c, count=1).lstrip(' ,;')
                        # The clause that was cut is what the "and" joined onto.
                        c = re.sub(r'^(?:and|or)\s+', '', c)
                    # The object Lane put in the head belongs to the sense it heads.
                    c = (w + ' ' + obj).strip() + ' — ' + c
                sec['bullets'].append(c)
        elif re.match('^[' + AR + ']', par) and not re.fullmatch(r'[' + AR + MARKS + r']+', par):
            nouns += [prose(c) for c in split_chunks(par)]
    nouns = merge_shared_body(nouns)

    # Sihah sentences / Arramooz cards go under the measure whose form they use.
    # Skeletons keep the shadda so كتّب (II) and كتب (I) stay apart; longest wins.
    forms = sorted(((skeleton(re.sub(PRONOUN_TAIL, '', v['head']), keep_shadda=True), m) for m, v in
                    measures.items()), key=lambda t: -len(t[0]))
    extra = collections.defaultdict(list)
    general = []

    def note_for(tag):
        # A non-Lane source is cited the way Lane's authorities are: by note number.
        if tag not in notes:
            notes.append(tag)
        return notes.index(tag) + 1

    def assign(line, tag):
        sk = skeleton(line, keep_shadda=True)
        for form, m in forms:
            if len(form) >= 3 and re.search(r'(?<![' + AR + '])[وف]?' + re.escape(form) + r'(?:ه|ها|ت|تُ|نا|وا|ني)?(?![' + AR + '])', sk):
                extra[m].append((tag, line))
                return
        general.append((tag, line))

    for blk in sihah:
        for sent in re.split(r'\s*\|\s*', blk):
            if sent.strip():
                assign(sent.strip(), 'Sihah')
    for blk in arramooz:
        assign(re.sub(r'^[.\s]+', '', strip_tags(blk).strip()), 'Arramooz')
    ae = []
    for blk in araeng:
        ae += [g.strip() for g in re.split(r'</?(?:li|div|ol|ul|p|b|i)[^>]*>', blk) if g.strip()]

    best = collections.OrderedDict()
    for h in heads:
        k = skeleton(h, keep_shadda=True)
        if k not in best or head_score(h) > head_score(best[k]):
            best[k] = h
    # The variant headwords are every measure's own head over again, and the screen
    # already carries the word looked up as its title. An entry that conjugates
    # nothing still needs a title line of its own.
    out = [] if measures else [' · '.join(best.values()) if best else key]
    # The editorial line first: on an entry that has nothing else it IS the entry.
    for line in curated:
        out.append('• ' + prose(line))
    for mnum in sorted(measures, key=measure_key):
        sec = measures[mnum]
        # An Arabic-Indic numeral, not "II": a Latin token here would split the
        # principal parts onto a line of their own (see the single-script wrap in
        # DictionaryDefinitionActivity).
        label = (str(mnum).translate(ARABIC_DIGITS) if isinstance(mnum, int) else mnum)
        stated, harvested = harvest_imperfect(sec['head'], mnum if isinstance(mnum, int) else 0,
                                              sec.get('text', '') + ' ' + ' '.join(sec['bullets']))
        out.append('%s  %s' % (label, tasrif(sec['head'], mnum if isinstance(mnum, int) else 0, sec['imp'],
                                              sec['masdar'], stated, harvested)))
        # The principal parts head the measure; the senses under them are not a
        # continuation of that line, so they do not start flush against it.
        out.append('')
        for b in sec['bullets']:
            for claim in split_claims(expand_crossref(b, key, resolve)):
                out.append('• ' + claim)
        for tag, line in extra.get(mnum, []):
            out.append('• %s [%d]' % (line, note_for(tag)))
    if nouns or general or ae:
        out.append('Nouns')
        for n in nouns:
            for claim in split_claims(n):
                out.append('• ' + claim)
        for tag, line in general:
            out.append('• %s [%d]' % (line, note_for(tag)))
        if ae:
            out.append('• ' + '; '.join(dict.fromkeys(ae)) + ' [%d]' % note_for('ara-eng'))
    if notes:
        # A blank line before the apparatus: the notes are not another sense.
        out.append('')
        out.append('Notes')
        for i, n in enumerate(notes, 1):
            out.append('[%d] %s' % (i, expand_sigla(n)))
    return '\n'.join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('base')
    ap.add_argument('keys', nargs='*')
    ap.add_argument('--out', help='write a new .dict + .idx (uncompressed) with this base name')
    ap.add_argument('--max', type=int, default=0, help='truncate printed entries to N chars')
    ap.add_argument('--import-lane', metavar='BASE',
                    help="lane-lexicon base path: import the roots this dictionary lacks")
    ap.add_argument('--import-keys', metavar='FILE',
                    help='one key per line: exactly which of its articles to take')
    ap.add_argument('--syn-from', metavar='BASE',
                    help='whose .syn to carry over, and whose index its ordinals count in '
                         '(default: the source being rendered)')
    a = ap.parse_args()
    data, ents, order = read_stardict(a.base)
    collect_sigla(data, ents)

    senses_cache = {}

    def senses(key):
        if key not in senses_cache:
            o, s = ents[key]
            senses_cache[key] = entry_senses(data[o:o + s].decode('utf-8', 'replace'))
        return senses_cache[key]

    def resolve(key, measure):
        """(head, gloss, entry it came from) for the measure a pointer names."""
        # A pointer that stays inside the entry only needs a name -- the measure it
        # names is already on the screen. One that leaves has to bring the sense.
        own = senses(key)
        if measure in own:
            return own[measure][0], None, None
        roots = [c for c in root_candidates(key) if c in ents] or [key]
        seen = []
        for root in roots:
            seen += [f for f in measure_forms(root, measure) if f in ents] + [root]
        for src in seen:
            if src != key and measure in senses(src):
                head, glosses = senses(src)[measure]
                return head, (glosses[0] if glosses else None), src
        return None
    if a.keys:
        for k in a.keys:
            o, s = ents[k]
            print('=' * 60, k)
            r = render_entry(k, data[o:o + s].decode('utf-8', 'replace'), resolve)
            print(r[:a.max] + ' …' if a.max and len(r) > a.max else r)
        return
    if a.out:
        bodies = {}
        for k in order:
            o, s = ents[k]
            rendered = render_entry(k, data[o:o + s].decode('utf-8', 'replace'), resolve)
            # An entry with no sense in it is worse than no entry: it tells the reader
            # nothing and it takes the lookup away from the candidate behind it --
            # وكا is an index key with an empty article, and it was answering وكان,
            # which is و + كان.
            if not any(line.startswith('•') for line in rendered.split('\n')):
                continue
            bodies[k] = rendered.encode('utf-8')

        imported = 0
        if a.import_lane:
            lane_data, lane_ents, lane_order = read_stardict_plain(a.import_lane)
            # Which of Lane's articles to take is not a question about their shape.
            # A four-letter key is as often an inflected form as a root -- ذكره,
            # قومه, وغير are all in there -- and importing one of those turns an
            # exact hit into a hijack: قومه stops reaching قوم and answers itself.
            #
            # The rule that does hold: take an article only where this dictionary
            # has nothing at all to say. Every key in the list is one the lookup was
            # asked for, in full, and missed -- see the header of the list file.
            wanted = set()
            with open(a.import_keys, encoding='utf-8') as handle:
                for line in handle:
                    line = line.strip()
                    if line and not line.startswith('#'):
                        wanted.add(line)
            for k in lane_order:
                if k in bodies or k not in wanted:
                    continue
                o, s = lane_ents[k]
                rendered = render_lane_entry(k, lane_data[o:o + s].decode('utf-8', 'replace'))
                if not rendered:
                    continue
                bodies[k] = rendered.encode('utf-8')
                imported += 1
            print('imported %d of %d wanted articles from %s' % (imported, len(wanted), a.import_lane),
                  file=sys.stderr)

        # StarDict binary-searches the index, and the reader compares keys byte by
        # byte with only ASCII case folded (StarDictIndex::compareKeys). The source
        # is in that order already; the merged set has to be put back into it.
        def index_order(key):
            return bytes(c | 0x20 if 65 <= c <= 90 else c for c in key.encode('utf-8'))

        newdata, newidx = io.BytesIO(), io.BytesIO()
        kept = sorted(bodies, key=index_order)
        for k in kept:
            body = bodies[k]
            newidx.write(k.encode('utf-8') + b'\0' + struct.pack('>II', newdata.tell(), len(body)))
            newdata.write(body)
        open(a.out + '.dict', 'wb').write(newdata.getvalue())
        open(a.out + '.idx', 'wb').write(newidx.getvalue())

        # Whose synonyms these are, and whose index their ordinals count in. When a
        # build extends an earlier one, that earlier one is the answer: the source's
        # own table is a different, larger set and swapping it in changes lookups
        # that have nothing to do with this import.
        syn_base = a.syn_from or a.base
        syn_order = order if syn_base == a.base else read_stardict_keys(syn_base)
        if os.path.exists(syn_base + '.syn'):
            # Through the key rather than the number: the source ordinal names an
            # entry in the source's order, and what is wanted is where that same
            # entry ended up. One that did not survive takes its synonyms with it.
            where = {k: i for i, k in enumerate(kept)}
            syn, at, moved, lost = open(syn_base + '.syn', 'rb').read(), 0, io.BytesIO(), 0
            while at < len(syn):
                j = syn.index(b'\0', at)
                key, ordinal = syn[at:j], struct.unpack('>I', syn[j + 1:j + 5])[0]
                at = j + 5
                target = syn_order[ordinal] if ordinal < len(syn_order) else None
                if target is None or target not in where:
                    lost += 1
                    continue
                moved.write(key + b'\0' + struct.pack('>I', where[target]))
            open(a.out + '.syn', 'wb').write(moved.getvalue())
            print('rewrote .syn: %d synonyms, %d dropped with their entry' %
                  (len(syn.split(b'\0')) - 1 - lost, lost), file=sys.stderr)
        with open(a.out + '.keys', 'w', encoding='utf-8', newline='\n') as handle:
            handle.write('\n'.join(kept))
        print('wrote', a.out + '.dict', newdata.tell(), 'bytes;', len(kept), 'entries (dropped',
              len(order) - len(kept), 'empty); idxfilesize', newidx.tell())

if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main()
