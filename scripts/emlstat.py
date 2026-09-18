import re
import collections

from .mkeml import md_unesc, root, ls_eml

BEGIN_MARKER = b'\n<!--BEGIN emlstat-->\n```py\n'
END_MARKER = b'\n```\n<!--END emlstat-->\n'

def statinfo(emlord: int) -> tuple[str, bool, str]:
    with (root / 'email' / f'{emlord:04}.md').open() as f:
        it = iter(f)
        lines = [next(it)[:-2] for _ in range(5)]
        getlast = lambda l: md_unesc(re.split(r'\|(?!\\)', l[::-1], maxsplit=1)[0][::-1])
        return lines[2][-5:], getlast(lines[3]) != '(No Subject)', getlast(lines[4])

if __name__ == '__main__':
    README = root / 'email' / 'README.md'
    with README.open('rb') as f:
        m = bytearray(f.read())

    begin = m.index(BEGIN_MARKER) + len(BEGIN_MARKER)
    end = m.index(END_MARKER)
    if end < begin:
        raise ValueError('end before begin')

    m[begin:end] = b'\n'.join(
        f'{col}: {collections.Counter(it).most_common()}'.encode()
        for col, it in zip(
            ('tz', 'has subject', 'ctype'),
            zip(*map(statinfo, filter(None, ls_eml())))))

    with README.open('wb') as f:
        f.write(m)
