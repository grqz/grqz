import os
import random
from datetime import datetime, timezone

FILES_DIR = 'files'

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
                .replace(r'{sentences}', randf.read())
                .replace(r'{utcDateTime}', str(datetime.now(timezone.utc))))

    print('updated')
