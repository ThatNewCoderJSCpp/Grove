if [ "$1" = "0" ] && [ "$(readlink /usr/bin/grove 2>/dev/null)" = "/opt/grove/bin/grove" ]; then
    rm -f /usr/bin/grove
fi
