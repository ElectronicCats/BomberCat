#!/bin/python3
import os

# Define the directory where the web files are located
input_dir = 'data'

for filename in os.listdir(input_dir):
    file_extension = os.path.splitext(filename)[1]

    # Check if the file has an extension of .html, .css, or .js
    if file_extension in ('.html', '.css', '.js'):
        output_filename = os.path.join(input_dir, os.path.splitext(filename)[0] + file_extension + '.h')

        # Remove data/ from the output file name
        output_filename = output_filename.replace('data/', '')

        with open(os.path.join(input_dir, filename), 'r') as file:
            content = file.read()
            
        header_variable_name = os.path.splitext(filename)[0] + '_' + file_extension[1:]
        header_content = 'const char* {} = R"=====({})=====";'.format(header_variable_name, content)

        with open(output_filename, 'w') as file:
            file.write(header_content)
