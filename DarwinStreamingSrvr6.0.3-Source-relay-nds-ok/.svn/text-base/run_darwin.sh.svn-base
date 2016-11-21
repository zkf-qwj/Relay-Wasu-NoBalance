function set_title()
{
    echo -ne "\033]0;$*\007"
    export PROMPT_COMMAND="echo -ne \"\033]0;$*\"; echo -ne \"\007\""
}

MYNAME=`echo $0`;
PWD=`dirname $MYNAME`;
if test "$PWD" == "."; then
    PWD=`pwd`
fi
cd $PWD
PWD=`pwd`
echo "PWD : $PWD"

ARTS_EXT_LIB_DIR=$PWD/../../lighttpd/lighttpd-1.5.0-r2746/src:$PWD/../../../build/protobuf/lib

#
# Set the path variable for loading shared libs
#
export LD_LIBRARY_PATH=$ARTS_EXT_LIB_DIR:$LD_LIBRARY_PATH
echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"

USER_NAME=`id -n -u`
USER_CFG_FILE="$USER_NAME"_streamingserver.xml

set_title Darwin

DAEMONIZE=-d

APP_BINARY=DarwinStreamingServer

return_status() {
    USER_NAME=`id -n -u`
    f=`ps -aef | grep -i $APP_BINARY | grep -i $USER_NAME | grep -v grep | awk '{print $2}'`
    if test "$f" != ""; then
        echo -n "DSS is running. PID:"
        for i in $f; do
            echo -n "$i "
        done
        echo
    else
        echo "DSS is not running"
    fi
}

kill_app() {
    USER_NAME=`id -n -u`
    f=`ps -aef | grep -i $APP_BINARY | grep -i $USER_NAME | grep -v grep | awk '{print $2}'`
    if test "$f" != ""; then
        for i in $f; do
            echo "Killing DSS process $i"
            kill -9 $i
        done
    else
        echo "DSS is not running"
    fi
}

BGMODE=false

while test "$1" != ""; do
    case "$1" in
        -d) DAEMONIZE= ;;
        status) return_status; exit;;
        stop) kill_app; exit;;
        restart) kill_app; BGMODE=true;;
        start) BGMODE=true;;
        *) OTHER_ARGS+=" $1";;
    esac
    shift;
done

if test "$BGMODE" = "false"; then
    ./$APP_BINARY -d -c $USER_CFG_FILE
else
    ./$APP_BINARY -d -c $USER_CFG_FILE >& ./all_darwin.log &
    sleep 1
fi

