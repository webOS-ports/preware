#!/bin/sh

# Installs the package manager service (org.webosinternals.ipkgservice) from Preware 2's
# copy, if it is missing or differs. Run as root by pmPostInstall.script and at boot by
# the preware2-service-check upstart job: removing the original Preware also removes the
# service it shares with Preware 2.

SID="org.webosinternals.ipkgservice"
APPDIR=`dirname $0`/..
APPDIR=`cd $APPDIR && pwd`

[ -f $APPDIR/bin/${SID} ] || exit 1

# The original Preware installs the very same service. Only (re)install it when it is
# missing or differs from ours: stopping the service while it is installing this package
# (e.g. when Preware 2 is installed or updated from Preware) would break that install.
#
SERVICE_OK=1
[ -f /var/usr/sbin/${SID} ] || SERVICE_OK=0
[ -f /var/palm/event.d/${SID} ] || SERVICE_OK=0
[ -f /var/palm/system-services/${SID}.service ] || SERVICE_OK=0
[ -f /var/palm/ls2/roles/prv/${SID}.json ] || SERVICE_OK=0
[ -f /var/palm/ls2/roles/pub/${SID}.json ] || SERVICE_OK=0
if [ "$SERVICE_OK" -eq 1 ] ; then
  cmp -s $APPDIR/bin/${SID} /var/usr/sbin/${SID} || SERVICE_OK=0
fi

if [ "$SERVICE_OK" -eq 0 ] ; then

  # Stop the service if running
  /sbin/stop ${SID} || true
  /usr/bin/killall -9 ${SID} || true

  # Install the ipkgservice executable
  mkdir -p /var/usr/sbin/
  install -m 755 $APPDIR/bin/${SID} /var/usr/sbin/${SID}

  # Install the dbus service
  mkdir -p /var/palm/system-services
  cp $APPDIR/dbus/${SID}.service /var/palm/system-services/${SID}.service

  # Install the ls2 roles
  mkdir -p /var/palm/ls2/roles/prv /var/palm/ls2/roles/pub
  cp $APPDIR/dbus/${SID}.json /var/palm/ls2/roles/prv/${SID}.json
  cp $APPDIR/dbus/${SID}.json /var/palm/ls2/roles/pub/${SID}.json
  /usr/bin/ls-control scan-services || true

  # Install the upstart script atomically: upstart re-reads jobs in /var/palm/event.d as
  # soon as they change, so write it elsewhere on the same filesystem and rename it.
  mkdir -p /var/palm/event.d
  cp $APPDIR/upstart/${SID} /var/palm/.${SID}.new
  mv /var/palm/.${SID}.new /var/palm/event.d/${SID}

  # Start the service
  /sbin/start ${SID}

elif ! grep -q '^Type=static' /var/palm/system-services/${SID}.service ; then

  # The original Preware's D-Bus service file lets the hub start a second instance
  # of the service (see service-guard.sh). Replacing it needs no restart.
  cp $APPDIR/dbus/${SID}.service /var/palm/system-services/${SID}.service
  /usr/bin/ls-control scan-services || true

fi

exit 0
