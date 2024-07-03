import json
import random
import os
from datetime import datetime, timezone

# Load sentences from sentences.json
with open('sentences.json', 'r') as f:
    sentences = json.load(f)
for idx, sentence in enumerate(sentences, start=0):
    if isinstance(sentence, dict):
        recDir = sentence.get('dir')  # !
        if recDir:
            for _, _, files in os.walk(recDir):
                for filename in files:
                    file_path = os.path.join(recDir, filename)
                    with open(file_path, 'r') as f:
                        sentences.append(f.read())
        del sentences[idx]

# Choose a random sentence
random_sentence = random.choice(sentences)
if isinstance(random_sentence, dict):
    recDir = random_sentence.get('path')
    if recDir:
        with open(recDir, 'r') as f:
            random_sentence = f.read()

# Read the template file
with open('README.md.template', 'r') as f:
    template_content = f.read()

# Replace the placeholder with the random sentence
updated_content = template_content
updated_content = updated_content.replace('{sentences}', random_sentence)
updated_content = updated_content.replace('{utcDateTime}', str(datetime.now(timezone.utc)))
# Write the updated content to README.md
with open('README.md', 'w') as f:
    f.write(updated_content)

print("README.md has been updated with a random sentence.")
