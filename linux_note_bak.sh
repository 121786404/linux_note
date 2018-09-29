#!/bin/bash
 
cd linux-stable
git diff --name-only | xargs -I {} cp {} --parents ../tmp
find ../tmp/ -type f -print | xargs chmod -x
cd ..