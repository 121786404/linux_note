#!/bin/bash
#find . ! -iregex '.*\.git\*' -type f -name '*.[c|h]' -exec file {} \; | cut -d':' -f2 | sort | uniq -c 
#find . ! -iregex '.*\.git\*' -type f -name '*.[c|h]' -exec bash -c "enca -L zh_CN {}|grep GB2312 > /dev/null && echo {}" \;
find . ! -iregex '.*\.git\*' -type f -name '*.[c|h]' -exec bash -c "enca -L zh_CN {}|grep GB2312 > /dev/null && enconv -L zh_CN -x UTF-8 {}" \;

