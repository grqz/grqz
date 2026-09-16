import re
import collections

from .mkeml import md_unesc, root, ls_eml

def statinfo(emlord: int) -> tuple[str, bool, str]:
    with (root / 'email' / f'{emlord:04}.md').open() as f:
        it = iter(f)
        lines = [next(it)[:-2] for _ in range(5)]
        getlast = lambda l: md_unesc(re.split(r'\|(?!\\)', l[::-1], maxsplit=1)[0][::-1])
        return lines[2][-5:], getlast(lines[3]) != '(No Subject)', getlast(lines[4])

if __name__ == '__main__':
    info = 'tz', 'has subject', 'ctype'
    for col, it in zip(info, zip(*map(statinfo, filter(None, ls_eml())))):
        print(f'{col}: {collections.Counter(it).most_common()}')
