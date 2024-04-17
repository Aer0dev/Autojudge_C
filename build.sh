#!/bin/bash

echo "Now Compiling"

gcc autojudge.c -o autojudge

echo "Build has been Finished"
echo "Type ./autojudge -i <input_dir> -a <answer_dir> -t <time_limit> <target_src> to Execute"