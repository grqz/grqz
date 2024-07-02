import json
import random

# Load sentences from sentences.json
with open('sentences.json', 'r') as f:
    sentences = json.load(f)

# Choose a random sentence
random_sentence = random.choice(sentences)
if isinstance(random_sentence, dict):
    filePath = random_sentence.get('path')
    if filePath:
        with open(filePath, 'r') as f:
            random_sentence = f.read()

# Read the template file
with open('README.md.template', 'r') as f:
    template_content = f.read()

# Replace the placeholder with the random sentence
updated_content = template_content.replace('{sentences}', random_sentence)

# Write the updated content to README.md
with open('README.md', 'w') as f:
    f.write(updated_content)

print("README.md has been updated with a random sentence.")
