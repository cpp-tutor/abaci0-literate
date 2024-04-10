#!/usr/bin/env python3

import sys
import os

if len(sys.argv) != 2:
    print('Syntax: literate.py program.lit')

input_file = sys.argv[1]
output_file = input_file[:input_file.rfind('.')] + '.md'
current_source_file = ''
print('Reading: ' + input_file + ', writing: ' + output_file)
with open(input_file, 'r') as file:
    input_str = file.read()
ofile = open(output_file, 'w')
osource = None
writing_source = False

lines = input_str.splitlines()
while len(lines):
    line = lines[0]
    lines = lines[1:]
    if len(lines):
        next_line = lines[0]
    else:
        next_line = ''
    if line[:3] == '```':
        ofile.write(line + '\n')
        if writing_source:
            writing_source = False
        else:
            if next_line[:3] == '//@':
                if osource != None:
                    osource.close()
                    osource = None
                    writing_source = False
                current_source_file = next_line[3:]
                if current_source_file != '':
                    print('Writing: ' + current_source_file)
                    lines = lines[1:]
                    if os.path.dirname(current_source_file) != '':
                        os.makedirs(os.path.dirname(current_source_file), exist_ok=True)
                    osource = open(current_source_file, 'w')
                    writing_source = True
            else:
                writing_source = True
    else:
        ofile.write(line + '\n')        
        if writing_source == True and osource != None:
            osource.write(line + '\n') 

if osource != None:
    osource.close()
ofile.close()
