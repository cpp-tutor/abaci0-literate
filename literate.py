#!/usr/bin/env python3

import sys
import os
import re

if len(sys.argv) != 2:
    print('Syntax: literate.py program.lit')

input_file = sys.argv[1]
output_file = input_file[:input_file.rfind('.')] + '.md'
output_file_contents = ''
current_source_file = ''
current_source_file_contents = ''
add_to_current_source_file = False

def read_next_line(file_path):
    with open(file_path, 'r') as file:
        for line in file:
            yield line.rstrip('\n')
        yield None

print('Reading: ' + input_file)
input_line = read_next_line(input_file)
while True:
    line = next(input_line)
    if line == None:
        if current_source_file != '':
            if os.path.dirname(current_source_file) != '':
                os.makedirs(os.path.dirname(current_source_file), exist_ok=True)
            print('Writing: ' + current_source_file)
            with open(current_source_file, 'w') as file:
                file.write(current_source_file_contents)
            current_source_file_contents = ''
        break
    if line[:3] == '```':
        add_to_current_source_file = not add_to_current_source_file
        if add_to_current_source_file:
            line = next(input_line)
            if line != None and line[:3] == '//@':
                if current_source_file != '':
                    if os.path.dirname(current_source_file) != '':
                        os.makedirs(os.path.dirname(current_source_file), exist_ok=True)
                    print('Writing: ' + current_source_file)
                    with open(current_source_file, 'w') as file:
                        file.write(current_source_file_contents)
                    current_source_file_contents = ''
                current_source_file = line[3:]
                if current_source_file.find('@') != -1:
                    current_source_file = current_source_file[:current_source_file.find('@')]
            elif line != None and line[:3] == '//#':
                pass
            else:
                current_source_file_contents += line + '\n'
    elif add_to_current_source_file and current_source_file != '':
        current_source_file_contents += line + '\n'

if add_to_current_source_file:
    print('Incomplete source file: ' + current_source_file)
    exit(1)

current_source_file_begin = 0
current_source_file_end = 0
print('Reading: ' + input_file)
input_line = read_next_line(input_file)
while True:
    line = next(input_line)
    if line == None:
        break
    if line[:3] == '```':
        if not add_to_current_source_file:
            output_file_contents += line + '\n'
        if add_to_current_source_file and current_source_file_contents != '':
            lines = current_source_file_contents.split('\n')
            if current_source_file_begin != 0:
                lines = lines[current_source_file_begin:]
            if current_source_file_end != 0:
                lines = lines[:len(lines) - current_source_file_end - 1]
            while lines and lines[-1] == '':
                lines.pop()
            output_file_contents += '\n'.join(lines) + '\n'
            current_source_file_contents = ''
            current_source_file_begin = 0
            current_source_file_end = 0
        if add_to_current_source_file:
            output_file_contents += line + '\n'
        add_to_current_source_file = not add_to_current_source_file
        if add_to_current_source_file:
            line = next(input_line)
            if line != None and (line[:3] == '//@' or line[:3] == '//#'):
                current_source_file = line[3:]
                if current_source_file.find('@') != -1:
                    lines_to_omit = current_source_file[current_source_file.find('@') + 1:]
                    pattern = r'\+(\d+),\-(\d+)'
                    matches = re.match(pattern, lines_to_omit)
                    if matches:
                        current_source_file_begin = int(matches.group(1))
                        current_source_file_end = int(matches.group(2))
            else:
                current_source_file_contents += line + '\n'
    else:
        if add_to_current_source_file:
            current_source_file_contents += line + '\n'
        else:
            output_file_contents += line + '\n'    

print('Writing: ' + output_file)
with open(output_file, 'w') as file:
    file.write(output_file_contents)
