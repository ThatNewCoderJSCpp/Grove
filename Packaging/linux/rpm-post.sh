if [ ! -e /usr/bin/grove ] || [ -L /usr/bin/grove ]; then
    ln -sf /opt/grove/bin/grove /usr/bin/grove
fi
