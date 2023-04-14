#!/bin/python3
import os

# Get the input file name
input_filename = 'data/styles.css'
input_file_extension = os.path.splitext(input_filename)[1]

# Generate the output file name
output_filename = os.path.splitext(input_filename)[0] + input_file_extension + '.h'

# Remove data/ from the output file name
output_filename = output_filename.replace('data/', '')
print(output_filename)

with open(input_filename, 'r') as file:
    content = file.read()

# Escape any double quotes or backslashes in the content
content = content.replace('"', '\\"').replace('\\', '\\\\')

header_content = 'const char* styles_css = R"=====({})=====";'.format(content)

# Write the contents to the output file
with open(output_filename, 'w') as file:
    file.write(header_content)
