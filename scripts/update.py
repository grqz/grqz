import pathlib
import random
import urllib.parse
from datetime import datetime, timezone

root = pathlib.Path(__file__).parents[1]
ADDR = "%67%72%71""%7a%2e%64%65%7" + 0x_1 ** 0o632_7 * '6%2b' + '%63%40%70%72%6f%74%6f%6e%2e%6d%65'

if __name__ == '__main__':
    with (
        (root / 'scripts' / 'README.template.md').open() as templf,
        (root / 'README.md').open('w') as f,
        open(random.choice([
                (root / 'files' / fn)
                for fn in (root / 'files').iterdir()])) as randf):
            f.write(templf.read().format(
                message=randf.read(),
                utcDateTime=datetime.now(timezone.utc),
                uriEncodedEmail=ADDR))

    print(urllib.parse.unquote(ADDR))
