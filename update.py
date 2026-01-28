import os
import urllib.parse
import random
from datetime import datetime, timezone

FILES_DIR = 'files'
ADDR = "%67%72%71%7a%2e%64%65%76%40%70%72%6f%74%6f%6e%2e%6d%65"

if __name__ == '__main__':
    paths = [
        os.path.join(FILES_DIR, fn)
        for _, _, files in os.walk(FILES_DIR)
        for fn in files]

    with (
        open('README.md.template', 'r') as templf,
        open('README.md', 'w') as f,
        open(random.choice(paths)) as randf):
            f.write(templf.read()
                .replace(r'{message}', randf.read())
                .replace(r'{utcDateTime}', str(datetime.now(timezone.utc)))
                .replace(r'{uriEncodedEmail}', ADDR))

    print(urllib.parse.unquote(ADDR))
