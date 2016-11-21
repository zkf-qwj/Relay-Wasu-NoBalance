#!/bin/bash

# Install script for the Darwin Streaming Server

echo;echo Installing Darwin Streaming Server;echo

PKG="arts-darwin"
SVC="arts_darwin"
DEF_COMP_NAME="Controller_1"
CONTROLLER_IP=${CONTROLLER_IP:-127.0.0.1}

source /arts/bin/arts_setup_common.sh || exit 1;

assert_superuser

assert_installed_package $PKG

setup_common


    echo "Creating unprivileged user to run the server = \"qtss\"."

    /usr/sbin/userdel qtss
    /usr/sbin/useradd -M qtss > /dev/null 2>&1
    
    chown -R -f qtss /var/streaming/
    chown -R -f qtss /etc/streaming/
    chown -R -f qtss /arts/movies/

    if test "$ARTS_AUTOMATED_INSTALL" = "true"; then
        username=qtadm
        password=qtadm
    else
        # prompt the user to enter the admin username
        while [ "$username" = "" ]; do
            printf "In order to administer the Darwin Streaming Server you must create an administrator user.\n"
            printf "Note: The administrator username cannot contain spaces, "
            printf "or quotes, either single or double, and cannot be more "
            printf "than 80 characters long].\n"
    
            printf "Please enter a new administrator user name: "
            read username
            if [ "$username" = "" ]; then
                echo ""
                echo "Error: No username entered!"
                echo ""
            fi
        done
        echo ""
    
        # prompt the user to enter the admin password
        while [ "$password" = "" ]; do
            printf "\nYou must also enter a password for the administrator user"
            printf "Note: The administrator password cannot contain spaces, "
            printf "or quotes, either single or double, and cannot be more "
            printf "than 80 characters long].\n"
    
            printf "Please enter a new administrator Password: "
            stty -echo 2> /dev/null
            read password
            stty echo 2> /dev/null
            echo ""
            printf "Re-enter the new administrator password: "
            stty -echo 2> /dev/null
            read password1
            stty echo 2> /dev/null 
            if [ "$password" = "" ]; then
                echo ""
                echo "Error: No password entered!"
                echo ""
            fi
            if [ "$password" != "$password1" ]; then
                echo ""
                echo "Error: passwords entered do not match!"
                echo ""
                password=""
            fi
    
        done
        echo ""
    fi

    # Add the new admin username to /etc/streaming/qtusers
    /arts/bin/qtpasswd -p $password $username

    # Add the new admin username to /etc/streaming/qtgroups
    # and delete the default admin username
    echo admin: $username > /etc/streaming/qtgroups.tmp
    mv /etc/streaming/qtgroups.tmp /etc/streaming/qtgroups

    # Modify the server config to use public ip
    tmp_file=`mktemp`
    sed "s/__CONTROLLER_IP__/$CONTROLLER_IP/g" /etc/streaming/streamingserver.xml > $tmp_file
    mv $tmp_file /etc/streaming/streamingserver.xml

    tmp_file=`mktemp`
    sed "s/__BIND_INTERFACE__/$BIND_INTERFACE/g" /etc/streaming/streamingserver.xml > $tmp_file
    mv $tmp_file /etc/streaming/streamingserver.xml
    # Remove the default admin username to /etc/streaming/qtusers
    /arts/bin/qtpasswd -F -d 'aGFja21l' > /dev/null

    chown -R -f qtss /etc/streaming/

    chown -R -f qtss /var/streaming/

ask_and_start_service $SVC

ask_and_register_service $SVC

    echo Setup Complete!


