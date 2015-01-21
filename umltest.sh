#!/bin/bash

CURDIR="`pwd`"

cat > umltest.inner.sh <<EOF
#!/bin/sh
(
   set -e
   set -x
   insmod /usr/lib/uml/modules/\`uname -r\`/kernel/fs/fuse/fuse.ko
   cd "$CURDIR"
   ./tests.sh
   echo Success
)
if [ \$? = 0 ]; then
    # proper shutdown
    halt -f
else
    # crash UML
    exit 1
fi
EOF

chmod +x umltest.inner.sh

exec /usr/bin/linux.uml init=`pwd`/umltest.inner.sh rootfstype=hostfs rw
