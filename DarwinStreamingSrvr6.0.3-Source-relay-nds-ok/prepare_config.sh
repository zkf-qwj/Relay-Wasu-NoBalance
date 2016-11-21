
USER_NAME=`id -n -u`
GROUP_NAME=`id -n -g`

USER_ID=`id -u`
GROUP_ID=`id -g`


RTSP_PORT="30$USER_ID"
RTSP_PORT_EXTRA_1="32$USER_ID"
RTSP_PORT_EXTRA_2="33$USER_ID"
RTSP_PORT_EXTRA_2="34$USER_ID"
#RTSP_PORT_EXTRA_3=`expr $RTSP_PORT_EXTRA_2 + 1`

CONTROLLER_PH_PORT="36$USER_ID"
PH_LISTEN_PORT="37$USER_ID"

USER_CFG_FILE="$USER_NAME"_streamingserver.xml

sed \
    -e "s/\(run_user_name.*\)qtss/\1$USER_NAME/" \
    -e "s/\(run_group_name.*\)qtss/\1$GROUP_NAME/" \
    -e "s/554/$RTSP_PORT/" \
    -e "s/7070/$RTSP_PORT_EXTRA_1/" \
    -e "s/8000/$RTSP_PORT_EXTRA_2/" \
    -e "s/8001/$RTSP_PORT_EXTRA_3/" \
    -e "s/27272/$CONTROLLER_PH_PORT/" \
    -e "s/8500/$PH_LISTEN_PORT/" \
    -e "s/__CONTROLLER_IP__/127.0.0.1/" \
    -e "s/__BIND_INTERFACE__/127.0.0.1/" \
    -e "s/\/etc/\/home\/$USER_NAME/" \
    -e "s/\/var\/streaming\/logs/./" \
    -e "s/arts_request_logging\" TYPE=\"Bool16\" >false/arts_request_logging\" TYPE=\"Bool16\" >true/" \
    streamingserver.xml \
  > $USER_CFG_FILE

