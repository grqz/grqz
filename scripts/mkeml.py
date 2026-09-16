import datetime
import email
import email.utils
import email.policy
import pathlib
import re
import sys

root = pathlib.Path(__file__).parents[1]

ENGLISH_MONTHS = [
    'Jan', 'Feb', 'Mar',
    'Apr', 'May', 'Jun',
    'Jul', 'Aug', 'Sep',
    'Oct', 'Nov', 'Dec',
]

def md_esc(x: str) -> str:
    return re.sub(r'[\\`*_{}\[\]<>()#+-.!|]', r'\\\g<0>', x)

def proc_msg(x: str, ctype: str) -> str:
    if ctype.startswith('text/html'):
        out, n =re.subn(r'\n[\s\n]+', r'\n', x)
        sys.stderr.write(f'h{n}\n')
        return out
    return x

if __name__ == '__main__':
    if len(sys.argv) == 1:
        sys.stderr.write(f'Usage: {sys.argv[0]} <eml>\n')
        sys.exit(1)

    last = max(map(
        lambda y: (lambda x:
            int(x) if all('0' <= y <= '9' for y in x)
            else 0)(y.name.removesuffix('.md')),
        (root / 'email').iterdir()))

    with (root / 'email' / 'README.md').open('a') as readme:
        for nord, eml in enumerate(sys.argv[1:], 1):
            tord = last + nord
            with open(eml, 'rb') as f:
                msg = email.message_from_binary_file(f, policy=email.policy.default)
            with (root / 'email' / f'{tord:04}.md').open('w') as mdf:
                subj = msg['subject']
                date = msg['date']
                edt = email.utils.parsedate_to_datetime(date)
                edt = edt.astimezone(datetime.timezone.utc)
                fdate = edt.strftime(f'%d {ENGLISH_MONTHS[edt.month - 1]} %Y')
                readme.write(f'|{tord:4}|[{subj}](<{tord:04}.md>)|{fdate}|\n')
                body = msg.get_body(('html', 'plain'))
                assert body is not None
                ctype = body.get_content_type()
                mdf.write('|Header field|Value|\n|------------|-----|\n')
                for h, val in (('date', date), ('subject', subj), ('content-type', ctype)):
                    mdf.write(f'|{h}|{md_esc(val)}|\n')
                mdf.write('\n' + proc_msg(body.get_content(), ctype))
